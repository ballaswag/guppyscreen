#ifndef __FINETINE_PANEL_H__
#define __FINETINE_PANEL_H__

#include "lvgl/lvgl.h"
#include "button_container.h"
#include "selector.h"
#include "image_label.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

class FineTunePanel : public NotifyConsumer {
 public:
  FineTunePanel(KWebSocketClient &, std::mutex &);
  ~FineTunePanel();
  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_zoffset(lv_event_t *event);
  void handle_pa(lv_event_t *event);
  void handle_speed(lv_event_t *event);
  void handle_flow(lv_event_t *event);

  void consume(json &j);  
  
  static void _handle_callback(lv_event_t *event) {
    FineTunePanel *panel = (FineTunePanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_zoffset(lv_event_t *event) {
    FineTunePanel *panel = (FineTunePanel*)event->user_data;
    panel->handle_zoffset(event);
  };

  static void _handle_pa(lv_event_t *event) {
    FineTunePanel *panel = (FineTunePanel*)event->user_data;
    panel->handle_pa(event);
  };

  static void _handle_speed(lv_event_t *event) {
    FineTunePanel *panel = (FineTunePanel*)event->user_data;
    panel->handle_speed(event);
  };
  
  static void _handle_flow(lv_event_t *event) {
    FineTunePanel *panel = (FineTunePanel*)event->user_data;
    panel->handle_flow(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *values_cont;
  ButtonContainer zreset_btn;
  ButtonContainer zup_btn;
  ButtonContainer zdown_btn;
  ButtonContainer pareset_btn;
  ButtonContainer paup_btn;
  ButtonContainer padown_btn;
  ButtonContainer speed_reset_btn;
  ButtonContainer speed_up_btn;
  ButtonContainer speed_down_btn;  
  ButtonContainer flow_reset_btn;
  ButtonContainer flow_up_btn;
  ButtonContainer flow_down_btn;
  ButtonContainer back_btn;
  Selector zoffset_selector;
  Selector multipler_selector;
  ImageLabel z_offset;
  ImageLabel pa;
  ImageLabel speed_factor;
  ImageLabel flow_factor;
};

#endif  // __FINETINE_PANEL_H__
