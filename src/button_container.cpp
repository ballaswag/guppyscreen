#include "button_container.h"
#include "simple_dialog.h"
#include "theme.h"

#include <algorithm>

using namespace Theme;

namespace {
  void handle_button_container_dialog_result(lv_obj_t *, uint32_t clicked_btn, void *user_data) {
    ButtonContainer *button_container = static_cast<ButtonContainer *>(user_data);
    button_container->handle_prompt_result(clicked_btn);
  }
}

ButtonContainer::ButtonContainer(lv_obj_t *parent,
				 const void *btn_img,
				 const char *text,
				 lv_event_cb_t cb,
				 void* user_data,
				 const std::string &title,
				 const std::string &prompt,
				 const std::array<std::string, 2> &buttons)
  : btn_cont(lv_obj_create(parent))
  , btn(lv_img_create(btn_cont))
  , label(lv_label_create(btn_cont))
  , title_text(title)
  , prompt_text(prompt)
  , prompt_buttons(buttons)
{
  // no local padding here: a local style outranks the shared card/tile styles
  // use_plain() adds, and would flatten their padding to nothing
  lv_obj_add_style(btn_cont, &styles().row, 0);
  lv_obj_set_size(btn_cont, scale_w(90), LV_SIZE_CONTENT);

  lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_img_set_src(btn, btn_img);
  // the recolours are the same two every icon in the UI wears
  lv_img_set_size_mode(btn, LV_IMG_SIZE_MODE_REAL);
  style_icon(btn);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);

  if (cb != NULL) {
    lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_RELEASED, this);
    if (!prompt_text.empty()) {
      lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_CLICKED, this);
      lv_obj_add_event_cb(btn_cont, &ButtonContainer::_handle_callback, LV_EVENT_LONG_PRESSED, this);
    }
    lv_obj_add_event_cb(btn_cont, cb, LV_EVENT_CLICKED, user_data);
  }

  lv_label_set_text(label, text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, col(DISABLED), LV_STATE_DISABLED);

  lv_obj_align_to(label, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

ButtonContainer::~ButtonContainer() {
  if (pressed_transition_timer != nullptr) {
    lv_timer_del(pressed_transition_timer);
  }
}

lv_obj_t *ButtonContainer::get_container() {
  return btn_cont;
}

// An icon-and-label action: no box of its own, the icon turning the accent
// colour under a finger is the whole feedback, which is how every tile in
// grumpyscreen has always looked.
void ButtonContainer::use_plain() {
  lv_obj_add_style(btn_cont, &styles().tile, 0);

  // No size is set: a grid cell with LV_GRID_ALIGN_STRETCH sizes the tile, a
  // floating tile keeps its natural size, and fit_icon() adapts the icon.
  stack();
  lv_obj_add_event_cb(btn_cont, [](lv_event_t *e) {
    static_cast<ButtonContainer *>(lv_event_get_user_data(e))->fit_icon();
  }, LV_EVENT_SIZE_CHANGED, this);
}

void ButtonContainer::stack() {
  lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_align(btn, LV_ALIGN_DEFAULT);
  lv_obj_set_align(label, LV_ALIGN_DEFAULT);
}

void ButtonContainer::match_height(lv_obj_t *root, lv_obj_t *other) {
  lv_obj_update_layout(root);
  lv_obj_set_height(btn_cont, lv_obj_get_height(other));
}

void ButtonContainer::float_bottom_right() {
  use_plain();
  lv_obj_add_flag(btn_cont, LV_OBJ_FLAG_FLOATING);
  // a fixed square, wide enough for a one-word label on one line, so Back and
  // Refresh are the same shape whatever their text and the caller can place
  // one beside the other without measuring; fit_icon sizes the icon to the rest
  lv_obj_set_size(btn_cont, float_w(), float_w());
  lv_obj_set_style_pad_all(btn_cont, gap(), 0);
  lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

int ButtonContainer::float_w() { return scale_w(76); }

// The theme sets the shared icon size. Available space still bounds the image
// so a small control or a longer label cannot make it clip.
void ButtonContainer::fit_icon() {
  const lv_img_dsc_t *src = static_cast<const lv_img_dsc_t *>(lv_img_get_src(btn));
  if (src == NULL || lv_img_src_get_type(src) != LV_IMG_SRC_VARIABLE) {
    return;
  }
  lv_coord_t avail_h = lv_obj_get_content_height(btn_cont);
  if (!lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
    avail_h -= lv_obj_get_height(label) + lv_obj_get_style_pad_row(btn_cont, 0);
  }
  fit_action_icon(btn, lv_obj_get_content_width(btn_cont), avail_h);
}

lv_obj_t *ButtonContainer::get_button() {
  return btn;
}

void ButtonContainer::disable() {
  if (pressed_transition_timer != nullptr) {
    lv_timer_del(pressed_transition_timer);
    pressed_transition_timer = nullptr;
  }
  lv_obj_clear_state(btn, LV_STATE_PRESSED);
  lv_obj_add_state(btn, LV_STATE_DISABLED);
  lv_obj_add_state(btn_cont, LV_STATE_DISABLED);
  lv_obj_add_state(label, LV_STATE_DISABLED);
}

void ButtonContainer::enable() {
  lv_obj_clear_state(btn, LV_STATE_DISABLED);
  lv_obj_clear_state(btn_cont, LV_STATE_DISABLED);
  lv_obj_clear_state(label, LV_STATE_DISABLED);
  // the tile is the click target; the icon must stay non-clickable or taps
  // on it land on the image and go nowhere
  lv_obj_add_flag(btn_cont, LV_OBJ_FLAG_CLICKABLE);
}

void ButtonContainer::hide() {
  lv_obj_add_flag(btn_cont, LV_OBJ_FLAG_HIDDEN);
}

void ButtonContainer::hide_label() {
  lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_pad_row(btn_cont, 0, 0);
}

void ButtonContainer::show() {
  lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_HIDDEN);
}

bool ButtonContainer::start_pressed_transition(uint32_t duration_ms) {
  if (pressed_transition_timer != nullptr) {
    return false;
  }

  lv_obj_add_state(btn, LV_STATE_PRESSED);
  lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_CLICKABLE);
  pressed_transition_timer = lv_timer_create(&ButtonContainer::_handle_pressed_transition_timer,
                                             duration_ms, this);
  lv_timer_set_repeat_count(pressed_transition_timer, 1);
  return true;
}

