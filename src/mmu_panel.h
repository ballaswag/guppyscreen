#ifndef __MMU_PANEL_H__
#define __MMU_PANEL_H__

#include "mmu_backend.h"
#include "tool_map_panel.h"
#include "button_container.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "lvgl/lvgl.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// "RRGGBB", "#rrggbb" (any case) -> colour. false (and a placeholder grey in
// *out) when the string is not a colour. Shared with the spoolman table,
// which paints the same vendor hex strings.
bool parse_colour(const std::string &colour, lv_color_t *out);

// shared with the print-start tool map:
// a dim sheet (cb on a tap outside) with a centred box, which it returns
lv_obj_t *create_popout(lv_obj_t **overlay, lv_event_cb_t cb, void *user_data);
void show_popout(lv_obj_t *overlay);
void hide_popout(lv_obj_t *overlay);
// a slot as a picker tile; by_name labels it by slot name, not tool
lv_obj_t *create_slot_tile(lv_obj_t *parent, const MmuSlot &slot, size_t idx, int width,
                           lv_event_cb_t cb, void *user_data, bool by_name = false);

// Vendor-agnostic MMU panel: renders the slots of whichever MmuBackend is
// active and drives it through the MmuBackend verbs only.
class MmuPanel : public NotifyConsumer {
 public:
  MmuPanel(KWebSocketClient &ws, std::mutex &l);
  ~MmuPanel();

  // Probe compiled-in backends in registration order when Klipper connects.
  void add_backend(MmuBackend *b) { backends.push_back(b); }
  // Detect a supported backend; NULL leaves MMU unavailable.
  MmuBackend *select_backend();
  // names of the lanes holding spool_id, ", " separated
  std::string spool_lanes(int spool_id) const {
    return backend == NULL ? std::string() : backend->lanes_with_spool(spool_id);
  }
  // a long-pressed lane asks for a spool: the lane's name, its spool now (-1
  // for none), and what to call with the chosen spool (-1 clears)
  std::function<void(const std::string &, int, std::function<void(int)>)> pick_spool;
  std::function<void()> open_spoolman;
  void set_spoolman_available(bool available);

  // build the UI into the MMU tab. caller must hold lv_lock.
  void create(lv_obj_t *parent);
  // pull current state after the initial subscribe. caller must hold lv_lock.
  void init_state();
  // Opens the print-start tool map for a file's metadata; print() runs from
  // its Print button. false when there is nothing to map: print directly.
  // Caller holds lv_lock.
  bool open_tool_map(const json &metadata, const std::string &filename,
                     std::function<void()> print);
  // Detach the backend and hide the panel. Caller must hold lv_lock.
  void clear();
  void consume(json &j);

  void handle_card(lv_event_t *e);
  void handle_status_bar(lv_event_t *e);
  void handle_page_prev(lv_event_t *e);
  void handle_page_next(lv_event_t *e);
  void handle_edit_action(lv_event_t *e);

  static void _handle_card(lv_event_t *e) {
    ((MmuPanel*)e->user_data)->handle_card(e);
  };

  static void _handle_status_bar(lv_event_t *e) {
    ((MmuPanel*)e->user_data)->handle_status_bar(e);
  };

  static void _handle_page_prev(lv_event_t *e) {
    ((MmuPanel*)e->user_data)->handle_page_prev(e);
  };

  static void _handle_page_next(lv_event_t *e) {
    ((MmuPanel*)e->user_data)->handle_page_next(e);
  };

  static void _handle_edit_action(lv_event_t *e) {
    ((MmuPanel*)e->user_data)->handle_edit_action(e);
  };

 private:
  struct Card {
    lv_obj_t *cont;
    lv_obj_t *spool;
    lv_obj_t *hole;
    lv_obj_t *used;
    lv_obj_t *title;
    lv_obj_t *material;
  };

  void refresh();
  void populate();
  void rebuild_grid(size_t page_count);
  void update_nav(size_t total_pages);

  // Full-screen native panels
  void create_edit_screen();
  void create_colour_swatches();
  void open_edit(int idx);
  void close_edit();
  void update_edit_preview();
  void save_edit();

  // dim sheet + centred box, shared by the three popouts; returns the box
  lv_obj_t *create_popout(lv_obj_t **overlay);
  lv_color_t slot_colour(const MmuSlot &slot, bool *valid);
  bool is_backup_slot(int idx) const;

