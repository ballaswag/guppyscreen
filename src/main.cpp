#include "lvgl/lvgl.h"
#include "theme.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#ifdef GUPPY_SDL
#include "lv_drivers/sdl/sdl.h"
#endif
#ifdef GUPPY_CALIBRATE
#include "lv_tc.h"
#include "lv_tc_screen.h"
#endif
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <cstdlib>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

static void hal_init(lv_color_t p, lv_color_t s);

#include "guppyscreen.h"
#include "logger.h"
#include "hv/hlog.h"
#include "config.h"

#include <algorithm>

using namespace hv;

#define DISP_BUF_SIZE (128 * 1024)

#ifdef GUPPY_SDL
static int sdl_window_event_watch(void *userdata, SDL_Event *event) {
    (void) userdata;

    if (event->type == SDL_QUIT) {
        std::_Exit(0);
    }

    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE) {
        sdl_quit_qry = true;
    }

    return 0;
}
#endif

int main(void) {
    const char* config_file_env = std::getenv("CONFIG_FILE");
    fs::path config_path = config_file_env && config_file_env[0] != '\0'
        ? fs::path(config_file_env)
        : fs::canonical("/proc/self/exe").parent_path() / "grumpyscreen.cfg";

    Config *conf = Config::get_instance();
    if (fs::exists(config_path)) {
        if (!conf->load(config_path.string())) {
            LOG_ERROR("Failed to load {}", config_path.string());
            return 1;
        }
    } else {
        LOG_ERROR("Config file {} not found", config_path.string());
        return 1;
    }

    const char* config_override_env = std::getenv("CONFIG_OVERRIDE_FILE");
    if (config_override_env && config_override_env[0] != '\0') {
        fs::path config_override_path = fs::path(config_override_env);
        conf->set_override_path(config_override_path.string());
        if (fs::exists(config_override_path)) {
            if (!conf->load_override(config_override_path.string())) {
                LOG_ERROR("Failed to load override config {}", config_override_path.string());
                return 1;
            }
        } else {
            LOG_INFO("Override config file {} not found, continuing without overrides", config_override_path.string());
        }
    }

    GuppyScreen::init(hal_init);
    GuppyScreen::loop();
    return 0;
}

static void hal_init(lv_color_t primary, lv_color_t secondary) {
    lv_disp_t * disp = nullptr;

    /*A small buffer for LittlevGL to draw the screen's content*/
    static lv_color_t buf[DISP_BUF_SIZE];

    /*Initialize a descriptor for the buffer*/
    // Single buffered: fbdev_flush and sdl_display_flush both copy the pixels
    // and call lv_disp_flush_ready before returning, so a second buffer never
    // overlaps rendering with flushing and only costs another 512 KiB of RAM.
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    /*Initialize and register a display driver*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;

#ifdef GUPPY_SDL
    sdl_init();
    SDL_AddEventWatch(sdl_window_event_watch, nullptr);
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = SDL_HOR_RES;
    disp_drv.ver_res = SDL_VER_RES;
#else
    disp_drv.flush_cb   = fbdev_flush;

    uint32_t width;
    uint32_t height;
    uint32_t dpi;
    fbdev_get_sizes(&width, &height, &dpi);
    
    disp_drv.hor_res    = width;
    disp_drv.ver_res    = height;
    disp_drv.dpi        = LV_DPI_DEF * width / 480;  // LVGL defaults scale with the screen

    const char *rotate_env = std::getenv("DISPLAY_ROTATE");
    std::uint32_t rotate_value;
    if (rotate_env != nullptr && rotate_env[0] != '\0') {
        char *end = nullptr;
        const unsigned long rotate = std::strtoul(rotate_env, &end, 10);
        if (end == rotate_env || *end != '\0' || rotate > 3) {
            LOG_ERROR("Invalid DISPLAY_ROTATE='{}', expected 0-3", rotate_env);
            rotate_value = 0;
        } else {
            rotate_value = static_cast<std::uint32_t>(rotate);
        }
    } else {
        // Fall back to the config file for legacy installations.
        rotate_value = Config::get_instance()->get<std::uint32_t>("/ui/display_rotate");
    }

    if (rotate_value > 0 && rotate_value < 4) {
      disp_drv.sw_rotate = 1;
      disp_drv.rotated = rotate_value;
    }
#endif

    disp = lv_disp_drv_register(&disp_drv);

#ifdef GUPPY_SDL
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.read_cb = sdl_mouse_read;
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv_1);
#else
    const char *path = std::getenv("LVGL_EVDEV_DEV");
    if (path != nullptr && path[0] != '\0') {
        LOG_INFO("Input Device is: {}", path);
        evdev_set_file(const_cast<char *>(path));
    } else { // k1 will continue to use this
        evdev_init();
    }
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.read_cb = evdev_read; // no calibration
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

#ifdef GUPPY_CALIBRATE
    lv_tc_indev_drv_init(&indev_drv_1, evdev_read);
#endif
    lv_indev_drv_register(&indev_drv_1);
#endif

    // 12 on a 480x272 panel, 20 on 800x480 -- the same sizes the build used to
    // pick with an #ifdef, now read off the display that is actually attached.
    // Dark or light follows the configured page colour, so what LVGL paints on
    // its own (scrollbars, cursors, chart defaults) suits a light theme too.
    const bool dark = lv_color_brightness(Theme::col(Theme::BG)) < 128;
    lv_theme_t * th = lv_theme_default_init(disp, primary, secondary, dark, Theme::scale_font(12));
    lv_disp_set_theme(disp, th);
}
