#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;


#ifdef SIMULATOR
#define SDL_MAIN_HANDLED /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
#include "lv_drivers/sdl/sdl.h"

static int tick_thread(void *data);

#endif // SIMUALTOR

static void hal_init(void);  

#include "websocket_client.h"
#include "notify_consumer.h"
#include "main_panel.h"
#include "spoolman_panel.h"
#include "init_panel.h"
#include "state.h"
#include "hv/hlog.h"
#include "hv/json.hpp"
#include "config.h"
#include "bedmesh_panel.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"

#include <mutex>
#include <vector>
#include <atomic>
#include <algorithm>

#define DISP_BUF_SIZE (128 * 1024)

using namespace hv;
using namespace std;


static lv_style_t style_container;
static lv_style_t style_imgbtn_pressed;
static lv_style_t style_imgbtn_disabled;
static lv_obj_t *screen_saver;

/*Will be called when the styles of the base theme are already added
  to add new styles*/
static void new_theme_apply_cb(lv_theme_t * th, lv_obj_t * obj)
{
    LV_UNUSED(th);

    if(lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_add_style(obj, &style_container, 0);
    }

    if (lv_obj_check_type(obj, &lv_imgbtn_class)) {
      lv_obj_add_style(obj, &style_imgbtn_pressed, LV_STATE_PRESSED);
      lv_obj_add_style(obj, &style_imgbtn_disabled, LV_STATE_DISABLED);
    }
}

#ifndef SIMULATOR
std::atomic_bool is_sleeping(false);

#endif // SIMULATOR

int main(void)
{
    hlog_disable();
    // config
    Config *conf = Config::get_instance();
    spdlog::debug("current path {}", std::string(fs::canonical("/proc/self/exe").parent_path()));

    auto config_path = fs::canonical("/proc/self/exe").parent_path() / "guppyconfig.json";
    conf->init(config_path.string());

    const std::string ll_path = conf->df() + "log_level";
    auto ll = spdlog::level::from_str(conf->get<std::string>(ll_path));

    auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
    // console_sink->set_level(spdlog::level::debug);
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
			 conf->get<std::string>("/log_path"), 1048576 * 10, 3);
    // file_sink->set_level(spdlog::level::debug);    

    spdlog::sinks_init_list log_sinks{console_sink, file_sink};
    auto klogger = std::make_shared<spdlog::logger>("guppyscreen", log_sinks);
    spdlog::register_logger(klogger);

    spdlog::set_level(ll);
    spdlog::set_default_logger(klogger);
    klogger->flush_on(ll);

#ifdef GUPPYSCREEN_VERSION
    spdlog::info("Guppy Screen Version: {}", GUPPYSCREEN_VERSION);
#endif // GUPPYSCREEN_VERSION

    spdlog::info("DPI: {}", LV_DPI_DEF);
    /*LittlevGL init*/
    lv_init();

#ifndef SIMULATOR
    /*Linux frame buffer device init*/
    fbdev_init();
    fbdev_unblank();
