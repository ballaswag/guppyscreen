#ifndef __SETTING_PANEL_H__
#define __SETTING_PANEL_H__

#include "platform.h"
#include "button_container.h"
#ifndef OS_ANDROID
#include "wifi_panel.h"
#endif
#include "websocket_client.h"
#ifdef COSMOS
#include "update_manager_client.h"
#endif
#include "lvgl/lvgl.h"

#include <mutex>

class SettingPanel {
 public:
  SettingPanel(KWebSocketClient &c, std::mutex &, lv_obj_t *parent);
  ~SettingPanel();

  lv_obj_t *get_container();
  void foreground();
  void select_first_tab();
  // Firmware restart is a Klipper RPC, so it only works while Moonraker is
  // connected to Klipper.
  void set_firmware_restart_available(bool available);

  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SettingPanel *panel = (SettingPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  static void _tabview_event_cb(lv_event_t *event);
  void refresh_active_tab();
  void refresh_total_print_time();
#ifndef OS_ANDROID
  static void _setting_changed_cb(lv_event_t *event);
  void setting_changed(lv_event_t *event);
  void refresh_emergency_stop_warning();
  void refresh_sleep_timeout_warning();
#endif

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  bool owns_cont;
  lv_obj_t *cont;
  lv_obj_t *tabview;
  lv_obj_t *actions_tab;
#ifndef OS_ANDROID
  lv_obj_t *wifi_tab;  // Android leaves the device's wifi to Android
  lv_obj_t *settings_tab;
#endif
  lv_obj_t *about_tab;
  lv_obj_t *actions_cont;
#ifndef OS_ANDROID
  lv_obj_t *settings_cont;
#endif
  lv_obj_t *about_cont;
#ifndef OS_ANDROID
  lv_obj_t *emergency_prompt_switch;
  lv_obj_t *emergency_stop_warning;
  lv_obj_t *sleep_timeout_selector;
  lv_obj_t *sleep_timeout_warning;
#endif
#ifdef COSMOS
  UpdateManagerClient update_manager;
#endif

#ifndef OS_ANDROID
  WifiPanel wifi_panel;
#endif

  ButtonContainer restart_klipper_btn;
  ButtonContainer restart_firmware_btn;
#ifndef OS_ANDROID
  ButtonContainer guppy_restart_btn;
  ButtonContainer support_zip_btn;
  ButtonContainer switch_to_stock_btn;
  ButtonContainer factory_reset_btn;
#endif
#ifdef COSMOS
  ButtonContainer update_btn;
#else
  ButtonContainer shutdown_host_btn;
#endif

  lv_obj_t *total_print_time_label;  // About tab
};

#endif // __SETTING_PANEL_H__
