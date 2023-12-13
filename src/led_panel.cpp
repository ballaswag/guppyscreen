#include "led_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(cancel);
LV_IMG_DECLARE(light_img);
LV_IMG_DECLARE(back);

LedPanel::LedPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , ledpanel_cont(lv_obj_create(lv_scr_act()))
  , leds_cont(lv_obj_create(ledpanel_cont))
  , back_btn(ledpanel_cont, &back, "Back", &LedPanel::_handle_callback, this)
{
    lv_obj_clear_flag(ledpanel_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ledpanel_cont, lv_pct(100), lv_pct(100));

    lv_obj_set_style_pad_all(ledpanel_cont, 0, 0);
    
    lv_obj_center(leds_cont);
    lv_obj_set_size(leds_cont, lv_pct(80), lv_pct(100));
    lv_obj_set_flex_flow(leds_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leds_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

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
      int v = static_cast<int>(value.template get<double>() * 100);
      l.second->update_value(v);
    }

    value = j[json::json_pointer(fmt::format("/params/0/{}/color_data", l.first))];
    if (!value.is_null() && value.size() > 0) {
      // color_data = [[r,b,g,w]]
      value =  value.at(0);
      if (value.size() == 4) {
	int v = static_cast<int>(value.at(3).template get<double>() * 100);
	l.second->update_value(v);
      }
    }
  }
}

void LedPanel::init(json &l) {
  std::lock_guard<std::mutex> lock(lv_lock);
  leds.clear();
  for (auto &led : l.items()) {
    std::string key = led.key();
    spdlog::trace("create led {}, {}", l.dump(), led.value().dump());
    std::string display_name = led.value()["display_name"].template get<std::string>();

    lv_event_cb_t led_cb = &LedPanel::_handle_led_update;
    if (key.rfind("output_pin ", 0) != 0) {
	// standard led
	led_cb = &LedPanel::_handle_led_update_generic;
    }

    auto lptr = std::make_shared<SliderContainer>(leds_cont, display_name.c_str(), &cancel, "Off",
						  &light_img, "Max", led_cb, this);
    leds.insert({key, lptr});
  }

  if (leds.size() > 3) {
    lv_obj_add_flag(leds_cont, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(leds_cont, LV_OBJ_FLAG_SCROLLABLE);    
  }

  lv_obj_move_foreground(back_btn.get_container());
}

void LedPanel::foreground() {
  for (auto &l : leds) {
    // hack for output_pin leds
    auto led_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", l.first)));
    if (!led_value.is_null()) {
      int v = static_cast<int>(led_value.template get<double>() * 100);
      l.second->update_value(v);
    }
    
    led_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/color_data", l.first)));
    if (!led_value.is_null() && led_value.size() > 0) {
      // color_data = [[r,b,g,w]]
      led_value = led_value.at(0);
      if (led_value.size() == 4) {
	int v = static_cast<int>(led_value.at(3).template get<double>() * 100);
	l.second->update_value(v);
      }
    }
  }

  lv_obj_move_foreground(ledpanel_cont);
}

void LedPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_target(event);
  if (btn == back_btn.get_button()) {
    lv_obj_move_background(ledpanel_cont);
  }
  else {
    spdlog::debug("Unknown action button pressed");
  }
}

void LedPanel::handle_led_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating led brightness to {}", pct);

    for (auto &l : leds) {
      if (obj == l.second->get_slider()) {
	std::string led_name = KUtils::get_obj_name(l.first);
      	spdlog::trace("update led {}", led_name);
	ws.gcode_script(fmt::format(fmt::format("SET_PIN PIN={} VALUE={}", led_name, pct)));
	break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {

    for (auto &l : leds) {
      if (obj == l.second->get_off()) {
	std::string led_name = KUtils::get_obj_name(l.first);
      	spdlog::trace("turning off led {}", led_name);
	ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", led_name));
	l.second->update_value(0);
	break;
      } else if (obj == l.second->get_max()) {
	std::string led_name = KUtils::get_obj_name(l.first);
	spdlog::trace("turning led to max {}", led_name);
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

    spdlog::trace("updating led brightness to {}", pct);

    for (auto &l : leds) {
      if (obj == l.second->get_slider()) {
	std::string led_name = KUtils::get_obj_name(l.first);
      	spdlog::trace("update led {}", led_name);
	ws.gcode_script(fmt::format(fmt::format("SET_LED LED={} WHITE={}", led_name, pct)));
	break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {

    for (auto &l : leds) {
      if (obj == l.second->get_off()) {
	std::string led_name = KUtils::get_obj_name(l.first);
      	spdlog::trace("turning off led {}", led_name);
	ws.gcode_script(fmt::format("SET_LED LED={} WHITE=0", led_name));
	l.second->update_value(0);
	break;
      } else if (obj == l.second->get_max()) {
	std::string led_name = KUtils::get_obj_name(l.first);
	spdlog::trace("turning led to max {}", led_name);
	ws.gcode_script(fmt::format("SET_LED LED={} WHITE=1", led_name));
	l.second->update_value(100);
	break;
      }
    }
  }
}
