#ifndef __SPINBOX_SELECTOR_H__
#define __SPINBOX_SELECTOR_H__

#include "lvgl/lvgl.h"

#include <functional>
#include <string>


class SpinBoxSelector {
 public:
  SpinBoxSelector(lv_obj_t *parent,
		  const std::string &name,
		  int min,
		  int max,
		  int value,
		  std::function<void(int)>);

  ~SpinBoxSelector();

  void update_value(int v);

 private:
  lv_obj_t *cont;
  lv_obj_t *sb_cont;
  lv_obj_t *sb;
  std::function<void(int)> cb;
  
};

#endif // __SPINBOX_SELECTOR_H__
