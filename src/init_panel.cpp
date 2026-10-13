#include "init_panel.h"
#include "utils.h"
#include "state.h"
#include "config.h"
#include "mmu_panel.h"
#include "logger.h"
#include "theme.h"

using namespace Theme;

#include <algorithm>
#include <cstdio>

InitPanel::InitPanel(MainPanel &mp, std::mutex& l)
  : cont(lv_obj_create(lv_scr_act()))
  , label(lv_label_create(cont))
  , main_panel(mp)
  , lv_lock(l)
{
  // a centred notice card; the label wraps inside it if a message runs long
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(cont, &styles().panel, 0);
  // the system-is-not-ready notice, so it wears the not-ready colour
  lv_obj_set_style_bg_color(cont, col(DISABLED), 0);
  lv_obj_set_size(cont, scale_w(264), LV_SIZE_CONTENT);
  lv_obj_center(cont);

  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_text(label, LV_SYMBOL_WARNING " Waiting for Moonraker to start...");
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

  main_panel.add_tab_change_cb(&InitPanel::_tabview_event_cb, this);
  update_visibility();
}

InitPanel::~InitPanel() {
  main_panel.remove_tab_change_cb(&InitPanel::_tabview_event_cb, this);
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void InitPanel::websocket_connected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel websocket connected");
  begin_waiting(LV_SYMBOL_WARNING " Waiting for Klipper to start...", false);
  start_initialization(ws);
}

void InitPanel::klippy_ready(KWebSocketClient &ws) {
  LOG_DEBUG("init panel klippy ready");
  main_panel.set_firmware_restart_available(true);
  start_initialization(ws);
}

void InitPanel::start_initialization(KWebSocketClient &ws) {
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (lifecycle != Lifecycle::WaitingForKlipper) {
      LOG_DEBUG("ignoring Klipper startup event while {}",
                lifecycle == Lifecycle::Initializing ? "initializing" : "ready");
      return;
    }
    lifecycle = Lifecycle::Initializing;
    generation = ++lifecycle_generation;
  }

  State *state = State::get_instance();
  state->reset();

  // Moonraker can accept object-list requests while Klipper is still booting.
  // Do not let that partial state build panels (or select an MMU backend): the
  // ready notification will start a fresh attempt when Klipper is usable.
  ws.send_jsonrpc("printer.info", [this, &ws, generation](json& info) {
    if (!initialization_active(generation)) return;
    State::get_instance()->set_data("printer_info", info, "/result");
    const std::string klippy_state = info.value("/result/state"_json_pointer, std::string());
    const bool klipper_connected = !klippy_state.empty() && klippy_state != "disconnected";
    main_panel.set_firmware_restart_available(klipper_connected);
    if (klippy_state != "ready") {
      LOG_DEBUG("Klipper is not ready (state: {})", klippy_state.empty() ? "unknown" : klippy_state);
      begin_waiting(klipper_connected
                        ? LV_SYMBOL_WARNING " Waiting for Klipper to be ready..."
                        : LV_SYMBOL_WARNING " Waiting for Klipper to start...",
                    klipper_connected);
      wait_for_klipper(generation);
      return;
    }

    ws.send_jsonrpc("printer.objects.list", [this, &ws, generation](json& d) {
      if (!initialization_active(generation)) return;
    State *state = State::get_instance();
	  state->set_data("printer_objs", d, "/result");

	  ws.send_jsonrpc("server.files.roots",
			[this, generation](json& j) {
          if (initialization_active(generation)) State::get_instance()->set_data("roots", j, "/result");
        });

    this->main_panel.subscribe();

    // spoolman
    ws.send_jsonrpc("server.info", [this, generation](json &j) {
      if (!initialization_active(generation)) return;
      if (get_log_level() <= LogLevel::DEBUG) {
        LOG_DEBUG("server_info {}", j.dump());
      }
      State::get_instance()->set_data("server_info", j, "/result");

      const auto components_ptr = "/result/components"_json_pointer;
      if (j.contains(components_ptr) && !j[components_ptr].is_null()) {
        auto &components = j[components_ptr];
        const auto &has_spoolman = components.template get<std::vector<std::string>>();
        if (std::find(has_spoolman.begin(), has_spoolman.end(), "spoolman") != has_spoolman.end()) {
          this->main_panel.enable_spoolman();
        }
      }
    });

    auto display_sensors = state->get_display_sensors();
    this->main_panel.create_sensors(display_sensors);

    auto display_fans = state->get_display_fans();
    this->main_panel.create_fans(display_fans);

    auto display_leds = state->get_display_leds();
    this->main_panel.create_leds(display_leds);

    // subscribe to all objects except gcode_macro
    const auto objects_ptr = "/result/objects"_json_pointer;
    if (d.contains(objects_ptr) && !d[objects_ptr].is_null()) {
      auto objs = d[objects_ptr];
      json sub_objs;
      for (auto &obj : objs) {
        std::string obj_name = obj.template get<std::string>();
        if (obj_name.rfind("gcode_macro ", 0 ) != 0) {
          sub_objs[obj_name] = nullptr;
        }
      }

      // runs again on every klipper reconnect, so the tab has to follow the
      // backend back down as well as up
      MmuPanel *mmu = this->main_panel.mmu();
      if (mmu != NULL && mmu->select_backend() != NULL) {
        this->main_panel.enable_mmu();
      } else {
        this->main_panel.disable_mmu();
      }

      json subs = {{ "objects", sub_objs }};
      if (get_log_level() <= LogLevel::DEBUG) {
        LOG_DEBUG("subscribing to {}", subs.dump());
      }
      ws.send_jsonrpc("printer.objects.subscribe", subs, [this, generation](json &data) {
        if (!initialization_active(generation)) return;
        State::get_instance()->set_data("printer_state", data, "/result/status");
        this->main_panel.init(data);
        LOG_DEBUG("done init");
        initialization_complete(generation);
      });
    } else {
      LOG_DEBUG("printer.objects.list returned no objects");
      wait_for_klipper(generation);
    }
    });
  });
}

