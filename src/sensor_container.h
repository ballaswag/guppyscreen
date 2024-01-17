#ifndef __SENSOR_CONTAINER_H__
#define __SENSOR_CONTAINER_H__

#include "websocket_client.h"
#include "numpad.h"
#include "lvgl/lvgl.h"

#include <ctime>

class SensorContainer {
 public:
  SensorContainer(KWebSocketClient &c,
		  lv_obj_t *parent,
		  const void *img,
		  const char *text,
		  lv_color_t color,
		  bool editable,
		  bool show_target,
		  Numpad &np,
		  std::string name,
		  lv_obj_t *chart,
		  lv_chart_series_t *chart_series);
		  
  SensorContainer(KWebSocketClient &c,
		  lv_obj_t *parent,
		  const void *img,
		  uint16_t img_scale,
		  const char *text,
		  lv_color_t color,
		  bool editable,
		  bool show_target,
		  Numpad &np,
		  std::string name,
		  lv_obj_t *chart,
		  lv_chart_series_t *chart_series);
  
  ~SensorContainer();

  lv_obj_t *get_sensor();
  void update_target(int new_target);
  void update_value(int new_value);
  void update_series(int value);
  void handle_edit(lv_event_t *event);

  static void _handle_edit(lv_event_t *event) {
    SensorContainer *panel = (SensorContainer*)event->user_data;
    panel->handle_edit(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *sensor_cont;
  lv_obj_t *sensor_img;
  lv_obj_t *sensor_label;
  lv_obj_t *value_label;
  int value;
  lv_obj_t *divider_label;
  lv_obj_t *target_label;
  int target;
  Numpad &numpad;
  std::string id;
  lv_obj_t *chart;
  lv_chart_series_t *series;
  std::time_t last_updated_ts;
  
};

#endif // __SENSOR_CONTAINER_H__
