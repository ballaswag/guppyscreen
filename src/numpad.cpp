#include "numpad.h"
#include "logger.h"
#include "theme.h"

#include <algorithm>
#include <string>

Numpad::Numpad(lv_obj_t *parent)
  : edit_cont(lv_obj_create(parent))
  , input(lv_textarea_create(edit_cont))
  , kb(lv_keyboard_create(edit_cont))
  , ready_cb([](double v){})
  , range_min(std::numeric_limits<double>::lowest())
  , range_max(std::numeric_limits<double>::max())
  , blink_timer(NULL)
  , prev_was_empty(false)
  , highlight_target(NULL)
{
  LOG_TRACE("creating numpad on main_cont");
  // hidden until a readout asks for it; the caller places it with cover_from()
  lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE
                  | LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(edit_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_background(edit_cont);
  lv_obj_add_style(edit_cont, &Theme::styles().screen, 0);

  lv_obj_set_flex_align(edit_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_flow(edit_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_set_size(input, LV_PCT(100), LV_SIZE_CONTENT);
  lv_textarea_set_one_line(input, true);
  // the blink that says a value was clamped
  lv_obj_set_style_border_color(input, Theme::col(Theme::DANGER), LV_STATE_USER_1);
  lv_obj_set_style_text_color(input, Theme::col(Theme::DANGER), LV_STATE_USER_1);

  lv_obj_set_width(kb, LV_PCT(100));
  lv_obj_set_flex_grow(kb, 1);
  static const char * kb_map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, NULL };
  static const lv_btnmatrix_ctrl_t kb_ctrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
  
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  lv_keyboard_set_textarea(kb, input);

  lv_obj_add_event_cb(input, &Numpad::_handle_input, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(kb, &Numpad::_handle_kb_input, LV_EVENT_VALUE_CHANGED, this);
}

Numpad::~Numpad() {
  if (blink_timer != NULL) {
    lv_timer_del(blink_timer);
    blink_timer = NULL;
  }
  if (edit_cont != NULL) {
    lv_obj_del(edit_cont);
    edit_cont = NULL;
  }
}

void Numpad::set_callback(std::function<void(double)> cb, double min, double max) {
  ready_cb = cb;
  range_min = min;
  range_max = max;
}

void Numpad::handle_input(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    // input validation, e.g. range
  }

  if (code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(edit_cont);
    set_highlight_target(NULL);
  }
  
  if (code == LV_EVENT_READY) {
    // input validation, e.g. range
    std::string value = std::string(lv_textarea_get_text(input));
    if (value.length() > 0) {
      double v = std::stod(value);
      if (v != 0 && (v < range_min || v > range_max)) {
        // clamp and stay open: the corrected value blinks, a second OK sends it
        lv_textarea_set_text(input, fmt::format("{}", std::clamp(v, range_min, range_max)).c_str());
        if (blink_timer == NULL) {
          lv_obj_add_state(input, LV_STATE_USER_1);
          blink_timer = lv_timer_create(&Numpad::_handle_blink_timer, 150, this);
          lv_timer_set_repeat_count(blink_timer, 3);  // off, on, off
        }
        return;
      }
      ready_cb(v);
    }

    lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(edit_cont);
    set_highlight_target(NULL);
    lv_textarea_set_text(input, "");
  }
}

void Numpad::handle_kb_input(lv_event_t *e) {
  uint16_t btn = lv_btnmatrix_get_selected_btn(kb);
  const char *btn_text = lv_btnmatrix_get_btn_text(kb, btn);
  if (btn_text != NULL && strcmp(btn_text, LV_SYMBOL_BACKSPACE) == 0) {
    bool is_empty = strlen(lv_textarea_get_text(input)) == 0;
    if (is_empty && prev_was_empty) {
      lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_background(edit_cont);
      set_highlight_target(NULL);
    }
    prev_was_empty = is_empty;
  } else {
    prev_was_empty = false;
  }
}

void Numpad::_handle_blink_timer(lv_timer_t *timer) {
  Numpad *numpad = static_cast<Numpad *>(timer->user_data);
  if (lv_obj_has_state(numpad->input, LV_STATE_USER_1)) {
    lv_obj_clear_state(numpad->input, LV_STATE_USER_1);
  } else {
    lv_obj_add_state(numpad->input, LV_STATE_USER_1);
  }
  if (timer->repeat_count == 0) {
    numpad->blink_timer = NULL;  // LVGL deletes a finished timer itself
  }
}

void Numpad::cover_from(lv_coord_t x) {
  lv_obj_t *parent = lv_obj_get_parent(edit_cont);
  lv_obj_set_align(edit_cont, LV_ALIGN_DEFAULT);
  // parent-content coordinates: reach past the parent's own padding on the
  // other three sides so nothing shows around the popout
  const lv_coord_t top = lv_obj_get_style_pad_top(parent, 0);
  lv_obj_set_pos(edit_cont, x, -top);
  lv_obj_set_size(edit_cont, lv_obj_get_width(parent) - lv_obj_get_style_pad_left(parent, 0) - x,
                  lv_obj_get_height(parent));
}

void Numpad::set_highlight_target(lv_obj_t *obj) {
  if (highlight_target != NULL) {
    lv_obj_clear_state(highlight_target, LV_STATE_CHECKED);
  }
  highlight_target = obj;
  if (highlight_target != NULL) {
    lv_obj_add_state(highlight_target, LV_STATE_CHECKED);
  }
}

void Numpad::release_if_target(lv_obj_t *obj) {
  if (highlight_target == obj) {
    highlight_target = NULL;
  }
}

void Numpad::foreground_reset() {
  LOG_TRACE("resetting foreground");
  prev_was_empty = false;
  lv_textarea_set_text(input, "");
  lv_obj_clear_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(edit_cont);
  lv_keyboard_set_textarea(kb, input);
}

void Numpad::background() {
  lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(edit_cont);
  set_highlight_target(NULL);
  lv_textarea_set_text(input, "");
}
