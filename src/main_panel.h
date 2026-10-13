#ifndef __MAIN_PANEL_H__
#define __MAIN_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "sensor_container.h"
#include "button_container.h"
#include "prompt_panel.h"
#include "numpad.h"
#include "homing_panel.h"
#include "extruder_panel.h"
#include "fan_panel.h"
#include "led_panel.h"
#include "print_panel.h"
#include "console_panel.h"
#include "setting_panel.h"
#include "print_status_panel.h"
#include "spoolman_panel.h"
#include "mmu_panel.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <map>
#include <memory>

class MainPanel : public NotifyConsumer {
 public:
  MainPanel(KWebSocketClient &ws,
	    std::mutex &lv_lock,
	    SpoolmanPanel &sm,
	    MmuPanel *mmu);

  ~MainPanel();
  void consume(json &data);
  void init(json &data);
  void subscribe();
  void enable_spoolman();
  void enable_mmu();
  void disable_mmu();
  void set_firmware_restart_available(bool available);
  void set_klipper_ready(bool ready);
  MmuPanel *mmu() { return mmu_panel; }
  bool home_active() const;
  void add_tab_change_cb(lv_event_cb_t cb, void *user_data);
  void remove_tab_change_cb(lv_event_cb_t cb, void *user_data);
  
  void create_panel();
  void create_sensors(json &temp_sensors);
  void create_fans(json &temp_fans);
  void create_leds(json &leds);
  void handle_homing_cb(lv_event_t *event);
  void handle_extrude_cb(lv_event_t *event);
  void handle_fanpanel_cb(lv_event_t *event);
  void handle_ledpanel_cb(lv_event_t *event);
  void handle_print_cb(lv_event_t *event);
  void handle_emergency_cb(lv_event_t *event);

  lv_obj_t *create_button(lv_obj_t *parent,
			  const void *btn_img,
			  const char* text,
			  lv_event_cb_t cb);

  lv_obj_t *create_heater_info(lv_obj_t *parent,
			       const void *heater_img,
			       const char* text,
			       lv_color_t color);
  
  static void _handle_homing_cb(lv_event_t *event) {
    MainPanel *panel = (MainPanel*)event->user_data;
    panel->handle_homing_cb(event);
  };

  static void _handle_extrude_cb(lv_event_t *event) {
    MainPanel *panel = (MainPanel*)event->user_data;
    panel->handle_extrude_cb(event);
  };

  static void _handle_fanpanel_cb(lv_event_t *event) {
    MainPanel *panel = (MainPanel*)event->user_data;
    panel->handle_fanpanel_cb(event);
  };

  static void _handle_ledpanel_cb(lv_event_t *event) {
    MainPanel *panel = (MainPanel*)event->user_data;
    panel->handle_ledpanel_cb(event);
  };

  static void _handle_print_cb(lv_event_t *event) {
    MainPanel *panel = (MainPanel*)event->user_data;
    panel->handle_print_cb(event);
  };

  static void _handle_emergency_cb(lv_event_t *event) {
      MainPanel *panel = (MainPanel*)event->user_data;
      panel->handle_emergency_cb(event);
    };

 private:
  void create_main(lv_obj_t *parent);
  static void _tabview_event_cb(lv_event_t *e);
  KWebSocketClient &ws;
  HomingPanel homing_panel;
  FanPanel fan_panel;
  LedPanel led_panel;
  lv_obj_t *tabview;
  lv_obj_t *main_tab;
  // NULL when /mmu/backend is none; otherwise the tab exists from startup and
  // its button stays disabled until the backend is detected
  lv_obj_t *mmu_tab;
  lv_obj_t *console_tab;
  ConsolePanel console_panel;
  lv_obj_t *setting_tab;
  SettingPanel setting_panel;
  lv_obj_t *main_cont;
  PrintStatusPanel print_status_panel;
  PrintPanel print_panel;
  Numpad numpad;
  ExtruderPanel extruder_panel;
  PromptPanel prompt_panel;
  SpoolmanPanel &spoolman_panel;
  MmuPanel *mmu_panel;

  lv_obj_t *temp_cont;
  // lv_chart draws primary-Y tick labels outside its own left edge, so the
  // chart lives inside this box and the labels land in the box's padding
  lv_obj_t *temp_chart_box;
  lv_obj_t *temp_chart;

  std::map<std::string, std::shared_ptr<SensorContainer>> sensors;
  
  ButtonContainer homing_btn;
  ButtonContainer extrude_btn;
  ButtonContainer action_btn;
  ButtonContainer led_btn;
  ButtonContainer print_btn;
  ButtonContainer emergency_btn;
  bool klipper_ready = false;
};
#endif // __MAIN_PANEL_H__
