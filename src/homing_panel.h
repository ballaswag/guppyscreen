#ifndef __HOMING_PANEL_H__
#define __HOMING_PANEL_H__

#include "lvgl/lvgl.h"
#include "button_container.h"
#include "websocket_client.h"
#include "selector.h"
#include "notify_consumer.h"

#include <mutex>

class HomingPanel : public NotifyConsumer {
 public:
  HomingPanel(KWebSocketClient &ws, std::mutex &);
  ~HomingPanel();

  void consume(json &data);
  lv_obj_t * get_container();
  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_selector_cb(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    HomingPanel *panel = (HomingPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_selector_cb(lv_event_t *event) {
    HomingPanel *panel = (HomingPanel*)event->user_data;
    panel->handle_selector_cb(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *homing_cont;
  ButtonContainer home_all_btn;
  ButtonContainer home_xy_btn;
  ButtonContainer y_up_btn;
  ButtonContainer y_down_btn;
  ButtonContainer x_up_btn;
  ButtonContainer x_down_btn;
  ButtonContainer z_up_btn;
  ButtonContainer z_down_btn;
  ButtonContainer emergency_btn;
  ButtonContainer motoroff_btn;
  ButtonContainer back_btn;
  Selector distance_selector;
};

#endif // __HOMING_PANEL_H__
