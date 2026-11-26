#include "led_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"
#include "icons.h"

using namespace Theme;

namespace {
bool get_led_pwm(const json &led) {
  auto pwm_it = led.find("pwm");
  if (pwm_it != led.end() && pwm_it->is_boolean()) {
    return pwm_it->template get<bool>();
  }
  return true;
}

std::string get_led_display_name(const json &led, const std::string &fallback) {
  auto display_name_it = led.find("display_name");
  if (display_name_it != led.end() && display_name_it->is_string()) {
    return display_name_it->template get<std::string>();
  }
  return fallback;
}
} // namespace

LedPanel::LedPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , ledpanel_cont(create_screen(NULL))
  , leds_cont(create_row(ledpanel_cont))
  , side_cont(create_row(ledpanel_cont))
  , all_on_btn(side_cont, Icons::LIGHT_IMG, "All On", &LedPanel::_handle_callback, this)
  , all_off_btn(side_cont, Icons::LIGHT_OFF, "All Off", &LedPanel::_handle_callback, this)
  , back_btn(side_cont, Icons::BACK, "Back", &LedPanel::_handle_callback, this)
{
  // the list takes the width, the action tiles their own column
  lv_obj_set_flex_flow(ledpanel_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(leds_cont, 0, lv_pct(100));
  lv_obj_set_flex_grow(leds_cont, 1);
  lv_obj_set_flex_flow(leds_cont, LV_FLEX_FLOW_COLUMN);
  manage_scroll(leds_cont);
  lv_obj_set_size(side_cont, scale_w(84), lv_pct(100));
  lv_obj_set_flex_flow(side_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(side_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  for (ButtonContainer *b : {&all_on_btn, &all_off_btn, &back_btn}) {
    b->use_plain();
    lv_obj_set_width(b->get_container(), lv_pct(100));
    lv_obj_set_height(b->get_container(), action_tile_h());
  }
  ws.register_notify_update(this);
}

LedPanel::~LedPanel() {
  if (ledpanel_cont != NULL) {
    lv_obj_del(ledpanel_cont);
    ledpanel_cont = NULL;
  }

  leds.clear();

  ws.unregister_notify_update(this);
}

void LedPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (auto &l : leds) {
    // hack for output_pin leds
    auto value = j[json::json_pointer(fmt::format("/params/0/{}/value", l.first))];
    if (!value.is_null()) {
      const double raw_value = value.template get<double>();
      int v = static_cast<int>(raw_value * 100);
      l.second->update_value(v);
      if (!single_led_id.empty() && l.first == single_led_id) {
        single_led_last_value = raw_value;
        single_led_last_value_valid = true;
      }
    }

    value = j[json::json_pointer(fmt::format("/params/0/{}/color_data", l.first))];
    if (!value.is_null() && value.size() > 0) {
      // color_data = [[r,b,g,w]]
      value =  value.at(0);
      if (value.size() == 4) {
        const double raw_value = value.at(3).template get<double>();
        int v = static_cast<int>(raw_value * 100);
        l.second->update_value(v);
        if (!single_led_id.empty() && l.first == single_led_id) {
          single_led_last_value = raw_value;
          single_led_last_value_valid = true;
        }
      }
    }
  }
}

void LedPanel::init(json &l) {
  leds.clear();
  single_led_id.clear();
  single_led_is_output_pin = false;
  single_led_last_value = 0.0;
  single_led_last_value_valid = false;

  size_t created_led_count = 0;
  std::string created_led_id;
  bool created_led_is_output_pin = false;
  bool created_led_pwm = true;

  for (auto &led : l) {
    auto id_it = led.find("id");
    if (id_it == led.end() || !id_it->is_string()) {
      if (get_log_level() <= LogLevel::DEBUG) {
        LOG_DEBUG("skipping malformed LED config entry: {}", led.dump());
      }
      continue;
    }

    std::string key = id_it->template get<std::string>();
    if (get_log_level() <= LogLevel::DEBUG) {
      LOG_DEBUG("create led {}, {}", l.dump(), led.dump());
    }
    std::string display_name = get_led_display_name(led, KUtils::get_obj_name(key));
    bool pwm = get_led_pwm(led);
    const bool is_output_pin = key.rfind("output_pin ", 0) == 0;

    lv_event_cb_t null_cb = NULL;
    lv_event_cb_t led_cb = &LedPanel::_handle_led_update;
    if (!is_output_pin) {
      // standard led
      led_cb = &LedPanel::_handle_led_update_generic;
    }

    // a non-PWM LED is a switch: On rather than Max, and no slider
    auto lptr = std::make_shared<SliderContainer>(leds_cont, display_name.c_str(),
              Icons::CANCEL, "Off", led_cb, this,
              Icons::LIGHT_IMG, pwm ? "Max" : "On", led_cb, this,
              pwm ? led_cb : null_cb, this, "%");
    if (leds.insert({key, lptr}).second) {
      ++created_led_count;
      created_led_id = key;
      created_led_is_output_pin = is_output_pin;
      created_led_pwm = pwm;
    }
  }

  if (created_led_count == 1) {
    if (!created_led_pwm) {
      single_led_id = created_led_id;
      single_led_is_output_pin = created_led_is_output_pin;
      LOG_DEBUG("single LED mode enabled for {} (pwm={})",
                single_led_id,
                created_led_pwm);
    }
  }

  // Any number of rows, any screen size: build them, then let the scroller
  // measure whether they overflow; no row height or count is guessed anywhere
  const lv_coord_t row_h = SliderContainer::row_height(leds.size());
  for (auto &r : leds) {
    r.second->set_height(row_h);
  }
  refresh_scroll(leds_cont);
}

void LedPanel::activate() {
  if (!single_led_id.empty()) {
    toggle_single_led();
  } else {
    foreground();
  }
}

void LedPanel::foreground() {
  for (auto &l : leds) {
    // hack for output_pin leds
    auto led_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", l.first)));
    if (!led_value.is_null()) {
      const double raw_value = led_value.template get<double>();
      int v = static_cast<int>(raw_value * 100);
      l.second->update_value(v);
      if (!single_led_id.empty() && l.first == single_led_id) {
        single_led_last_value = raw_value;
        single_led_last_value_valid = true;
      }
    }
    
    led_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/color_data", l.first)));
    if (!led_value.is_null() && led_value.size() > 0) {
      // color_data = [[r,b,g,w]]
      led_value = led_value.at(0);
      if (led_value.size() == 4) {
        const double raw_value = led_value.at(3).template get<double>();
        int v = static_cast<int>(raw_value * 100);
        l.second->update_value(v);
        if (!single_led_id.empty() && l.first == single_led_id) {
          single_led_last_value = raw_value;
          single_led_last_value_valid = true;
        }
      }
    }
  }

  lv_obj_move_foreground(ledpanel_cont);
}

void LedPanel::background() {
  lv_obj_move_background(ledpanel_cont);
}

const void *LedPanel::get_main_button_image() {
  if (single_led_id.empty()) {
    return Icons::LIGHT_IMG;
  }

  const double current_value = single_led_last_value_valid
                                 ? single_led_last_value
                                 : get_led_value(single_led_id);
  return current_value > 0.0 ? Icons::LIGHT_IMG : Icons::LIGHT_OFF;
}

double LedPanel::get_led_value(const std::string &led_id) {
  auto value = State::get_instance()->get_data(
    json::json_pointer(fmt::format("/printer_state/{}/value", led_id)));
  if (!value.is_null() && value.is_number()) {
    return value.template get<double>();
  }

  value = State::get_instance()->get_data(
    json::json_pointer(fmt::format("/printer_state/{}/color_data", led_id)));
  if (!value.is_null() && value.is_array() && !value.empty()) {
    value = value.at(0);
    if (value.is_array() && value.size() >= 4 && value.at(3).is_number()) {
      return value.at(3).template get<double>();
    }
  }

  return 0.0;
}

void LedPanel::toggle_single_led() {
  const auto current_value = get_led_value(single_led_id);
  const bool turn_on = current_value <= 0.0;
  const double target_value = turn_on ? 1.0 : 0.0;
  const std::string led_name = KUtils::get_obj_name(single_led_id);

  if (single_led_is_output_pin) {
    ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE={}", led_name, target_value));
  } else {
    ws.gcode_script(fmt::format("SET_LED LED={} WHITE={}", led_name, target_value));
  }
  single_led_last_value = target_value;
  single_led_last_value_valid = true;
}

void LedPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    background();
  } else if (btn == all_off_btn.get_container() || btn == all_on_btn.get_container()) {
    const bool on = btn == all_on_btn.get_container();
    for (auto &l : leds) {
      ws.gcode_script(led_gcode(l.first, on ? 1.0 : 0.0));
      l.second->update_value(on ? 100 : 0);
    }
  }
  else {
    LOG_DEBUG("Unknown action button pressed");
  }
}

