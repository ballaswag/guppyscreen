#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "selector.h"
#include "sensor_container.h"
#include "numpad.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include <string>
#include <vector>

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np);
  ~ExtruderPanel();

  void foreground();
  void background();
  lv_obj_t *get_container() { return panel_cont; }
  void consume(json &j);
  void handle_callback(lv_event_t *e);
#ifndef COSMOS
  void handle_extruder_select(lv_event_t *e);
#endif

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

#ifndef COSMOS
  static void _handle_extruder_select(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_extruder_select(event);
  };
#endif

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  std::vector<std::string> temp_options;
  std::vector<const char*> temp_option_map;
  uint32_t temp_default_idx;
  std::vector<std::string> length_options;
  std::vector<const char*> length_option_map;
  uint32_t length_default_idx;
  std::vector<std::string> speed_options;
  std::vector<const char*> speed_option_map;
  uint32_t speed_default_idx;

  // Each tab includes its own readout and filament controls.
  lv_obj_t *tabview;
  lv_obj_t *load_tab;
  lv_obj_t *extrude_tab;
#ifndef COSMOS
  // each readout shares its row with an extruder picker, shown only when
  // there is more than one
  lv_obj_t *load_header;
  lv_obj_t *extrude_header;
#endif
  SensorContainer load_extruder_temp;
  SensorContainer extrude_extruder_temp;
  std::string active_extruder = "extruder";
#ifndef COSMOS
  lv_obj_t *load_extruder_dd;
  lv_obj_t *extrude_extruder_dd;
  std::vector<std::string> extruders;

  void refresh_extruders();
  void set_active_extruder(const std::string &name);
#endif
  lv_obj_t *load_cont;
  lv_obj_t *extrude_cont;
  lv_obj_t *extrude_col;  // the Extrude / Retract tiles, stacked

  Selector load_temp_selector;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;

  Selector speed_selector;
  Selector length_selector;
  Selector extrude_temp_selector;
  ButtonContainer extrude_btn;
  ButtonContainer retract_btn;

  lv_obj_t *load_right_col;
  lv_obj_t *extrude_right_col;
  ButtonContainer load_cooldown_btn;
  ButtonContainer extrude_cooldown_btn;
};

#endif // __EXTRUDER_PANEL_H__
