#include "power_panel.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <map>

LV_IMG_DECLARE(back);

PowerPanel::PowerPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : ws(websocket_client)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , back_btn(cont, &back, "Back", &PowerPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);	
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

PowerPanel::~PowerPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }

  devices.clear();
}

void PowerPanel::create_device(json &j) {
  std::string name = j["device"].template get<std::string>();

  lv_obj_t *power_device_toggle;
  auto entry = devices.find(name);
  if (entry == devices.end()) {
    lv_obj_t *power_device_toggle_cont = lv_obj_create(cont);
    lv_obj_set_size(power_device_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(power_device_toggle_cont, 0, 0);

    lv_obj_t *l = lv_label_create(power_device_toggle_cont);
    lv_label_set_text(l, name.c_str());
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    power_device_toggle = lv_switch_create(power_device_toggle_cont); 
    lv_obj_align(power_device_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(power_device_toggle, &PowerPanel::_handle_callback,
	        LV_EVENT_VALUE_CHANGED, this);

    devices.insert({name, power_device_toggle});
  } else {
    power_device_toggle = entry->second;
  }
  
  std::string status = j["status"].template get<std::string>();
  spdlog::debug("Fetched initial status for power device {}: {}", name, status);
  
  if (status == "on") {
    lv_obj_add_state(power_device_toggle, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(power_device_toggle, LV_STATE_CHECKED);
  }
}

void PowerPanel::create_devices(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  if (j.contains("result")) {
    json result = j["result"];
    if (result.contains("devices")) {
      json devices = result["devices"];
      for (auto &device : devices.items()) {
        create_device(device.value());
      }
    }
  }
}

void PowerPanel::handle_device_callback(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  if (j.contains("result")) {
    json result = j["result"];
    for (auto &device : result.items()) {
      auto entry = devices.find(device.key());
      if (entry != devices.end()) {
        std::string new_status = device.value();

        spdlog::debug("Fetched new status for power device {}: {}", entry->first, new_status);

        if (new_status == "on") {
          lv_obj_add_state(entry->second, LV_STATE_CHECKED);
        } else {
          lv_obj_clear_state(entry->second, LV_STATE_CHECKED);
        }
      }
    }
  }
}

void PowerPanel::foreground() {
  lv_obj_move_foreground(cont);

  json params;
  for (auto &device : devices) {
    params[device.first] = 0; // value is ignored by moonraker
  }
  ws.send_jsonrpc("machine.device_power.status", params, [this](json& j) {
    this->handle_device_callback(j);
  });
}

void PowerPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);
    if (btn == back_btn.get_container()) {
      lv_obj_move_background(cont);
    }
  } else if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *obj = lv_event_get_target(e);
    for (auto &device : devices) {
      if (obj == device.second) {
        bool turnOn = lv_obj_has_state(device.second, LV_STATE_CHECKED);
        std::string status = turnOn ? "on" : "off";

        spdlog::debug("Turning power device {} {}", device.first, status);

        json params;
        params["device"] = device.first;
        params["action"] = status;
        ws.send_jsonrpc("machine.device_power.post_device", params, [this](json& j) {
          this->handle_device_callback(j);
        });
	      break;
      }
    }
  }
}
