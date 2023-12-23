#ifndef __MACROS_PANEL_H__
#define __MACROS_PANEL_H__

#include "websocket_client.h"
#include "macro_item.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <memory>
#include <mutex>

class MacrosPanel {
 public:
  MacrosPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent);
  ~MacrosPanel();

  void populate();
  void handle_hide_show(lv_event_t *e);

  static void _handle_hide_show(lv_event_t *e) {
    MacrosPanel *panel = (MacrosPanel*)e->user_data;
    panel->handle_hide_show(e);
  };

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *top_controls;
  lv_obj_t *show_hide_switch;
  lv_obj_t *top_cont;
  lv_obj_t *kb;
  std::vector<std::shared_ptr<MacroItem>> macro_items;

};

#endif // __MACROS_PANEL_H__
