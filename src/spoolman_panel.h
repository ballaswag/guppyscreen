#ifndef __SPOOLMAN_PANEL_H__
#define __SPOOLMAN_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <map>
#include <mutex>

class SpoolmanPanel {
 public:
  SpoolmanPanel(KWebSocketClient &c, std::mutex &l);
  ~SpoolmanPanel();

  void init();
  void foreground();
  void populate_spools(std::vector<json> &sorted_spools);
  void handle_active_id_update(json &j);
  
  void handle_callback(lv_event_t *event);
  void handle_spoolman_action(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SpoolmanPanel *panel = (SpoolmanPanel*)event->user_data;
    panel->handle_callback(event);
  };
  
  static void _handle_spoolman_action(lv_event_t *event) {
    SpoolmanPanel *panel = (SpoolmanPanel*)event->user_data;
    panel->handle_spoolman_action(event);
  };

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *spool_table;
  lv_obj_t *controls;
  lv_obj_t *switch_cont;
  lv_obj_t *show_archived;
  ButtonContainer reload_btn;
  ButtonContainer back_btn;
  int32_t active_id;
  std::map<uint32_t, json> spools;
  uint32_t sorted_by;
};

#endif // __SPOOLMAN_PANEL_H__
