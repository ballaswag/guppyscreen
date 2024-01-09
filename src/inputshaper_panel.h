#ifndef __INPUTSHAPER_PANEL_H__
#define __INPUTSHAPER_PANEL_H__

#include "websocket_client.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <mutex>

class InputShaperPanel {
 public:
  InputShaperPanel(KWebSocketClient &c, std::mutex &l);
  ~InputShaperPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_image_clicked(lv_event_t *event);
  void handle_macro_response(json &j);
  void handle_update_slider(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_image_clicked(lv_event_t *event) {
    InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
    panel->handle_image_clicked(event);
  };

  static void _handle_update_slider(lv_event_t *event) {
    InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
    panel->handle_update_slider(event);
  };

  uint32_t find_shaper_index(const std::vector<std::string> &s,
			     const std::string &shaper);

  void set_shaper_detail(json &res,
			 lv_obj_t *label,
			 lv_obj_t *slider,
			 lv_obj_t *slider_label,
			 lv_obj_t *dd);
  
 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;

  // xgraph
  lv_obj_t *xgraph_cont;
  lv_obj_t *xgraph;
  lv_obj_t *xoutput; // calibrate shaper output x
  lv_obj_t *xspinner;

  // y graph
  lv_obj_t *ygraph_cont;
  lv_obj_t *ygraph;
  lv_obj_t *youtput; // calibrate shaper output y
  lv_obj_t *yspinner;

  // x controls
  lv_obj_t *xcontrol;
  lv_obj_t *xaxis_label;
  lv_obj_t *x_switch;
  lv_obj_t *xslider_cont;
  lv_obj_t *xslider;
  lv_obj_t *xlabel;
  lv_obj_t *xshaper_dd;

  // y controls
  lv_obj_t *ycontrol;
  lv_obj_t *yaxis_label;
  lv_obj_t *y_switch;
  lv_obj_t *yslider_cont;
  lv_obj_t *yslider;
  lv_obj_t *ylabel;
  lv_obj_t *yshaper_dd;

  lv_obj_t *button_cont;
  lv_obj_t *switch_cont;
  lv_obj_t *graph_switch_label;
  lv_obj_t *graph_switch;
  ButtonContainer calibrate_btn;
  ButtonContainer save_btn;
  ButtonContainer emergency_btn;
  ButtonContainer back_btn;
  bool ximage_fullsized;
  bool yimage_fullsized;
  json calibrate_output;

  static std::vector<std::string> shapers;
  
};

#endif // __INPUTSHAPER_PANEL_H__
