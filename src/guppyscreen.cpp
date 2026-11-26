#include "guppyscreen.h"

#include "config.h"
#include "theme.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#ifdef GUPPY_SDL
#include "lv_drivers/sdl/sdl_common.h"
#endif
#include "logger.h"
#include "state.h"
#if defined(GUPPY_CALIBRATE) || (defined(GUPPY_FBDEV) && !defined(COSMOS))
#include <fstream>
#endif
#ifdef GUPPY_SDL
#include <cstdlib>
#endif
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

namespace {
constexpr double calibration_version = 2.0;

#if defined(GUPPY_FBDEV) && !defined(COSMOS)
std::string backlight_dir;
std::string backlight_max;

void backlight_init() {
  std::error_code ec;
  for (const auto &e : fs::directory_iterator("/sys/class/backlight", ec)) {
    std::ifstream max(e.path() / "max_brightness");
    if (max >> backlight_max) {
      backlight_dir = e.path();
      return;
    }
  }
}

void backlight_set(const std::string &value) {
  std::ofstream(backlight_dir + "/brightness") << value;
}
#endif
}

GuppyScreen *GuppyScreen::instance = NULL;
lv_style_t GuppyScreen::style_container;
lv_theme_t GuppyScreen::th_new;

#ifndef OS_ANDROID
lv_obj_t *GuppyScreen::screen_saver = NULL;
#endif

KWebSocketClient GuppyScreen::ws(NULL);

std::mutex GuppyScreen::lv_lock;

GuppyScreen::GuppyScreen()
  : spoolman_panel(std::make_unique<SpoolmanPanel>(ws, lv_lock))
  , mmu_panel(std::make_unique<MmuPanel>(ws, lv_lock))
#ifdef MMU_BACKEND_AFC
  , afc_backend(mmu_panel != nullptr ? std::make_unique<AfcBackend>(ws) : nullptr)
#endif
#ifdef MMU_BACKEND_HH
  , hh_backend(mmu_panel != nullptr ? std::make_unique<HhBackend>(ws) : nullptr)
#endif
  , main_panel(ws, lv_lock, spoolman_panel.get(), mmu_panel.get())
  , init_panel(main_panel, lv_lock)
{
#ifdef MMU_BACKEND_AFC
  if (afc_backend != nullptr) {
    mmu_panel->add_backend(afc_backend.get());
  }
#endif
#ifdef MMU_BACKEND_HH
  if (hh_backend != nullptr) {
    mmu_panel->add_backend(hh_backend.get());
  }
#endif
  main_panel.create_panel();
}

GuppyScreen *GuppyScreen::get() {
  if (instance == NULL) {
    instance = new GuppyScreen();
  }

  return instance;
}