  KWebSocketClient &ws;
  ToolMapPanel tool_map;
  std::vector<MmuBackend*> backends;
  MmuBackend *backend;
  lv_obj_t *cont;

  // Main Tab Spool Grid
  lv_obj_t *header_row;
  lv_obj_t *status_bar;
  lv_obj_t *status_label;
  std::unique_ptr<ButtonContainer> spoolman_btn;
  bool spoolman_available = false;
  lv_obj_t *cards_row1;
  lv_obj_t *cards_row2;
  lv_obj_t *nav_row;
  lv_obj_t *nav_prev_btn;
  lv_obj_t *nav_next_btn;
  lv_obj_t *nav_label;
  std::vector<Card> visible_cards;
  size_t current_page;
  size_t built_page_count; // card count the current grid was built for

  // Full-Screen Native Spool Edit Panel (attached to lv_scr_act())
  lv_obj_t *edit_panel_cont;
  lv_obj_t *edit_preview_spool;
  lv_obj_t *edit_preview_hole;
  lv_obj_t *edit_preview_used;
  lv_obj_t *edit_name_lbl;
  lv_obj_t *edit_tool_lbl;
  lv_obj_t *edit_mat_lbl;
  lv_obj_t *edit_load_btn;   // toggles between Load and Unload with slot state
  lv_obj_t *edit_eject_btn;
  // Backup, Save and Back are icon tiles like the ones on the extruder panel,
  // so they answer a tap the same way -- the icon takes the accent. They are
  // built with the edit screen rather than in the constructor, hence pointers.
  std::unique_ptr<ButtonContainer> edit_backup_btn;  // infinite spool assignment
  lv_obj_t *edit_swatches_row1;
  lv_obj_t *edit_swatches_row2;
  std::vector<lv_obj_t*> colour_swatch_btns;
  std::vector<std::string> colour_swatch_hex; // value behind each swatch, "" = clear
  std::vector<std::string> materials;        // inline row, first few from config
  std::vector<std::string> material_catalog; // popout: configured material list
  std::vector<lv_obj_t*> material_btns;
  std::unique_ptr<ButtonContainer> edit_save_btn;
  std::unique_ptr<ButtonContainer> edit_back_btn;

  // Backup picker popout: choose which slot this one backs up
  lv_obj_t *backup_picker;
  lv_obj_t *backup_picker_list;
  std::vector<lv_obj_t*> backup_pick_btns;
  std::vector<std::string> backup_pick_names; // slot each tile was built for
  void open_backup_picker();
  void close_backup_picker();

  // Custom colour popout: hue wheel + saturation/brightness sliders
  lv_obj_t *colour_picker;
  lv_obj_t *colour_wheel;
  lv_obj_t *colour_sat_slider;
  lv_obj_t *colour_val_slider;
  lv_obj_t *colour_pick_preview;
  lv_obj_t *colour_pick_ok;
  lv_obj_t *colour_pick_cancel;
  lv_obj_t *custom_colour_btn;
  void open_colour_picker();
  void close_colour_picker();
  lv_color_t picker_colour();

  // Material picker popout ("more materials")
  lv_obj_t *material_picker;
  lv_obj_t *material_picker_list;
  lv_obj_t *more_mat_btn;
  std::vector<lv_obj_t*> mat_pick_btns;
  void open_material_picker();
  void close_material_picker();

  int edit_slot_idx;
  // slots are addressed by index but the backend rebuilds that vector on every
  // refresh, so the open edit screen is pinned by name and re-resolved instead
  std::string edit_slot_name;
  std::string draft_colour;
  std::string draft_material;
  // a touched draft survives external slot updates. Tracked per field: editing
  // the colour must not pin a material another client changed meanwhile
  bool draft_colour_dirty = false;
  bool draft_material_dirty = false;

  // local copy of the active backend's state, refreshed before each redraw
  std::vector<MmuSlot> slots;
  int loaded_idx;
  MmuActivity activity;
  std::string message;
  std::string dismissed_message; // tapped away locally; cleared when it changes
  bool message_error;            // banner is a fault, not information
  bool error_state;
  bool bypass;
  bool spoolman_active = false; // weights only mean something via spoolman
  bool weight_active = false;   // ... or when the backend counts them down
  // percent of the spool left, -1 when no weight is known
  int remaining_pct(const MmuSlot &slot) const;
  bool card_long_pressed = false; // its release still clicks
  void pick_spool_for(int idx);
};

#endif // __MMU_PANEL_H__