#endif

    hal_init();
    lv_png_init();

    lv_style_init(&style_container);
    lv_style_set_border_width(&style_container, 0);
    lv_style_set_radius(&style_container, 0);
    
    lv_style_init(&style_imgbtn_pressed);
    lv_style_set_img_recolor_opa(&style_imgbtn_pressed, LV_OPA_100);
    lv_style_set_img_recolor(&style_imgbtn_pressed, lv_palette_main(LV_PALETTE_BLUE));
    
    lv_style_init(&style_imgbtn_disabled);
    lv_style_set_img_recolor_opa(&style_imgbtn_disabled, LV_OPA_100);
    lv_style_set_img_recolor(&style_imgbtn_disabled, lv_palette_darken(LV_PALETTE_GREY, 1));
    /*Initialize the new theme from the current theme*/
    
    lv_theme_t * th_act = lv_disp_get_theme(NULL);
    static lv_theme_t th_new;
    th_new = *th_act;

    /*Set the parent theme and the style apply callback for the new theme*/
    lv_theme_set_parent(&th_new, th_act);
    lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);

    /*Assign the new theme to the current display*/
    lv_disp_set_theme(NULL, &th_new);

    mutex lv_lock;
    KWebSocketClient ws(NULL);
    /// preregister state to consume subscriptions
    ws.register_notify_update(State::get_instance());

    SpoolmanPanel spoolman_panel(ws, lv_lock);
    MainPanel main_panel(ws, lv_lock, spoolman_panel);
    main_panel.create_panel();

    InitPanel init_panel(main_panel,
			 main_panel.get_tune_panel().get_bedmesh_panel(),
			 lv_lock);

    std::string ws_url = fmt::format("ws://{}:{}/websocket",
				     conf->get<std::string>(conf->df() + "moonraker_host"),
				     conf->get<uint32_t>(conf->df() + "moonraker_port"));
    
    spdlog::info("connecting to printer at {}", ws_url);

    uint32_t display_sleep = conf->get<uint32_t>(conf->df() + "display_sleep_sec") * 1000;

    ws.connect(ws_url.c_str(),
	       [&init_panel, &ws]() { init_panel.connected(ws); },
	       [&init_panel, &ws]() { init_panel.disconnected(ws); });

    screen_saver = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen_saver, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_saver, LV_OPA_100, 0);
    lv_obj_move_background(screen_saver);

    /*Handle LitlevGL tasks (tickless mode)*/
    while(1) {
      lv_lock.lock();
      lv_timer_handler();
      lv_lock.unlock();

#ifndef SIMULATOR      
      if (lv_disp_get_inactive_time(NULL) > display_sleep) {
	if (!is_sleeping.load()) {
	  spdlog::debug("putting display to sleeping");
	  fbdev_blank();
	  lv_obj_move_foreground(screen_saver);
	  // spdlog::debug("screen saver foreground");
	  is_sleeping = true;
	}
      } else {
	if (is_sleeping.load()) {
	  spdlog::debug("waking up display");
	  fbdev_unblank();
	  lv_obj_move_background(screen_saver);
	  is_sleeping = false;
	}
      }
#endif // SIMULATOR

      usleep(5000);
    }

    return 0;
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}



#ifndef SIMULATOR

static void hal_init(void) {
    /*A small buffer for LittlevGL to draw the screen's content*/
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];

    /*Initialize a descriptor for the buffer*/
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, buf2, DISP_BUF_SIZE);

    /*Initialize and register a display driver*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = fbdev_flush;

    uint32_t width;
    uint32_t height;
    uint32_t dpi;
    fbdev_get_sizes(&width, &height, &dpi);
    
    disp_drv.hor_res    = width;
    disp_drv.ver_res    = height;
    Config *conf = Config::get_instance();
    auto rotate = conf->get_json("/display_rotate");
    if (!rotate.is_null()) {
      auto rotate_value = rotate.template get<uint32_t>();
      if (rotate_value > 0 && rotate_value < 4) {
	disp_drv.sw_rotate = 1;
	disp_drv.rotated = rotate_value;
      }
    }
    lv_disp_drv_register(&disp_drv);

    spdlog::debug("resolution {} x {}", width, height);
    
    lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
    lv_theme_t * th = height <= 480
      ? lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_12)
      : lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    lv_disp_set_theme(disp, th);

    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1); /*Basic initialization*/
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

    /*This function will be called periodically (by the library) to get the mouse position and state*/
    indev_drv_1.read_cb = evdev_read;    
    auto touch_calibrated = conf->get_json("/touch_calibrated");
    if (!touch_calibrated.is_null()) {
      auto is_calibrated = touch_calibrated.template get<bool>();
      if (is_calibrated) {
	indev_drv_1.read_cb = evdev_read_calibrated;
      }
    }
      
    lv_indev_t *mouse_indev = lv_indev_drv_register(&indev_drv_1);
}