GuppyScreen *GuppyScreen::init(std::function<void(lv_color_t, lv_color_t)> hal_init) {
  hlog_disable();

  // config
  Config *conf = Config::get_instance();
  auto ll = conf->get<std::string>("/ui/log_level");
  set_log_level(ll);

  auto primary_color = Theme::cfg_col("primary_colour");
  auto secondary_color = Theme::cfg_col("secondary_colour");

  LOG_INFO("GrumpyScreen Version: {}-{}", GUPPYSCREEN_BRANCH, GUPPYSCREEN_VERSION);

  /*LittlevGL init*/
  lv_init();

  /*Linux frame buffer device init*/
#ifdef GUPPY_FBDEV
  fbdev_init();
  fbdev_unblank();
#ifndef COSMOS
  if (conf->get<bool>("/ui/display_sleep_backlight", false)) {
    backlight_init();
    if (!backlight_dir.empty()) {
      backlight_set(backlight_max);
    }
  }
#endif
#endif

  hal_init(primary_color, secondary_color);
  lv_png_init();

  lv_style_init(&style_container);
  lv_style_set_border_width(&style_container, 0);
  lv_style_set_radius(&style_container, 0);
  // LVGL's own card style, which every plain container wears, sets its text
  // colour, and that beats what a label would inherit from the page. Say it
  // here so text without a colour of its own follows the theme.
  lv_style_set_text_color(&style_container, Theme::col(Theme::TEXT));


  // Initia1ize the new theme from the current theme
  lv_theme_t *th_act = lv_disp_get_theme(NULL);
  th_new = *th_act;

  // Set the parent theme and the style apply callback for the new theme
  lv_theme_set_parent(&th_new, th_act);
  lv_theme_set_apply_cb(&th_new, &GuppyScreen::new_theme_apply_cb);

  // Assign the new theme to the current display
  lv_disp_set_theme(NULL, &th_new);

  ws.register_notify_update(State::get_instance());

  GuppyScreen *gs = GuppyScreen::get();

#ifndef OS_ANDROID
  screen_saver = lv_obj_create(lv_scr_act());

  lv_obj_set_size(screen_saver, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(screen_saver, LV_OPA_100, 0);
#ifdef GUPPY_FBDEV
#ifndef COSMOS
  // with the backlight off it only has to catch the waking touch
  if (!backlight_dir.empty()) {
    lv_obj_set_style_opa(screen_saver, LV_OPA_TRANSP, 0);
  }
#endif
#else
  // nothing blanks the panel here, so the screen saver itself has to be dark
  lv_obj_set_style_bg_color(screen_saver, lv_color_black(), 0);
#endif
  lv_obj_move_background(screen_saver);
#endif

#ifdef GUPPY_CALIBRATE
  lv_obj_t *main_screen = lv_disp_get_scr_act(NULL);
  std::vector<float> c = GuppyScreen::load_calibration_coeff();
  if (c.empty()) {
    lv_tc_register_coeff_save_cb(&GuppyScreen::save_calibration_coeff);
    lv_obj_t *touch_calibrate_scr = lv_tc_screen_create();
    lv_disp_load_scr(touch_calibrate_scr);
    lv_tc_screen_start(touch_calibrate_scr);
    lv_obj_add_event_cb(touch_calibrate_scr, &GuppyScreen::handle_calibrated, LV_EVENT_READY, main_screen);
    LOG_INFO("running touch calibration");
  } else {
    // load calibration data
    lv_tc_coeff_t coeff = {true, c[0], c[1], c[2], c[3], c[4], c[5]};
    lv_tc_set_coeff(coeff, false);
    LOG_INFO("loaded calibration coefficients");
  }
#endif

  // Connect last. The websocket callbacks build the panels from the network
  // thread under lv_lock, but nothing above takes that lock, so connecting
  // any earlier lets a quick Moonraker reply race this function's own LVGL
  // calls and corrupt LVGL's event list.
  std::string ws_url = fmt::format("ws://{}:{}/websocket",
                                   conf->get<std::string>("/moonraker/host"),
                                   conf->get<uint32_t>("/moonraker/port"));

  LOG_INFO("connecting to printer at {}", ws_url);
  gs->connect_ws(ws_url);

  return gs;
}

void GuppyScreen::loop() {
  /*Handle LitlevGL tasks (tickless mode)*/
#ifndef OS_ANDROID
  // Android manages screen timeout and brightness through the device settings.
  std::atomic_bool is_sleeping(false);
  Config *conf = Config::get_instance();
  int32_t display_sleep = conf->get<int32_t>("/ui/display_sleep_sec") * 1000;
  uint32_t inactive_baseline = lv_disp_get_inactive_time(NULL);
  uint32_t last_inactive = inactive_baseline;
  uint32_t sleep_checked = lv_tick_get();

  LOG_DEBUG("Display sleep timeout: {} ms, starting inactivity baseline: {} ms",
           display_sleep,
           inactive_baseline);
#endif

  while (1) {
#ifdef GUPPY_SDL
    if (sdl_quit_qry) {
      std::exit(0);
    }
#endif

    lv_lock.lock();
    const uint32_t time_till_next = lv_timer_handler();

#ifdef OS_ANDROID
    android_set_menu_visible(instance->main_panel.printer_menu_visible());
#endif

#ifndef OS_ANDROID
    // The Settings panel updates Config in place, so do not keep using the
    // startup snapshot after the user changes the timeout.
    // Once a second is soon enough, and spares a config lookup every pass.
    if (lv_tick_elaps(sleep_checked) >= 1000) {
      sleep_checked = lv_tick_get();
      display_sleep = conf->get<int32_t>("/ui/display_sleep_sec") * 1000;
    }
#endif

#ifdef GUPPY_SDL
    if (sdl_quit_qry) {
      lv_lock.unlock();
      std::exit(0);
    }
#endif

#ifndef OS_ANDROID
    if (display_sleep != -1) {
      const uint32_t current_inactive = lv_disp_get_inactive_time(NULL);
      if (current_inactive < last_inactive) {
        inactive_baseline = current_inactive;
      }
      last_inactive = current_inactive;

      const uint32_t effective_inactive = current_inactive - inactive_baseline;
      if (effective_inactive > static_cast<uint32_t>(display_sleep)) {
        if (!is_sleeping.load()) {
          LOG_DEBUG("putting display to sleeping after {} ms effective inactivity", effective_inactive);
#ifdef GUPPY_FBDEV
#ifdef COSMOS
          fbdev_blank();
#else
          if (backlight_dir.empty()) {
            fbdev_blank();
          } else {
            backlight_set("0");
          }
#endif
#endif
          lv_obj_move_foreground(screen_saver);
          is_sleeping = true;
        }
      } else {
        if (is_sleeping.load()) {
          LOG_DEBUG("waking up display");
#ifdef GUPPY_FBDEV
#ifdef COSMOS
          fbdev_unblank();
#else
          if (backlight_dir.empty()) {
            fbdev_unblank();
          } else {
            backlight_set(backlight_max);
          }
#endif
#endif
          lv_obj_move_background(screen_saver);
          is_sleeping = false;
        }
      }
    }
#endif

    lv_lock.unlock();

    // Wait until LVGL's next timer is due, capped at one display frame so
    // updates from the network thread are rendered promptly. SDL also needs
    // its event poll serviced every 10 ms. The lock is released while waiting.
#ifdef GUPPY_SDL
    const uint32_t max_wait = 10;
#else
    const uint32_t max_wait = LV_DISP_DEF_REFR_PERIOD;
#endif
    uint32_t wait_ms = time_till_next;
    if (wait_ms < 1) {
      wait_ms = 1;
    }
    else if (wait_ms > max_wait) {
      wait_ms = max_wait;
    }
    usleep(wait_ms * 1000);
  }
}

std::mutex &GuppyScreen::get_lock() {
  return lv_lock;
}

void GuppyScreen::connect_ws(const std::string &url) {
  init_panel.waiting_for_moonraker();
  ws.connect(url.c_str(),
   [this]() { init_panel.websocket_connected(ws); },
   [this]() { init_panel.klippy_ready(ws); },
   [this]() { init_panel.klippy_disconnected(ws); },
   [this]() { init_panel.klippy_shutdown(ws); },
   [this]() { init_panel.websocket_disconnected(ws); });
}

void GuppyScreen::new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  if (lv_obj_check_type(obj, &lv_obj_class)) {
    lv_obj_add_style(obj, &style_container, 0);
  }

  // Widget looks by class: a panel never styles a keyboard, entry or table
  // itself, so one edit to Theme reaches all of them. Panels may still add a
  // style on top for a genuinely different role (the console log is a panel).
  // Screens are skipped: the first one is created before there is an active
  // screen to read the primary colour from, and styles() would hang on it.
  if (lv_obj_get_parent(obj) == NULL) {
    return;
  }
  Theme::Styles &s = Theme::styles();
  // every text button is the stock grey button; LVGL's own theme, this one's
  // parent, darkens it under a finger and greys it out when disabled
  if (lv_obj_check_type(obj, &lv_btn_class)) {
    lv_obj_add_style(obj, &s.btn, LV_PART_MAIN);
  }
  // nothing draws a scrollbar: every list in the UI is swiped
  if (!lv_obj_check_type(obj, &lv_label_class) && !lv_obj_check_type(obj, &lv_img_class)) {
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
  }
  // button matrices (selectors, dialog button rows) and keyboards share the
  // key look; the one difference is what CHECKED means
  if (lv_obj_check_type(obj, &lv_btnmatrix_class) || lv_obj_check_type(obj, &lv_keyboard_class)) {
    lv_obj_add_style(obj, &s.key_body, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.key, LV_PART_ITEMS);
    lv_obj_add_style(obj, &s.key_pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_style(obj, &s.key_disabled, LV_PART_ITEMS | LV_STATE_DISABLED);
    // a matrix's checked key is the chosen value: accent. A keyboard's checked
    // keys are its control keys: plain.
    if (lv_obj_check_type(obj, &lv_keyboard_class)) {
      lv_obj_add_style(obj, &s.key_ctrl, LV_PART_ITEMS | LV_STATE_CHECKED);
    } else {
      lv_obj_add_style(obj, &s.key_checked, LV_PART_ITEMS | LV_STATE_CHECKED);
    }
  }
  if (lv_obj_check_type(obj, &lv_textarea_class)) {
    lv_obj_add_style(obj, &s.input, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.input_placeholder, LV_PART_TEXTAREA_PLACEHOLDER);
  }
  // a dropdown is an entry that opens a list: the closed box and the list share
  // the entry's look; its rows take the surface, and the chosen one the accent
  if (lv_obj_check_type(obj, &lv_dropdown_class)) {
    lv_obj_add_style(obj, &s.input, LV_PART_MAIN);
  }
  if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
    lv_obj_add_style(obj, &s.input, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.option, LV_PART_SELECTED);
    lv_obj_add_style(obj, &s.key_checked, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_style(obj, &s.key_pressed, LV_PART_SELECTED | LV_STATE_PRESSED);
  }
  if (lv_obj_check_type(obj, &lv_table_class)) {
    lv_obj_add_style(obj, &s.table, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.table_cell, LV_PART_ITEMS);
  }
  // sliders, bars, switches, arcs, spinners: groove in the raised grey, the
  // filled part in the accent, knobs in the text colour
  if (lv_obj_check_type(obj, &lv_arc_class) || lv_obj_check_type(obj, &lv_spinner_class)) {
    lv_obj_add_style(obj, &s.arc_track, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.arc_fill, LV_PART_INDICATOR);
  } else if (lv_obj_check_type(obj, &lv_switch_class)) {
    // a switch is a grey channel that shows the accent only once it is on, with
    // a knob in the icon colour
    lv_obj_add_style(obj, &s.track, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.switch_knob, LV_PART_KNOB);
    lv_obj_add_style(obj, &s.track, LV_PART_INDICATOR);
    lv_obj_add_style(obj, &s.fill, LV_PART_INDICATOR | LV_STATE_CHECKED);
#ifdef OS_ANDROID
    // LVGL's default switch is a fixed 36x21px (from LV_DPI_DEF, not the
    // screen), a speck on a phone; size it from the touch target instead
    lv_obj_set_size(obj, Theme::touch_h() * 7 / 8, Theme::touch_h() / 2);
#endif
  } else if (lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_bar_class)) {
    lv_obj_add_style(obj, &s.groove, LV_PART_MAIN);
    lv_obj_add_style(obj, &s.fill, LV_PART_INDICATOR);
    lv_obj_add_style(obj, &s.knob, LV_PART_KNOB);
    if (lv_obj_check_type(obj, &lv_slider_class)) {
      lv_obj_add_style(obj, &s.slider, LV_PART_MAIN);
    }
  }
}

