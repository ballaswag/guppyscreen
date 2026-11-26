#ifndef __LED_PANEL_H__
#define __LED_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "slider_container.h"
#include "button_container.h"

#include <mutex>
#include <map>
#include <string>

class LedPanel : public NotifyConsumer {
 public:
  LedPanel(KWebSocketClient &, std::mutex &);
  ~LedPanel();

  void consume(json &j);

  lv_obj_t *get_container() { return ledpanel_cont; }
  void init(json&);
  void activate();
  const void *get_main_button_image();
  void foreground();
  void background();
  void handle_callback(lv_event_t *event);
  void handle_led_update(lv_event_t *event);
  void handle_led_update_generic(lv_event_t *event);
  // the gcode that sets one led to a fraction, by the kind of object it is
  std::string led_gcode(const std::string &key, double fraction);

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
  void toggle_single_led();
  double get_led_value(const std::string &led_id);
  KWebSocketClient &ws;
  lv_obj_t *ledpanel_cont;
  lv_obj_t *leds_cont;
  lv_obj_t *side_cont;
  ButtonContainer all_on_btn;
  ButtonContainer all_off_btn;
  ButtonContainer back_btn;
  std::map<std::string, std::shared_ptr<SliderContainer>> leds;
  std::string single_led_id;
  bool single_led_is_output_pin{false};
  double single_led_last_value{0.0};
  bool single_led_last_value_valid{false};

};

#endif // __LED_PANEL_H__
