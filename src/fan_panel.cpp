#include "fan_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"
#include "icons.h"

using namespace Theme;

FanPanel::FanPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , fanpanel_cont(create_screen(NULL))
  , fans_cont(create_row(fanpanel_cont))
  , side_cont(create_row(fanpanel_cont))
  , all_on_btn(side_cont, Icons::FAN_ON, "All On", &FanPanel::_handle_callback, this)
  , all_off_btn(side_cont, Icons::FAN_OFF_IMG, "All Off", &FanPanel::_handle_callback, this)
  , back_btn(side_cont, Icons::BACK, "Back", &FanPanel::_handle_callback, this)
{
  // the list takes the width, the action tiles their own column
  lv_obj_set_flex_flow(fanpanel_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(fans_cont, 0, lv_pct(100));
  lv_obj_set_flex_grow(fans_cont, 1);
  lv_obj_set_flex_flow(fans_cont, LV_FLEX_FLOW_COLUMN);
  manage_scroll(fans_cont);
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

FanPanel::~FanPanel() {
  if (fanpanel_cont != NULL) {
    lv_obj_del(fanpanel_cont);
    fanpanel_cont = NULL;
  }

  fans.clear();

  ws.unregister_notify_update(this);
}

void FanPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (auto &f : fans) {
    // hack for output_pin fans
    auto fan_value = j[json::json_pointer(fmt::format("/params/0/{}/value", f.first))];
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }

    fan_value = j[json::json_pointer(fmt::format("/params/0/{}/speed", f.first))];
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }
  }
}

void FanPanel::create_fans(json &f) {
  std::lock_guard<std::mutex> lock(lv_lock);
  fans.clear();

  for (auto &fan : f.items()) {
    std::string key = fan.key();
    if (get_log_level() <= LogLevel::TRACE) {
      LOG_TRACE("create fan {}, {}", f.dump(), fan.value().dump());
    }
    std::string display_name = fan.value()["display_name"].template get<std::string>();

    lv_event_cb_t fan_cb = &FanPanel::_handle_fan_update;
    if (key == "fan") {
      fan_cb = &FanPanel::_handle_fan_update_part_fan;
    } else if (key.rfind("output_pin ", 0) != 0) {
      // generic_fan, controller_fan, etc.
      fan_cb = &FanPanel::_handle_fan_update_generic;
    }
    // a row is as tall as its controls: rows start at the top, one gap apart
    fans.insert({key, std::make_shared<SliderContainer>(fans_cont, display_name.c_str(), Icons::CANCEL, "Off",
						  Icons::FAN_ON, "Max", fan_cb, this, "%")});
  }

  // Any number of rows, any screen size: build them, then let the scroller
  // measure whether they overflow; no row height or count is guessed anywhere
  const lv_coord_t row_h = SliderContainer::row_height(fans.size());
  for (auto &r : fans) {
    r.second->set_height(row_h);
  }
  refresh_scroll(fans_cont);
}

void FanPanel::foreground() {
  for (auto &f : fans) {
    // hack for output_pin fans
    auto fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", f.first)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }

    fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/speed", f.first)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }
  }
  
  lv_obj_move_foreground(fanpanel_cont);
}

void FanPanel::background() {
  lv_obj_move_background(fanpanel_cont);
}

void FanPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    background();
  } else if (btn == all_off_btn.get_container() || btn == all_on_btn.get_container()) {
    const bool on = btn == all_on_btn.get_container();
    for (auto &f : fans) {
      ws.gcode_script(fan_gcode(f.first, on ? 1.0 : 0.0));
      f.second->update_value(on ? 100 : 0);
    }
  }
  else {
    LOG_DEBUG("Unknown action button pressed");
  }
}

std::string FanPanel::fan_gcode(const std::string &key, double fraction) {
  const std::string name = KUtils::get_obj_name(key);
  if (key == "fan") {
    return fmt::format("M106 S{}", fraction * 255);
  }
  if (key.rfind("output_pin ", 0) == 0) {
    return fmt::format("SET_PIN PIN={} VALUE={}", name, fraction * 255);
  }
  return fmt::format("SET_FAN_SPEED FAN={} SPEED={}", name, fraction);
}

void FanPanel::handle_fan_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
	      std::string fan_name = KUtils::get_obj_name(f.first);
      	LOG_DEBUG("update fan {}", fan_name);
        // TODO - I think this double fmt:format is intentional
	      ws.gcode_script(fmt::format(fmt::format("SET_PIN PIN={} VALUE={}", fan_name, pct)));
	      break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
	      std::string fan_name = KUtils::get_obj_name(f.first);
      	LOG_DEBUG("turning off fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", fan_name));
        f.second->update_value(0);
	      break;
      } else if (obj == f.second->get_max()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        LOG_DEBUG("turning fan to max {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=255", fan_name));
        f.second->update_value(100);
        break;
      }
    }
  }
}

void FanPanel::handle_fan_update_part_fan(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating part fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        ws.gcode_script(fmt::format(fmt::format("M106 S{}", pct)));
        break;
      }
    }

  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);
    
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
      	LOG_DEBUG("turning off part fan");
        ws.gcode_script("M106 S0");
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
      	LOG_DEBUG("turning part fan to max");
        ws.gcode_script("M106 S255");
        f.second->update_value(100);
        break;
      }
    }
  }
}

void FanPanel::handle_fan_update_generic(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    LOG_DEBUG("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
	      std::string fan_name = KUtils::get_obj_name(f.first);
      	LOG_DEBUG("update fan {}", fan_name);
      	// TODO - I think this double fmt:format is intentional
        ws.gcode_script(fmt::format(fmt::format("SET_FAN_SPEED FAN={} SPEED={}", fan_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
	      std::string fan_name = KUtils::get_obj_name(f.first);
      	LOG_DEBUG("turning off fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=0", fan_name));
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        LOG_DEBUG("turning fan to max {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=1", fan_name));
        f.second->update_value(100);
        break;
      }
    }
  }
}
