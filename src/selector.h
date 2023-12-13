#ifndef __K_SELECTOR_H__
#define __K_SELECTOR_H__

#include "lvgl/lvgl.h"
#include <string>
#include <vector>

class Selector {
 public:
  Selector(lv_obj_t *parent,
	   const char *label_text,
	   std::vector<const char*> map,
	   uint32_t default_idx,
	   int32_t width_pct,
	   int32_t height_pct,
	   lv_event_cb_t cb,
	   void *cb_data);

  Selector(lv_obj_t *parent,
	   const char *label_text,
	   std::vector<const char*> map,
	   uint32_t default_idx,
	   lv_event_cb_t cb,
	   void *cb_data);
  
  ~Selector();
  lv_obj_t *get_selector();
  lv_obj_t *get_label();
  uint32_t get_selected_idx();
  void set_selected_idx(uint32_t idx);

 private:
  lv_obj_t *btnm;
  lv_obj_t *label;
  std::vector<const char*> map;
  uint32_t selector_idx;
};

#endif //  __K_SELECTOR_H__
