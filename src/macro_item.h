#ifndef __MACRO_ITEM_H__
#define __MACRO_ITEM_H__

#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <string>
#include <vector>
#include <map>
#include <utility>

class MacroItem {
 public:
  MacroItem(KWebSocketClient &c,
	    lv_obj_t *parent,
	    std::string macro_name,
	    const std::map<std::string, std::string> &m_params,
	    lv_obj_t *keyboard,
	    // lv_color_t bg_color,
	    bool hide);

  ~MacroItem();

  void handle_kb_input(lv_event_t *e);
  void handle_send_macro(lv_event_t *e);
  void handle_hide_show(lv_event_t *e);
  void hide_if_hidden();
  void show();

  static void _handle_kb_input(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _handle_send_macro(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_send_macro(e);
  };

  static void _handle_hide_show(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_hide_show(e);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *top_cont;
  lv_obj_t *macro_label;
  lv_obj_t *hide_show;
  lv_obj_t *kb;
  bool hidden;
  bool always_visible;
  std::vector<std::pair<lv_obj_t*, lv_obj_t*>> params;

};

#endif // __MACRO_ITEM_H__
