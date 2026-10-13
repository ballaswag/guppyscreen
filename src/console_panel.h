#ifndef __CONSOLE_PANEL_H__
#define __CONSOLE_PANEL_H__

#include "button_container.h"
#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <list>

class ConsolePanel {
 public:
  ConsolePanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
  ~ConsolePanel();

  lv_obj_t *get_container();
  void foreground();
  void set_gcode_input_enabled(bool enabled);
  void handle_macro_response(json &d);
  void handle_delete_btn(lv_event_t *event);
  void handle_input(lv_event_t *event);

  static void _handle_delete_btn(lv_event_t *event) {
    ConsolePanel *panel = (ConsolePanel*)event->user_data;
    panel->handle_delete_btn(event);
  };

  static void _handle_input(lv_event_t *event) {
    static_cast<ConsolePanel *>(event->user_data)->handle_input(event);
  };

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *console_cont;
  lv_obj_t *top_cont;
  lv_obj_t *output;
  lv_obj_t *lower_cont;   // the entry row and its keyboard
  lv_obj_t *bottom_cont;  // the entry row: gcode line and the clear tile
  lv_obj_t *input;
  lv_obj_t *kb;
  ButtonContainer delete_btn;
  bool gcode_input_enabled = false;
};

#endif // __CONSOLE_PANEL_H__
