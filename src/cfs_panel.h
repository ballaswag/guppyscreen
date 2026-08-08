#ifndef __CFS_PANEL_H__
#define __CFS_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"

#include <array>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define CFS_MAX_UNITS      4   /* T1..T4 */
#define CFS_SLOTS_PER_UNIT 4   /* A..D   */

/*
 * CfsPanel - Creality Filament System (CFS) panel for GuppyScreen.
 *
 * Renders the `box` printer object published by Creality's closed-source
 * module and lets the user assign material and colour per slot. Applying
 * is INSTANT: no dialogs, no restarts. The change reaches the `box`
 * object - and therefore Moonraker and the slicer - on the next poll.
 *
 * The klipper helper that makes this possible (cfs_override.py) is
 * embedded in this binary and installs itself on first run. The user
 * does nothing.
 */
class CfsPanel : public NotifyConsumer {
 public:
  CfsPanel(KWebSocketClient &ws, std::mutex &lock);
  ~CfsPanel();

  void consume(json &j);

  lv_obj_t *get_container();
  void foreground();

  void handle_back(lv_event_t *e);
  void handle_slot_click(lv_event_t *e);
  void handle_unit_click(lv_event_t *e);
  void handle_edit_click(lv_event_t *e);
  void handle_load(lv_event_t *e);
  void handle_unload(lv_event_t *e);
  void handle_refresh(lv_event_t *e);
  void handle_type_roller(lv_event_t *e);
  void handle_prod_roller(lv_event_t *e);
  void handle_col_pick(lv_event_t *e);
  void handle_apply(lv_event_t *e);
  void handle_clear(lv_event_t *e);
  void handle_cancel(lv_event_t *e);

  static void _back(lv_event_t *e)    { ((CfsPanel*)e->user_data)->handle_back(e); };
  static void _slot(lv_event_t *e)    { ((CfsPanel*)e->user_data)->handle_slot_click(e); };
  static void _unit(lv_event_t *e)    { ((CfsPanel*)e->user_data)->handle_unit_click(e); };
  static void _edit(lv_event_t *e)    { ((CfsPanel*)e->user_data)->handle_edit_click(e); };
  static void _load(lv_event_t *e)    { ((CfsPanel*)e->user_data)->handle_load(e); };
  static void _unload(lv_event_t *e)  { ((CfsPanel*)e->user_data)->handle_unload(e); };
  static void _refresh(lv_event_t *e) { ((CfsPanel*)e->user_data)->handle_refresh(e); };
  static void _typerol(lv_event_t *e) { ((CfsPanel*)e->user_data)->handle_type_roller(e); };
  static void _prodrol(lv_event_t *e) { ((CfsPanel*)e->user_data)->handle_prod_roller(e); };
  static void _colpick(lv_event_t *e) { ((CfsPanel*)e->user_data)->handle_col_pick(e); };
  static void _apply(lv_event_t *e)   { ((CfsPanel*)e->user_data)->handle_apply(e); };
  static void _clear(lv_event_t *e)   { ((CfsPanel*)e->user_data)->handle_clear(e); };
  static void _cancel(lv_event_t *e)  { ((CfsPanel*)e->user_data)->handle_cancel(e); };

 private:
  struct SlotView {
    lv_obj_t *card;
    lv_obj_t *swatch;
    lv_obj_t *header;
    lv_obj_t *material;
    lv_obj_t *bar;
    lv_obj_t *remain;
    lv_obj_t *edit;
  };

  void create_slot(int idx);
  void build_editor();
  void open_editor();
  void close_editor();
  void rebuild_types();
  void rebuild_products();
  void update_preview();

  void refresh_from_state();
  void update_box(json &box);
  void update_unit_tabs(json &box);
  void update_slots(json &unit);
  void update_header(json &box, json &unit);
  void select_slot(int idx);
  void select_unit(int idx);
  void learn_materials(json &same_material);

  void apply_now();
  void ensure_helper();          /* installs cfs_override.py when missing */

  std::string material_name(const std::string &code) const;
  std::string slot_id() const;
  std::string slot_id(int idx) const;
  std::string unit_key() const;
  void confirm_and_send(const std::string &q, const std::string &gcode);
  void send_gcode(const std::string &gcode);
  static lv_color_t parse_color(const std::string &raw, bool *ok);

  KWebSocketClient &ws;

  lv_obj_t *cont;
  lv_obj_t *header;
  lv_obj_t *unit_cont;
  std::array<lv_obj_t *, CFS_MAX_UNITS> unit_btns;
  lv_obj_t *state_label;
  lv_obj_t *temp_label;
  lv_obj_t *hum_label;
  lv_obj_t *slots_cont;
  std::array<SlotView, CFS_SLOTS_PER_UNIT> slots;
  lv_obj_t *controls;

  ButtonContainer load_btn;
  ButtonContainer unload_btn;
  ButtonContainer refresh_btn;
  ButtonContainer back_btn;

  /* editor: type first, then product */
  lv_obj_t *editor;
  lv_obj_t *editor_title;
  lv_obj_t *prev_swatch;
  lv_obj_t *prev_label;
  lv_obj_t *type_roller;
  lv_obj_t *prod_roller;
  lv_obj_t *col_grid;
  std::vector<std::string> types;      /* distinct types, ordered */
  std::vector<int> prods;              /* catalogue indices for the current type */
  int type_idx;
  int prod_idx;
  std::string pick_colour;
  int edit_slot;

  std::map<std::string, std::string> material_names;
  std::array<bool, CFS_MAX_UNITS> unit_present;
  bool helper_pending;                 /* a restart is needed to load it */
  int unit_idx;
  int slot_idx;
};

#endif /* __CFS_PANEL_H__ */
