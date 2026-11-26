#ifndef __SPOOLMAN_PANEL_H__
#define __SPOOLMAN_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// just what the list shows and acts on; spoolman's full record is dropped once
// these are read from it
struct SpoolRec {
  uint32_t id = 0;
  std::string name;      // "Vendor - Filament"
  std::string material;
  std::string colour;    // spoolman's color_hex, "" when unset
  double weight = 0;     // grams remaining
  double length = 0;     // metres remaining
  bool archived = false;
};

class SpoolmanPanel {
 public:
  SpoolmanPanel(KWebSocketClient &c, std::mutex &l);
  ~SpoolmanPanel();

  // Embed the list in main navigation; MMU browsing and picking use the screen.
  void attach(lv_obj_t *parent);
  void detach();
  void init();
  void foreground();
  void background();
  // the list, to pick a spool for an MMU lane: done gets the spool id, or -1
  // from the lane's own spool's Remove. caller holds lv_lock
  void pick(const std::string &lane, int current_spool, std::function<void(int)> done);
  // "Lane 1, Lane 3" for a spool id, from the MMU; unset without one
  std::function<std::string(int)> lanes_of;
  void populate_spools(std::vector<SpoolRec> &sorted_spools);
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
  // sort by id and redraw. caller must hold lv_lock
  void repopulate();
  // share the table's width across the columns: the flexible ones take what
  // the fixed ones leave, in whole pixels that reach the right edge exactly
  void layout_columns();
  void end_pick();

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *tab_parent = NULL;
  lv_obj_t *table_box;  // the table and its side scrollbar
  lv_obj_t *spool_table;
  lv_obj_t *empty_box;  // "No spools", shown in the table's place
  lv_obj_t *controls;
  lv_obj_t *switch_cont;
  lv_obj_t *show_archived;
  ButtonContainer reload_btn;
  ButtonContainer back_btn;
  lv_obj_t *pick_bar = NULL;
  lv_obj_t *pick_label = NULL;
  std::function<void(int)> pick_done;
  int pick_current = -1;  // the spool already in the lane being picked for
  bool listening = false;
  int32_t active_id;
  std::map<uint32_t, SpoolRec> spools;
  uint32_t sorted_by;
};

#endif // __SPOOLMAN_PANEL_H__
