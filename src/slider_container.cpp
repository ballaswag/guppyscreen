#include "slider_container.h"
#include "spdlog/spdlog.h"

SliderContainer::SliderContainer(lv_obj_t *parent,
				 const char *label_text,
				 const void *off_btn_img,
				 const char *off_text,
				 const void *max_btn_img,
				 const char *max_text,
				 lv_event_cb_t cb,
				 void *user_data)
  : SliderContainer(parent,
		    label_text,
		    off_btn_img,
		    off_text,
		    cb,
		    user_data,
		    max_btn_img,
		    max_text,
		    cb,
		    user_data,
		    cb,
		    user_data,
		    "%") {
}

SliderContainer::SliderContainer(lv_obj_t *parent,
				 const char *label_text,
				 const void *off_btn_img,
				 const char *off_text,
				 const void *max_btn_img,
				 const char *max_text,
				 lv_event_cb_t cb,
				 void *user_data,
				 std::string u)
  : SliderContainer(parent,
		    label_text,
		    off_btn_img,
		    off_text,
		    cb,
		    user_data,
		    max_btn_img,
		    max_text,
		    cb,
		    user_data,
		    cb,
		    user_data,
		    u) {
}


SliderContainer::SliderContainer(lv_obj_t *parent,
				 const char *label_text,
				 const void *off_btn_img,
				 const char *off_text,
				 lv_event_cb_t off_cb,
				 void * off_cb_user_data,
				 const void *max_btn_img,
				 const char *max_text,
				 lv_event_cb_t max_cb,
				 void * max_cb_user_data,
				 lv_event_cb_t slider_cb,
				 void * slider_user_data,
				 std::string u)
  : cont(lv_obj_create(parent))
  , label(lv_label_create(cont))
  , slider(lv_slider_create(cont))
  , slider_value(lv_label_create(cont))    
  , off_btn(cont, off_btn_img, off_text, off_cb, off_cb_user_data)
  , max_btn(cont, max_btn_img, max_text, max_cb, max_cb_user_data)
  , unit(u)
{
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(cont, 2, 0);
  
  lv_obj_set_width(cont, lv_pct(100));
  lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(slider, LV_PCT(45));

  lv_obj_align(off_btn.get_container(), LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(max_btn.get_container(), LV_ALIGN_RIGHT_MID, 0, 0);

  if (max_text == NULL) {
    max_btn.hide();
  }

  if (slider_cb != NULL) {
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_RELEASED, slider_user_data);
  }

  lv_obj_add_event_cb(slider, &SliderContainer::_handle_value_update,
		      LV_EVENT_VALUE_CHANGED, this);
  

  lv_label_set_text(label, label_text);
  lv_obj_align_to(label, off_btn.get_container(), LV_ALIGN_TOP_LEFT, 15, -20);
  lv_obj_move_foreground(label);

  lv_obj_align_to(slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

SliderContainer::~SliderContainer() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *SliderContainer::get_container() {
  return cont;
}

lv_obj_t *SliderContainer::get_slider() {
  return slider;
}

lv_obj_t *SliderContainer::get_off() {
  return off_btn.get_container();
}

lv_obj_t *SliderContainer::get_max() {
  return max_btn.get_container();
}

void SliderContainer::set_range(int min_range, int max_range) {
  lv_slider_set_range(slider, min_range, max_range);
}

void SliderContainer::update_value(int value) {
  lv_label_set_text(slider_value, fmt::format("{}{}", value, unit).c_str());
  lv_slider_set_value(slider, value, LV_ANIM_ON);
  lv_obj_align_to(slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

void SliderContainer::handle_value_update(lv_event_t *event) {
  lv_obj_t * obj = lv_event_get_target(event);
  update_value((int)lv_slider_get_value(obj));
}
