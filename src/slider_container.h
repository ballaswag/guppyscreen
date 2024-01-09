#ifndef __SLIDER_CONTAINER_H__
#define __SLIDER_CONTAINER_H__

#include "lvgl/lvgl.h"
#include "button_container.h"

#include <string>

class SliderContainer {
 public:
  SliderContainer(lv_obj_t *parent,
		  const char *label_text,
		  const void *off_bnt_img,
		  const char *off_text,
		  const void *max_bnt_img,
		  const char *max_text,
		  lv_event_cb_t cb,
		  void *user_data);

  SliderContainer(lv_obj_t *parent,
		  const char *label_text,
		  const void *off_bnt_img,
		  const char *off_text,
		  const void *max_bnt_img,
		  const char *max_text,
		  lv_event_cb_t cb,
		  void *user_data,
		  std::string u);

  SliderContainer(lv_obj_t *parent,
		  const char *label_text,
		  const void *off_bnt_img,
		  const char *off_text,
		  lv_event_cb_t off_cb,
		  void * off_cb_user_data,
		  const void *max_bnt_img,
		  const char *max_text,
		  lv_event_cb_t max_cb,
		  void * max_cb_user_data,
		  lv_event_cb_t slider_cb,
		  void * slider_user_data,
		  std::string u);
  ~SliderContainer();
  lv_obj_t *get_container();
  lv_obj_t *get_slider();
  lv_obj_t *get_off();
  lv_obj_t *get_max();

  void set_range(int min_range, int max_range);

  void update_value(int value);

  void handle_value_update(lv_event_t *event);

  static void _handle_value_update(lv_event_t *event) {
    SliderContainer *obj = (SliderContainer*)event->user_data;
    obj->handle_value_update(event);
  };
  

 private:
  lv_obj_t *cont;
  lv_obj_t *label;
  lv_obj_t *control_cont;
  ButtonContainer off_btn;
  lv_obj_t *slider_cont;
  lv_obj_t *slider;
  lv_obj_t *slider_value;
  ButtonContainer max_btn;
  std::string unit;
};

#endif // __SLIDER_CONTAINER_H__