#else // SIMULATOR

/**
 * Initialize the Hardware Abstraction Layer (HAL) for the LVGL graphics
 * library
 */
static void hal_init(void)
{
  /* Use the 'monitor' driver which creates window on PC's monitor to simulate a display*/
  sdl_init();
  /* Tick init.
   * You have to call 'lv_tick_inc()' in periodically to inform LittelvGL about
   * how much time were elapsed Create an SDL thread to do this*/
  SDL_CreateThread(tick_thread, "tick", NULL);

  /*Create a display buffer*/
  static lv_disp_draw_buf_t disp_buf1;
  static lv_color_t buf1_1[MONITOR_HOR_RES * 100];
  static lv_color_t buf1_2[MONITOR_HOR_RES * 100];
  lv_disp_draw_buf_init(&disp_buf1, buf1_1, buf1_2, MONITOR_HOR_RES * 100);

  /*Create a display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv); /*Basic initialization*/
  disp_drv.draw_buf = &disp_buf1;
  disp_drv.flush_cb = sdl_display_flush;
  spdlog::debug("resolution {} x {}", MONITOR_VER_RES, MONITOR_HOR_RES);
  disp_drv.hor_res = MONITOR_HOR_RES;
  disp_drv.ver_res = MONITOR_VER_RES;
  // disp_drv.sw_rotate = 1;
  // disp_drv.rotated = LV_DISP_ROT_270;
  disp_drv.antialiasing = 1;

  lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
  lv_theme_t * th = MONITOR_HOR_RES <= 480
    ? lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_12)
    : lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
  lv_disp_set_theme(disp, th);
 
  lv_group_t * g = lv_group_create();
  lv_group_set_default(g);

  /* Add the mouse as input device
   * Use the 'mouse' driver which reads the PC's mouse*/
  // mouse_init();
  static lv_indev_drv_t indev_drv_1;
  lv_indev_drv_init(&indev_drv_1); /*Basic initialization*/
  indev_drv_1.type = LV_INDEV_TYPE_POINTER;

  /*This function will be called periodically (by the library) to get the mouse position and state*/
  indev_drv_1.read_cb = sdl_mouse_read;
  lv_indev_t *mouse_indev = lv_indev_drv_register(&indev_drv_1);

  // keyboard_init();
  static lv_indev_drv_t indev_drv_2;
  lv_indev_drv_init(&indev_drv_2); /*Basic initialization*/
  indev_drv_2.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv_2.read_cb = sdl_keyboard_read;
  lv_indev_t *kb_indev = lv_indev_drv_register(&indev_drv_2);
  lv_indev_set_group(kb_indev, g);
  // mousewheel_init();
  static lv_indev_drv_t indev_drv_3;
  lv_indev_drv_init(&indev_drv_3); /*Basic initialization*/
  indev_drv_3.type = LV_INDEV_TYPE_ENCODER;
  indev_drv_3.read_cb = sdl_mousewheel_read;

  lv_indev_t * enc_indev = lv_indev_drv_register(&indev_drv_3);
  lv_indev_set_group(enc_indev, g);

  // /*Set a cursor for the mouse*/
  // LV_IMG_DECLARE(mouse_cursor_icon); /*Declare the image file.*/
  // lv_obj_t * cursor_obj = lv_img_create(lv_scr_act()); /*Create an image object for the cursor */
  // lv_img_set_src(cursor_obj, &mouse_cursor_icon);           /*Set the image source*/
  // lv_indev_set_cursor(mouse_indev, cursor_obj);             /*Connect the image  object to the driver*/
}

/**
 * A task to measure the elapsed time for LVGL
 * @param data unused
 * @return never return
 */
static int tick_thread(void *data) {
  (void)data;

  while(1) {
    SDL_Delay(5);
    lv_tick_inc(5); /*Tell LittelvGL that 5 milliseconds were elapsed*/
  }

  return 0;
}

#endif // SIMULATOR