std::string LedPanel::led_gcode(const std::string &key, double fraction) {
  const std::string name = KUtils::get_obj_name(key);
  if (key.rfind("output_pin ", 0) == 0) {
    return fmt::format("SET_PIN PIN={} VALUE={}", name, fraction);
  }
  return fmt::format("SET_LED LED={} WHITE={}", name, fraction);
}

void LedPanel::handle_led_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating led brightness to {}", pct);

    for (auto &l : leds) {
      if (obj == l.second->get_slider()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("update led {}", led_name);
      	// TODO - I think this double fmt:format is intentional
        ws.gcode_script(fmt::format(fmt::format("SET_PIN PIN={} VALUE={}", led_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &l : leds) {
      if (obj == l.second->get_off()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("turning off led {}", led_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", led_name));
        l.second->update_value(0);
        break;
      } else if (obj == l.second->get_max()) {
        std::string led_name = KUtils::get_obj_name(l.first);
        LOG_DEBUG("turning led to max {}", led_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=1", led_name));
        l.second->update_value(100);
        break;
      }
    }
  }
}

void LedPanel::handle_led_update_generic(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating led brightness to {}", pct);

    for (auto &l : leds) {
      if (obj == l.second->get_slider()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("update led {}", led_name);
      	// TODO - I think this double fmt:format is intentional
        ws.gcode_script(fmt::format(fmt::format("SET_LED LED={} WHITE={}", led_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &l : leds) {
      if (obj == l.second->get_off()) {
	      std::string led_name = KUtils::get_obj_name(l.first);
      	LOG_DEBUG("turning off led {}", led_name);
        ws.gcode_script(fmt::format("SET_LED LED={} WHITE=0", led_name));
        l.second->update_value(0);
        break;
      } else if (obj == l.second->get_max()) {
        std::string led_name = KUtils::get_obj_name(l.first);
        LOG_DEBUG("turning led to max {}", led_name);
        ws.gcode_script(fmt::format("SET_LED LED={} WHITE=1", led_name));
        l.second->update_value(100);
        break;
      }
    }
  }
}
