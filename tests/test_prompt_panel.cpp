#include "config.h"
#include "guppyscreen.h"
#include "notification_banner.h"
#include "state.h"
#include "theme.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static lv_obj_t *find_label(lv_obj_t *parent, const char *text) {
  if (lv_obj_check_type(parent, &lv_label_class) &&
      std::strcmp(lv_label_get_text(parent), text) == 0) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *found = find_label(lv_obj_get_child(parent, i), text)) {
      return found;
    }
  }
  return nullptr;
}

static void macro_response(PromptPanel &prompt, const std::string &text) {
  json response = {{"params", json::array({text})}};
  prompt.handle_macro_response(response);
}

static void action(PromptPanel &prompt, const std::string &command) {
  macro_response(prompt, "// action:" + command);
}

static void test_prompt_errors(PromptPanel &prompt, lv_obj_t *box) {
  using Source = NotificationBanner::Source;
  using Level = NotificationBanner::Level;
  auto &banner = NotificationBanner::instance();

  // The runout error may arrive before prompt_begin or while the prompt is
  // being built. Showing the recovery prompt replaces that banner in both cases.
  for (bool error_before_begin : {true, false}) {
    if (error_before_begin) {
      macro_response(prompt, "!! Filament not detected");
    }
    action(prompt, "prompt_begin Print aborted");
    action(prompt, "prompt_text Please load filament.");
    action(prompt, "prompt_footer_button OK|RESPOND TYPE=command MSG=action:prompt_end");
    if (!error_before_begin) {
      macro_response(prompt, "!! Filament not detected");
    }
    assert(banner.visible(Source::Klipper));
    auto *overlay = lv_obj_get_parent(find_label(lv_layer_top(), "Filament not detected"));
    assert(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));

    action(prompt, "prompt_show");
    assert(find_label(box, "Please load filament."));
    assert(!banner.visible(Source::Klipper));
    assert(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
    assert(!find_label(box, "Filament not detected"));

    // A failed prompt button still needs feedback inside the prompt.
    macro_response(prompt, "!! Printer not homed");
    assert(find_label(box, "Printer not homed"));
    assert(!banner.visible(Source::Klipper));
    action(prompt, "prompt_end");
    assert(!find_label(box, "Printer not homed"));
    assert(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
  }

  // Standalone command errors still appear, and opening a prompt only clears
  // the Klipper error, preserving messages from other sources.
  macro_response(prompt, "!! Move out of range");
  assert(banner.visible(Source::Klipper));
  banner.show(Source::Display, "Heating", Level::Info);
  banner.show(Source::Mmu, "Filament jam", Level::Error);
  action(prompt, "prompt_begin Recovery");
  action(prompt, "prompt_show");
  assert(!banner.visible(Source::Klipper));
  assert(banner.visible(Source::Display));
  assert(banner.visible(Source::Mmu));
  action(prompt, "prompt_end");
  assert(!banner.visible(Source::Klipper));
  banner.clear_all();
}

int main(int argc, char **argv) {
  assert(Config::get_instance()->load("grumpyscreen.cfg"));
  lv_init();
  static lv_color_t pixels[800];
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, pixels, nullptr, 800);
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
  const bool phone = argc > 1 && std::strcmp(argv[1], "--phone") == 0;
  const bool tablet = argc > 1 && std::strcmp(argv[1], "--tablet") == 0;
  driver.hor_res = phone ? 2400 : tablet ? 2560 : 800;
  driver.ver_res = phone ? 1080 : tablet ? 1600 : 480;
  driver.dpi = LV_DPI_DEF * driver.hor_res / 480;
  driver.draw_buf = &buffer;
  auto *display = lv_disp_drv_register(&driver);
  // Model Android's uncapped font metrics; the desktop fonts stop at 28px.
  lv_font_t body_font = *Theme::scale_font(12);
  body_font.line_height = Theme::scale_r(12) * 5 / 4;
  lv_font_t header_font = *Theme::scale_font(18);
  header_font.line_height = Theme::scale_r(18) * 5 / 4;
  auto *theme = lv_theme_default_init(display, Theme::cfg_col("primary_colour"),
                                     Theme::cfg_col("secondary_colour"), true, &body_font);
  lv_disp_set_theme(display, theme);
  static lv_theme_t custom_theme = *theme;
  lv_theme_set_parent(&custom_theme, theme);
  lv_theme_set_apply_cb(&custom_theme, GuppyScreen::new_theme_apply_cb);
  lv_disp_set_theme(display, &custom_theme);

  KWebSocketClient ws(nullptr);
  std::mutex lock;
  auto &status = *new PrintStatusPanel(ws, lock, lv_scr_act());
  auto &prompt = *new PromptPanel(ws, lock, lv_scr_act(), status);
  auto *header = find_label(lv_scr_act(), "HEADER");
  assert(header);
  lv_obj_set_style_text_font(header, &header_font, 0);

  json probe = {{"manual_probe", {{"is_active", true}, {"z_position", 232.0},
                                {"z_position_lower", nullptr}, {"z_position_upper", nullptr}}}};
  State::get_instance()->set_data("printer_state", probe, "");
  json update = {{"params", json::array({probe})}};
  prompt.consume(update);
  auto *box = lv_obj_get_parent(header);
  auto *body = lv_obj_get_child(box, 1);
  lv_obj_update_layout(box);
  assert(lv_obj_get_height(box) <= Theme::popout_max_h());
  assert(lv_obj_get_scroll_bottom(body) <= 0);
  auto *table = lv_obj_get_child(body, 0);
  auto *last_row = lv_obj_get_child(table, -1);
  lv_area_t body_bounds, row_bounds, footer_bounds;
  lv_obj_get_coords(body, &body_bounds);
  lv_obj_get_coords(last_row, &row_bounds);
  assert(row_bounds.y2 <= body_bounds.y2);
  auto *footer = lv_obj_get_parent(lv_obj_get_parent(find_label(box, "Abort")));
  lv_obj_get_coords(footer, &footer_bounds);
  assert(footer_bounds.y1 > body_bounds.y2);
  assert(footer_bounds.y2 < driver.ver_res);

  action(prompt, "prompt_begin Long prompt");
  for (int i = 0; i < 30; ++i) {
    action(prompt, "prompt_text A line of instructions");
  }
  action(prompt, "prompt_footer_button Abort|ABORT");
  action(prompt, "prompt_show");
  auto *label = find_label(lv_scr_act(), "Long prompt");
  assert(label);
  assert(lv_obj_get_parent(label) == box);
  footer = lv_obj_get_parent(lv_obj_get_parent(find_label(box, " Abort")));
  lv_obj_update_layout(box);
  lv_obj_get_coords(footer, &footer_bounds);
  assert(footer_bounds.y2 < driver.ver_res);
  assert(lv_obj_get_height(box) <= Theme::popout_max_h());
  assert(lv_obj_get_scroll_bottom(body) > 0);
  assert(lv_obj_has_flag(body, LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_scroll_to_y(body, LV_COORD_MAX, LV_ANIM_OFF);
  assert(lv_obj_get_scroll_bottom(body) == 0);
  lv_area_t scrolled_footer_bounds;
  lv_obj_get_coords(footer, &scrolled_footer_bounds);
  assert(footer_bounds.y1 == scrolled_footer_bounds.y1);
  assert(footer_bounds.y2 == scrolled_footer_bounds.y2);
  action(prompt, "prompt_end");
  test_prompt_errors(prompt, box);
  std::puts("prompt panel tests passed");
}