#ifdef GUPPY_CALIBRATE
void GuppyScreen::handle_calibrated(lv_event_t *event) {
  LOG_INFO("finished calibration");
  lv_obj_t *main_screen = (lv_obj_t *)event->user_data;
  lv_disp_load_scr(main_screen);
}

std::vector<float> GuppyScreen::load_calibration_coeff() {
  std::string config_path = fs::canonical("/proc/self/exe").parent_path() / "calibration.json";
  std::ifstream f(config_path);
  if (!f.is_open()) {
    return {};
  }

  json j;
  f >> j;
  if (!j.is_object()) {
    LOG_INFO("discarding calibration data: legacy or invalid format");
    return {};
  }

  if (!j.contains("version") || !j["version"].is_number() ||
      j["version"].get<double>() != calibration_version) {
    LOG_INFO("discarding calibration data: missing or unsupported version (expected {})", calibration_version);
    return {};
  }

  if (!j.contains("display_rotate") || !j["display_rotate"].is_number_unsigned()) {
    LOG_INFO("discarding calibration data: missing display_rotate");
    return {};
  }

  Config *conf = Config::get_instance();
  auto current_rotate = conf->get<std::uint32_t>("/ui/display_rotate");

  auto saved_rotate = j["display_rotate"].get<std::uint32_t>();
  if (saved_rotate != current_rotate) {
    LOG_INFO("discarding calibration data: display_rotate changed from {} to {}", saved_rotate, current_rotate);
    return {};
  }

  if (!j.contains("calibrations") || !j["calibrations"].is_array()) {
    LOG_INFO("discarding calibration data: missing calibrations");
    return {};
  }

  const auto& calibration_values = j["calibrations"];
  if (calibration_values.size() != 6) {
    LOG_INFO("discarding calibration data: expected 6 calibration coefficients, got {}", calibration_values.size());
    return {};
  }

  std::vector<float> coeffs;
  coeffs.reserve(calibration_values.size());
  for (const auto& v : calibration_values) {
    coeffs.push_back(v.get<float>());
  }
  return coeffs;
}

void GuppyScreen::save_calibration_coeff(lv_tc_coeff_t coeff) {
  auto config_path = fs::canonical("/proc/self/exe").parent_path() / "calibration.json";
  Config *conf = Config::get_instance();
  json j = {
    {"version", calibration_version},
    {"display_rotate", conf->get<std::uint32_t>("/ui/display_rotate")},
    {"calibrations", {coeff.a, coeff.b, coeff.c, coeff.d, coeff.e, coeff.f}},
  };
  std::ofstream f(config_path, std::ios::trunc);
  f << j.dump(2);
}
#endif

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void) {
  static uint64_t start_ms = 0;
  if (start_ms == 0) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    start_ms = static_cast<uint64_t>(ts_start.tv_sec) * 1000ULL +
               static_cast<uint64_t>(ts_start.tv_nsec) / 1000000ULL;
  }

  struct timespec ts_now;
  clock_gettime(CLOCK_MONOTONIC, &ts_now);
  const uint64_t now_ms = static_cast<uint64_t>(ts_now.tv_sec) * 1000ULL +
                          static_cast<uint64_t>(ts_now.tv_nsec) / 1000000ULL;

  return static_cast<uint32_t>(now_ms - start_ms);
}
