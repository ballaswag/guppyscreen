#include "fan_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(cancel);
LV_IMG_DECLARE(fan_on);
LV_IMG_DECLARE(back);

FanPanel::FanPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , fanpanel_cont(lv_obj_create(lv_scr_act()))
  , fans_cont(lv_obj_create(fanpanel_cont))
  , back_btn(fanpanel_cont, &back, "Back", &FanPanel::_handle_callback, this)
{
  lv_obj_set_style_pad_all(fanpanel_cont, 0, 0);
  
  lv_obj_clear_flag(fanpanel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(fanpanel_cont, LV_PCT(100), LV_PCT(100));

  lv_obj_center(fans_cont);
  lv_obj_set_size(fans_cont, lv_pct(80), lv_pct(100));
  lv_obj_set_flex_flow(fans_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);
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
    spdlog::trace("create fan {}, {}", f.dump(), fan.value().dump());
    std::string display_name = fan.value()["display_name"].template get<std::string>();

    lv_event_cb_t fan_cb = &FanPanel::_handle_fan_update;
    if (key == "fan") {
      fan_cb = &FanPanel::_handle_fan_update_part_fan;
    } else if (key.rfind("output_pin ", 0) != 0) {
      // generic_fan, controller_fan, etc.
      fan_cb = &FanPanel::_handle_fan_update_generic;
    }
    auto fptr = std::make_shared<SliderContainer>(fans_cont, display_name.c_str(), &cancel, "Off",
						  &fan_on, "Max", fan_cb, this);
    fans.insert({key, fptr});
    // lv_obj_set_grid_cell(fptr->get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, rowidx++, 1);
  }

  if (fans.size() > 3) {
    lv_obj_add_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);    
  }

  lv_obj_move_foreground(back_btn.get_container());
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
  
  lv_obj_move_foreground(back_btn.get_container());
  lv_obj_move_foreground(fanpanel_cont);
}

void FanPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_target(event);
  if (btn == back_btn.get_button()) {
    lv_obj_move_background(fanpanel_cont);
  }
  else {
    spdlog::debug("Unknown action button pressed");
  }
}

void FanPanel::handle_fan_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
	std::string fan_name = KUtils::get_obj_name(f.first);
      	spdlog::trace("update fan {}", fan_name);
	ws.gcode_script(fmt::format(fmt::format("SET_PIN PIN={} VALUE={}", fan_name, pct)));
	break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
	std::string fan_name = KUtils::get_obj_name(f.first);
      	spdlog::trace("turning off fan {}", fan_name);
	ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", fan_name));
	f.second->update_value(0);
	break;
      } else if (obj == f.second->get_max()) {
	std::string fan_name = KUtils::get_obj_name(f.first);
	spdlog::trace("turning fan to max {}", fan_name);
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

    spdlog::trace("updating part fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
	ws.gcode_script(fmt::format(fmt::format("M106 S{}", pct)));
	break;
      }
    }

  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
	ws.gcode_script("M106 S0");
	f.second->update_value(0);
	break;
      } else if (obj == f.second->get_max()) {
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

    spdlog::trace("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
	std::string fan_name = KUtils::get_obj_name(f.first);
      	spdlog::trace("update fan {}", fan_name);
	ws.gcode_script(fmt::format(fmt::format("SET_FAN_SPEED FAN={} SPEED={}", fan_name, pct)));
	break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
	std::string fan_name = KUtils::get_obj_name(f.first);
      	spdlog::trace("turning off fan {}", fan_name);
	ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=0", fan_name));
	f.second->update_value(0);
	break;
      } else if (obj == f.second->get_max()) {
	std::string fan_name = KUtils::get_obj_name(f.first);
	spdlog::trace("turning fan to max {}", fan_name);
	ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=1", fan_name));
	f.second->update_value(100);
	break;
      }
    }
  }
}