bool InitPanel::initialization_active(uint64_t generation) {
  std::lock_guard<std::mutex> lock(lv_lock);
  return lifecycle == Lifecycle::Initializing && lifecycle_generation == generation;
}

void InitPanel::wait_for_klipper(uint64_t generation) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (lifecycle == Lifecycle::Initializing && lifecycle_generation == generation) {
    lifecycle = Lifecycle::WaitingForKlipper;
  }
}

void InitPanel::initialization_complete(uint64_t generation) {
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (lifecycle != Lifecycle::Initializing || lifecycle_generation != generation) return;
    lifecycle = Lifecycle::Ready;
    waiting = false;
    update_visibility();
  }
  main_panel.set_klipper_ready(true);
}

void InitPanel::klippy_disconnected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel Klipper disconnected");
  begin_waiting(LV_SYMBOL_WARNING " Waiting for Klipper to start...", false);
}

void InitPanel::klippy_shutdown(KWebSocketClient &ws) {
  LOG_DEBUG("init panel Klipper shutdown");
  begin_waiting(LV_SYMBOL_WARNING " Waiting for Klipper to be ready...", true);
}

void InitPanel::websocket_disconnected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel Moonraker disconnected");
  waiting_for_moonraker();
}

void InitPanel::waiting_for_moonraker() {
  begin_waiting(LV_SYMBOL_WARNING " Waiting for Moonraker to start...", false);
}

void InitPanel::begin_waiting(const char *message, bool firmware_restart_available) {
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    ++lifecycle_generation;
    lifecycle = Lifecycle::WaitingForKlipper;
    set_message(message);
    waiting = true;
    update_visibility();
  }
  main_panel.set_klipper_ready(false);
  main_panel.set_firmware_restart_available(firmware_restart_available);
}

void InitPanel::set_message(const char *message) {
	lv_label_set_text(label, message);
}

void InitPanel::update_visibility() {
  if (waiting && main_panel.home_active()) {
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cont);
  } else {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

void InitPanel::_tabview_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  static_cast<InitPanel *>(lv_event_get_user_data(event))->update_visibility();
}