void ButtonContainer::_handle_pressed_transition_timer(lv_timer_t *timer) {
  ButtonContainer *button_container = static_cast<ButtonContainer *>(timer->user_data);
  button_container->pressed_transition_timer = nullptr;
  lv_obj_clear_state(button_container->btn, LV_STATE_PRESSED);
  lv_obj_add_flag(button_container->btn_cont, LV_OBJ_FLAG_CLICKABLE);
}

void ButtonContainer::set_label(const char *text) {
  lv_label_set_text(label, text);
}

void ButtonContainer::set_prompt_condition(std::function<bool()> condition) {
  prompt_condition = std::move(condition);
}

void ButtonContainer::set_image(const void *img) {
  // Setting the same source still redraws the button
  if (lv_img_get_src(btn) == img) {
    return;
  }
  lv_img_set_src(btn, img);
}

void ButtonContainer::handle_callback(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_obj_add_state(btn, LV_STATE_PRESSED);
  } else if (code == LV_EVENT_RELEASED) {
    lv_obj_clear_state(btn, LV_STATE_PRESSED);
  } else if (code == LV_EVENT_CLICKED && !dispatch_confirmed_click) {
    lv_event_stop_processing(e);
    // with prompting turned off a tap does nothing, only a long press fires
    if (!prompt_condition || prompt_condition()) {
      handle_prompt();
    }
  } else if (code == LV_EVENT_LONG_PRESSED && prompt_condition && !prompt_condition()) {
    dispatch_confirmed_click = true;
    lv_event_send(btn_cont, LV_EVENT_CLICKED, NULL);
    dispatch_confirmed_click = false;
  }
}

void ButtonContainer::handle_prompt() {
  prompt_button_map = {prompt_buttons[0].c_str(), prompt_buttons[1].c_str(), ""};

  SimpleDialogOptions options{};
  options.buttons = prompt_button_map.data();
  options.error = true;
  options.highlighted_button_idx = 1;
  options.result_cb = handle_button_container_dialog_result;
  options.user_data = this;
  create_configurable_dialog(lv_scr_act(), title_text.c_str(), prompt_text.c_str(), options);
}

void ButtonContainer::handle_prompt_result(uint32_t clicked_btn) {
  if (clicked_btn == 1) {
    dispatch_confirmed_click = true;
    lv_event_send(btn_cont, LV_EVENT_CLICKED, NULL);
    dispatch_confirmed_click = false;
  }
}
