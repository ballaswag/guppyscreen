#ifndef __FAN_PANEL_H__
#define __FAN_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "slider_container.h"
#include "button_container.h"

#include <map>
#include <memory>
#include <mutex>

class FanPanel : public NotifyConsumer {
 public:
  FanPanel(KWebSocketClient &ws, std::mutex &lock);
  ~FanPanel();

  void consume(json &j);
  
  lv_obj_t *get_container() { return fanpanel_cont; }
  void create_fans(json &f);
  void foreground();
  void background();
  void handle_callback(lv_event_t *event);
  void handle_fan_update(lv_event_t *event);
  void handle_fan_update_part_fan(lv_event_t *event);
  void handle_fan_update_generic(lv_event_t *event);
  // the gcode that sets one fan to a fraction, by the kind of object it is
  std::string fan_gcode(const std::string &key, double fraction);

  static void _handle_callback(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_fan_update(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update(event);
  };

  static void _handle_fan_update_part_fan(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update_part_fan(event);
  };
  
  static void _handle_fan_update_generic(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update_generic(event);
  };

 private:

  KWebSocketClient &ws;
  lv_obj_t *fanpanel_cont;
  lv_obj_t *fans_cont;
  lv_obj_t *side_cont;
  ButtonContainer all_on_btn;
  ButtonContainer all_off_btn;
  std::map<std::string, std::shared_ptr<SliderContainer>> fans;
  /* SliderContainer fan0; */
  /* SliderContainer fan1; */
  /* SliderContainer fan2; */
};

#endif // __FAN_PANEL_H__
