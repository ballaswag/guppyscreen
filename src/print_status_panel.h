#ifndef __PRINT_STATUS_PANEL_H__
#define __PRINT_STATUS_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "exclude_object_panel.h"
#include "image_label.h"
#include "finetune_panel.h"
#include "mini_print_status.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <ctime>
#include <map>

class PrintStatusPanel : public NotifyConsumer {
 public:
  PrintStatusPanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *mini_parent);
  ~PrintStatusPanel();

  void init(json &fans);
  void reset();
  void populate();
  void foreground();
  void background();
  lv_obj_t *get_container() { return status_cont; }
  bool is_foreground() const;

  void handle_metadata(const std::string &gcode_file, json &j);
  void handle_callback(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    PrintStatusPanel *panel = (PrintStatusPanel*)event->user_data;
    panel->handle_callback(event);
  };

  void consume(json &j);
  void update_time_progress(uint32_t time_passed);
  void update_flow_rate(double filament_used);
  void update_layers(json &info);
  int max_layer(json &info);
  int current_layer(json &info);

  FineTunePanel &get_finetune_panel();
  // the small printing chip shown on the main tab
  lv_obj_t *get_mini_status() { return mini_print_status.get_container(); }

 private:
  KWebSocketClient &ws;
  FineTunePanel finetune_panel;
  ExcludeObjectPanel exclude_object_panel;
  MiniPrintStatus mini_print_status;
  lv_obj_t *status_cont;
  lv_obj_t *buttons_cont;
  ButtonContainer finetune_btn;
  ButtonContainer objects_btn;
  ButtonContainer pause_btn;
  ButtonContainer resume_btn;
  ButtonContainer cancel_btn;
  ButtonContainer back_btn;
  lv_obj_t *thumbnail_cont;
  lv_obj_t *thumbnail;
  lv_obj_t *pbar_cont;
  lv_obj_t *progress_bar;
  lv_obj_t *progress_label;
  lv_obj_t *detail_cont;

  ImageLabel extruder_temp;
  ImageLabel bed_temp;
  ImageLabel chamber_temp;
  ImageLabel print_speed;
  ImageLabel z_offset;
  ImageLabel flow_rate;
  ImageLabel layers;
  ImageLabel fans;
  ImageLabel elapsed;
  /* ImageLabel fan1; */
  ImageLabel time_left;
  /* ImageLabel fan2; */

  /* json &metadata; */
  uint32_t estimated_time_s;

  // flow rate
  std::time_t flow_ts;
  double last_filament_used;
  double filament_diameter;
  double flow;
  int extruder_target;
  std::string active_extruder_;
  int heater_bed_target;
  void update_active_extruder(const json &update);
  json current_file;

  std::map<std::string, int> fan_speeds;
  std::string chamber_sensor_key_;
  bool is_foreground_ = false;
  std::string print_state_;
};

#endif // __PRINT_STATUS_PANEL_H__
