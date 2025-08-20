#include "prompt_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

// #define DEBUG_BOXES

static lv_style_t style_btn_grey;
static lv_style_t style_btn_blue;
static lv_style_t style_btn_red;
static lv_style_t style_btn_orange;
static lv_style_t style_btn_dark_grey;

PromptPanel::PromptPanel(KWebSocketClient &websocket_client,
  std::mutex &lock,
  lv_obj_t *parent)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , prompt_cont(lv_obj_create(lv_scr_act()))
  , header(lv_label_create(prompt_cont))
  , body_cont(lv_obj_create(prompt_cont))
  , body(lv_label_create(body_cont))
  , button_cont(lv_obj_create(body_cont))
  , footer_cont(lv_obj_create(prompt_cont))
{
  // PROMPT PANEL
  lv_obj_center(prompt_cont);
  lv_obj_set_size(prompt_cont, lv_pct(60), lv_pct(90));
  lv_obj_set_style_border_width(prompt_cont, 2, 0);
  lv_obj_set_style_border_color(prompt_cont, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_radius(prompt_cont, 16, 0);
  lv_obj_set_style_pad_all(prompt_cont, 8, 0);
  lv_obj_set_style_pad_row(prompt_cont, 0, 0);

#ifdef DEBUG_BOXES
  lv_obj_set_style_border_width(prompt_cont, 3, 0);
  lv_obj_set_style_border_color(prompt_cont, lv_palette_main(LV_PALETTE_RED), 0);
#endif

  static lv_coord_t grid_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_rows[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(prompt_cont, grid_cols, grid_rows);

  lv_obj_set_grid_cell(header, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  lv_obj_set_grid_cell(body_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(footer_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_END, 2, 1);

  // HEADER
  lv_label_set_long_mode(header, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(header, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_pad_bottom(header, 4, 0);
  lv_obj_set_style_border_width(header, 2, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, lv_palette_main(LV_PALETTE_GREY), 0);

#ifdef DEBUG_BOXES
  lv_obj_set_style_border_width(header, 2, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_border_color(header, lv_palette_main(LV_PALETTE_GREEN), 0);
#endif

  // BODY CONTAINER
  lv_obj_set_style_pad_all(body_cont, 0, 0);
  lv_obj_set_style_pad_ver(body_cont, 4, 0);
  lv_obj_set_style_border_width(body_cont, 2, 0);
  lv_obj_set_style_border_side(body_cont, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(body_cont, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_flex_flow(body_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

#ifdef DEBUG_BOXES
  lv_obj_set_style_border_width(body_cont, 2, 0);
  lv_obj_set_style_border_side(body_cont, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_border_color(body_cont, lv_palette_main(LV_PALETTE_YELLOW), 0);
#endif

  // BODY TEXT
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

#ifdef DEBUG_BOXES
  lv_obj_set_style_border_width(body, 2, 0);
  lv_obj_set_style_border_side(body, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_border_color(body, lv_palette_main(LV_PALETTE_PURPLE), 0);
#endif

  // BUTTONS
  lv_obj_set_style_pad_all(button_cont, 0, 0);
  lv_obj_set_style_pad_top(button_cont, 4, 0);
  lv_obj_set_style_pad_column(button_cont, 8, 0);
  lv_obj_set_style_pad_row(button_cont, 8, 0);
  lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_size(button_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(button_cont, LV_OBJ_FLAG_SCROLLABLE);

#ifdef DEBUG_BOXES
  lv_obj_set_style_border_width(button_cont, 2, 0);
  lv_obj_set_style_border_side(button_cont, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_border_color(button_cont, lv_palette_main(LV_PALETTE_TEAL), 0);
#endif

  // FOOTER
  lv_obj_set_height(footer_cont, LV_SIZE_CONTENT);
  lv_obj_set_width(footer_cont, LV_PCT(100));
  lv_obj_set_flex_flow(footer_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer_cont,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(footer_cont, 0, 0);
  lv_obj_set_style_pad_top(footer_cont, 8, 0);
  lv_obj_set_style_pad_column(footer_cont, 8, 0);

#ifdef DEBUG_BOXES
  lv_obj_set_style_border_width(footer_cont, 2, 0);
  lv_obj_set_style_border_side(footer_cont, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_border_color(footer_cont, lv_palette_main(LV_PALETTE_BLUE), 0);
#endif

  // BUTTON STYLES
  auto init_btn_style = [](lv_style_t &st, lv_color_t c) {
    lv_style_init(&st);
    lv_style_set_bg_color(&st, c);
    lv_style_set_bg_opa(&st, LV_OPA_COVER);
    lv_style_set_pad_all(&st, 15);
    };
  init_btn_style(style_btn_grey, lv_palette_main(LV_PALETTE_GREY));
  init_btn_style(style_btn_blue, lv_palette_main(LV_PALETTE_BLUE));
  init_btn_style(style_btn_red, lv_palette_main(LV_PALETTE_RED));
  init_btn_style(style_btn_orange, lv_palette_main(LV_PALETTE_ORANGE));
  init_btn_style(style_btn_dark_grey, lv_palette_darken(LV_PALETTE_GREY, 1));

  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);

  ws.register_notify_update(this);
  ws.register_method_callback(
    "notify_gcode_response",
    "PromptPanel",
    [this](json &d) { consume(d); }
  );
}

PromptPanel::~PromptPanel() {
  if (prompt_cont) lv_obj_del(prompt_cont);
  ws.unregister_notify_update(this);
}

void PromptPanel::foreground() {
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(prompt_cont);
}

void PromptPanel::background() {
  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(prompt_cont);
}

void PromptPanel::on_button_click(lv_event_t *e) {
  auto btn = lv_event_get_target(e);
  auto pcmd = static_cast<std::string *>(btn->user_data);
  auto panel = static_cast<PromptPanel *>(lv_event_get_user_data(e));
  if (pcmd && !pcmd->empty()) {
    panel->ws.gcode_script(*pcmd);
    // consider `delete pcmd; btn->user_data = nullptr;` if one‐shot
  }
}

void PromptPanel::consume(json &j) {
  auto &v = j["/params/0"_json_pointer];
  if (!v.is_string()) return;
  auto resp = v.get<std::string>();
  if (resp.rfind("// action:", 0) != 0) return;
  auto cmd = resp.substr(10);

  std::lock_guard<std::mutex> guard(lv_lock);

  if (cmd.rfind("prompt_begin", 0) == 0) {
    lv_label_set_text(header, (std::string(LV_SYMBOL_WARNING) + " " + cmd.substr(13)).c_str());

  } else if (cmd.rfind("prompt_text", 0) == 0) {
    lv_label_set_text(body, cmd.substr(12).c_str());

  } else if (cmd.rfind("prompt_button", 0) == 0 || cmd.rfind("prompt_footer_button", 0) == 0) {
    if (cmd.rfind("prompt_button_group", 0) == 0) return;
    auto parts = KUtils::split(cmd, '|');
    auto label = parts[0].substr(parts[0].find("button") + 6);
    auto gcode = parts.size() > 1 ? parts[1] : "";
    auto type = parts.size() > 2 ? parts[2] : "default";

    lv_obj_t *parent = (cmd.rfind("prompt_footer_button", 0) == 0) ? footer_cont : button_cont;
    auto *b = lv_btn_create(parent);
    if (type == "warning")      lv_obj_add_style(b, &style_btn_orange, 0);
    else if (type == "error")   lv_obj_add_style(b, &style_btn_red, 0);
    else if (type == "info")    lv_obj_add_style(b, &style_btn_blue, 0);
    else if (type == "primary") lv_obj_add_style(b, &style_btn_blue, 0);
    else                        lv_obj_add_style(b, &style_btn_dark_grey, 0);

    auto *lbl = lv_label_create(b);
    lv_label_set_text(lbl, label.c_str());
    lv_obj_center(lbl);

    b->user_data = new std::string(gcode);
    lv_obj_add_event_cb(b, PromptPanel::on_button_click, LV_EVENT_CLICKED, this);

  } else if (cmd == "prompt_show") {
    lv_obj_update_layout(prompt_cont);
    foreground();

  } else if (cmd == "prompt_end") {
    lv_label_set_text(header, "");
    lv_label_set_text(body, "");
    lv_obj_clean(button_cont);
    lv_obj_clean(footer_cont);
    lv_obj_scroll_to_y(body_cont, 0, LV_ANIM_OFF);
    background();
  }
}
