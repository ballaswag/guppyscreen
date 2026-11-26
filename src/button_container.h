#ifndef __BUTTON_CONTAINER_H__
#define __BUTTON_CONTAINER_H__

#include "lvgl/lvgl.h"

#include <array>
#include <functional>
#include <string>
class ButtonContainer {
 public:
  ButtonContainer(lv_obj_t *parent,
		  const void *btn_img,
		  const char *text,
		  lv_event_cb_t cb,
		  void *user_data,
		  const std::string &title_text = {},
		  const std::string &prompt_text = {},
		  const std::array<std::string, 2> &prompt_buttons = {});
  ~ButtonContainer();

  lv_obj_t *get_container();
  lv_obj_t *get_button();
  // Stand as tall as another control sharing this tile's row. The row is
  // content-sized in the panels that need this, so stretching into it would be
  // circular -- it is deriving its height from us. Measuring settles it, which
  // needs the layout resolved first, hence the root.
  void match_height(lv_obj_t *root, lv_obj_t *other);
  // Lay the tile out as icon over label using the theme's shared icon size:
  // no box, and the icon turns the accent colour when pressed.
  void use_plain();
  // A tile pinned to the parent's bottom-right corner, over the content:
  // the Back button on panels whose content is a list rather than a grid.
  void float_bottom_right();
  static int float_w();  // the width of such a tile
  void disable();
  void enable();
  void hide();
  // an icon-only tile: the label takes no room
  void hide_label();
  void show();
  // Keep the pressed visual visible and ignore input until the delay expires.
  bool start_pressed_transition(uint32_t duration_ms);

  void set_image(const void *img);
  void set_label(const char *text);
  void set_prompt_condition(std::function<bool()> condition);

  void handle_callback(lv_event_t *event);
  void handle_prompt();
  void handle_prompt_result(uint32_t clicked_btn);
  
  static void _handle_callback(lv_event_t *event) {
    ButtonContainer *button_container = (ButtonContainer*)event->user_data;
    button_container->handle_callback(event);
  };

 private:
  lv_obj_t *btn_cont;
  lv_obj_t *btn;
  lv_obj_t *label;
  std::string title_text;
  std::string prompt_text;
  std::array<std::string, 2> prompt_buttons;
  std::array<const char *, 3> prompt_button_map;
  bool dispatch_confirmed_click = false;
  std::function<bool()> prompt_condition;
  lv_timer_t *pressed_transition_timer = nullptr;

  void stack();
  void fit_icon();
  static void _handle_pressed_transition_timer(lv_timer_t *timer);
};

#endif // __BUTTON_CONTAINER_H__
