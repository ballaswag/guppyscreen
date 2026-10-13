#ifndef __NUMPAD_H__
#define __NUMPAD_H__

#include "lvgl/lvgl.h"
#include <functional>
#include <limits>

class Numpad {
 public:
  Numpad(lv_obj_t *parent);
  ~Numpad();

  // a value outside [min, max] is clamped and shown for a second OK instead of
  // being sent; 0 always passes, since it is "off" for any target
  void set_callback(std::function<void(double)> cb,
                    double min = std::numeric_limits<double>::lowest(),
                    double max = std::numeric_limits<double>::max());
  void handle_input(lv_event_t *event);
  void handle_kb_input(lv_event_t *event);
  /* void handle_defocused(lv_event_t *event); */
  void foreground_reset();
  void background();
  // cover the parent from x (parent-content coordinates) to its right edge
  void cover_from(lv_coord_t x);
  // mark obj checked while the numpad is open for it; NULL clears
  void set_highlight_target(lv_obj_t *obj);
  // forget obj if it is the current target, before it is deleted
  void release_if_target(lv_obj_t *obj);

  static void _handle_input(lv_event_t *event) {
    Numpad *panel = (Numpad*)event->user_data;
    panel->handle_input(event);
  };

  static void _handle_kb_input(lv_event_t *event) {
    Numpad *panel = (Numpad*)event->user_data;
    panel->handle_kb_input(event);
  };

  static void _handle_blink_timer(lv_timer_t *timer);

  /* static void _handle_defocused(lv_event_t *event) { */
  /*   Numpad *panel = (Numpad*)event->user_data; */
  /*   panel->handle_defocused(event); */
  /* }; */

 private:
  lv_obj_t *edit_cont;
  lv_obj_t *input;
  lv_obj_t *kb;
  std::function<void(double)> ready_cb;
  double range_min;
  double range_max;
  lv_timer_t *blink_timer;
  bool prev_was_empty;
  lv_obj_t *highlight_target;
};

#endif // __NUMPAD_H__
