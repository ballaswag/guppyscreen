#ifndef __CFS_MAPPING_H__
#define __CFS_MAPPING_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

/*
 * CfsMapping - filament mapping dialog, shown before a multi-colour print.
 *
 * Shown before every print on a CFS printer. Lists the filaments the sliced
 * file expects, each with its colour and type, and lets the user pick which
 * physical slot feeds it. Confirming sends CFS_MAP_TOOL per row, then prints.
 *
 * The mapping is klipper's own tool -> slot table (box.box_state.Tnn_map), so
 * it takes effect immediately and applies to the print about to start.
 */
class CfsMapping {
 public:
  CfsMapping(KWebSocketClient &ws, std::mutex &lock);
  ~CfsMapping();

  /* True whenever a CFS is present: the dialog is shown for every print. */
  static bool available();

  /* Shows the dialog. on_confirm() runs after the mapping has been sent. */
  void show(json &metadata, std::function<void()> on_confirm);

  void handle_slot(lv_event_t *e);
  void handle_print(lv_event_t *e);
  void handle_cancel(lv_event_t *e);

  static void _slot(lv_event_t *e)   { ((CfsMapping*)e->user_data)->handle_slot(e); };
  static void _print(lv_event_t *e)  { ((CfsMapping*)e->user_data)->handle_print(e); };
  static void _cancel(lv_event_t *e) { ((CfsMapping*)e->user_data)->handle_cancel(e); };

 private:
  struct Row {
    lv_obj_t *cont;
    lv_obj_t *swatch;      /* colour the file asks for */
    lv_obj_t *label;       /* "1  PLA" */
    lv_obj_t *slot_btn;    /* target slot, tap to cycle */
    lv_obj_t *slot_swatch; /* colour actually in that slot */
    lv_obj_t *slot_label;
    int       tool;        /* 0-based index in the file */
    int       choice;      /* index into `slots` */
  };

  void build();
  void rebuild_slots();
  void refresh_row(Row &r);
  void clear_rows();

  KWebSocketClient &ws;
  std::mutex &lv_lock;

  lv_obj_t *cont;
  lv_obj_t *title;
  lv_obj_t *rows_cont;
  std::vector<Row> rows;

  std::vector<std::string> slots;   /* "T1A".. of the slots that hold filament */
  std::vector<std::string> colours; /* colour of each of those slots */

  std::function<void()> on_confirm;
};

#endif /* __CFS_MAPPING_H__ */
