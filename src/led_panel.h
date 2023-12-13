#ifndef __LED_PANEL_H__
#define __LED_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "slider_container.h"
#include "button_container.h"

#include <mutex>
#include <map>

class LedPanel : public NotifyConsumer {
 public:
  LedPanel(KWebSocketClient &, std::mutex &);
  ~LedPanel();

  void consume(json &j);

  lv_obj_t *get_container();
  void init(json&);
  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_led_update(lv_event_t *event);
  void handle_led_update_generic(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    LedPanel *panel = (LedPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_led_update(lv_event_t *event) {
    LedPanel *panel = (LedPanel*)event->user_data;
    panel->handle_led_update(event);
  };

  static void _handle_led_update_generic(lv_event_t *event) {
    LedPanel *panel = (LedPanel*)event->user_data;
    panel->handle_led_update_generic(event);
  };


 private:
  KWebSocketClient &ws;
  lv_obj_t *ledpanel_cont;
  lv_obj_t *leds_cont;
  std::map<std::string, std::shared_ptr<SliderContainer>> leds;
  ButtonContainer back_btn;

};

#endif // __LED_PANEL_H__
