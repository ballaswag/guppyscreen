#include "prompt_panel.h"
#include "notification_banner.h"
#include "config.h"
#include "print_status_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "theme.h"

#include <algorithm>

using namespace Theme;

// a colour as label recolour wants it: "#RRGGBB text#"
static std::string hex(lv_color_t c) {
  return fmt::format("{:06X}", lv_color_to32(c) & 0xFFFFFF);
}

// uncomment for helper boxes
// #define DEBUG_LINES

// a row of prompt buttons; the footer and every button group
static lv_obj_t *button_row(lv_obj_t *parent, lv_flex_align_t align) {
  lv_obj_t *row = create_row(parent);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

// Klipper names a prompt button secondary/info/primary/warning/error, which
// are the theme roles under other names -- so a danger_colour set in the
// config turns up on a klipper prompt too, without this panel ever knowing
// what colour it is. A default button keeps the flat button look.
static void colour_prompt_btn(lv_obj_t *btn, const std::string &type) {
  lv_color_t c;
  if (type == "primary") {
    c = theme_primary();
  }
  else if (type == "secondary") {
    c = theme_secondary();
  }
  else if (type == "info" || type == "warning") {
    c = col(WARNING);
  }
  else if (type == "error") {
    c = col(DANGER);
  }
  else {
    return;
  }
  lv_obj_set_style_bg_color(btn, c, 0);
  lv_obj_set_style_text_color(btn, col(ON_PRIMARY), 0);
}

PromptPanel::PromptPanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent, PrintStatusPanel &print_status_panel)
    : NotifyConsumer(lock)
    , ws(websocket_client)
    , print_status_panel(print_status_panel)
    , prompt_cont(lv_obj_create(lv_scr_act()))
    , box(lv_obj_create(prompt_cont))
    , flex(create_row(box))
    , header(lv_label_create(box))
    , footer_cont(button_row(box, LV_FLEX_ALIGN_END))
    , numpad(prompt_cont)
{
  // a popout like the screen's own: the page dimmed behind a box sized to the
  // prompt, header over the body over the buttons
  lv_obj_set_size(prompt_cont, lv_pct(100), lv_pct(100));
  lv_obj_add_style(prompt_cont, &styles().popout, 0);
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_add_style(box, &styles().popout_box, 0);
  lv_obj_set_size(box, popout_w(), LV_SIZE_CONTENT);
  lv_obj_center(box);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_to_index(header, 0);

  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_style_text_font(header, scale_font(18), 0);
  lv_obj_set_style_pad_bottom(header, gap(), 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(header, border_w(), 0);
  lv_obj_set_style_border_color(header, col(BORDER), 0);

  lv_obj_set_size(flex, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(flex, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(flex, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  manage_scroll(flex);

#ifdef DEBUG_LINES
  // for debugging
  lv_obj_set_style_border_width(header, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(header, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(flex, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(flex, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(footer_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(footer_cont, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif

  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "MainPanel",[this](json& d) { this->handle_macro_response(d); });

  // create header
  lv_label_set_text(header, "HEADER");

  background(); // hide ourselves
}

// klipper's own dialogs send no action:prompt, they are printer status the
// way fluidd and mainsail open them: manual_probe (PROBE_CALIBRATE,
// Z_ENDSTOP_CALIBRATE and the like) and screws_tilt_adjust
// (SCREWS_TILT_CALCULATE)
void PromptPanel::consume(json &j) {
  auto &status = j["/params/0"_json_pointer];
  if (!status.is_object()) {
    return;
  }

  if (status.contains("display_status") && status["display_status"].contains("message")) {
    auto &message = status["display_status"]["message"];
    std::lock_guard<std::mutex> lock(lv_lock);
    update_display_message(message.is_string() ? message.template get<std::string>() : "");
  }

  bool manual_probe = status.contains("manual_probe");
  bool screws_tilt = status.contains("screws_tilt_adjust") && status["screws_tilt_adjust"].contains("results");
  if (!manual_probe && !screws_tilt) {
    return;
  }

  // State consumes before us, so it holds the whole object, not just what changed
  State *state = State::get_instance();
  std::lock_guard<std::mutex> lock(lv_lock);
  if (manual_probe) {
    json mp = state->get_data("/printer_state/manual_probe"_json_pointer);
    update_manual_probe(mp);
  }
  if (screws_tilt) {
    show_screws_tilt(status["screws_tilt_adjust"]);
  }
}

void PromptPanel::show_banner(const std::string &text, bool error) {
  using Source = NotificationBanner::Source;
  using Level = NotificationBanner::Level;
  auto &banner = NotificationBanner::instance();
  // A new command error can repeat the same text after dismissal.
  if (error) {
    banner.clear(Source::Klipper);
    banner.show(Source::Klipper, text, Level::Error);
  } else {
    // Clear the printer's stored message on dismissal so a later M117 with
    // identical text is a new message. Its status update clears our copy too.
    banner.show(Source::Display, text, Level::Info, [this]() {
      ws.gcode_script("M117");
    });
  }
}

// M117: an empty message clears it, and an error on screen is not replaced.
void PromptPanel::update_display_message(const std::string &message) {
  auto &banner = NotificationBanner::instance();
  if (banner.visible(NotificationBanner::Source::Klipper)) {
    return;
  }
  show_banner(message, false);
}

// screwN_name from the printer config the way fluidd shows it ("front_left"
// -> "Front Left"), or a number when it is missing
static std::string screw_name(json &settings, int n) {
  auto &name = settings[fmt::format("screw{}_name", n)];
  if (!name.is_string() || name.template get<std::string>().empty()) {
    return fmt::format("Screw {}", n);
  }
  std::string pretty;
  bool word_start = true;
  for (char c : name.template get<std::string>()) {
    if (c == '_' || c == ' ') {
      word_start = true;
      continue;
    }
    if (word_start && !pretty.empty()) {
      pretty += ' ';
    }
    pretty += word_start ? (char)toupper(c) : c;
    word_start = false;
  }
  return pretty;
}

// like fluidd, a klipper dialog does not open by itself mid-print
static bool printing() {
  json state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  return state.is_string() && state.template get<std::string>() == "printing";
}

// a column of rows; the popout body scrolls it when it does not fit
static lv_obj_t *create_table(lv_obj_t *parent) {
  lv_obj_t *table = create_row(parent);
  lv_obj_set_size(table, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
  return table;
}

static lv_obj_t *create_table_row(lv_obj_t *table) {
  lv_obj_t *row = create_row(table);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, scale_r(4), 0);
  return row;
}

// a label that takes an equal share of its row, centred
static lv_obj_t *create_cell(lv_obj_t *row, const char *text) {
  lv_obj_t *cell = lv_label_create(row);
  lv_obj_set_flex_grow(cell, 1);
  lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_recolor(cell, true);
  lv_label_set_text(cell, text);
  return cell;
}

// fluidd's z field: its label over the value, empty until there is one
static std::string z_field(const char *label, json &z) {
  return fmt::format("#{} {}#\n{}", hex(col(TEXT_DIM)), label,
                     z.is_number() ? fmt::format("{:.3f}", z.template get<double>()) : "");
}

void PromptPanel::update_manual_probe(json &mp) {
  bool active = mp.is_object() && mp.value("is_active", false);
  if (!active) {
    if (klipper_dialog == KlipperDialog::MANUAL_PROBE) {
      close();
    }
    return;
  }

  bool opening = klipper_dialog != KlipperDialog::MANUAL_PROBE;
  if (opening) {
    if (printing()) {
      return;
    }
    open(KlipperDialog::MANUAL_PROBE, "Manual Probe");
    lv_obj_t *table = create_table(flex);

    lv_obj_t *z_row = create_table_row(table);
    z_min_label = create_cell(z_row, "");
    info_label = create_cell(z_row, "");
    z_max_label = create_cell(z_row, "");

    // klipper's bisect steps, between the heights already tried
    lv_obj_t *bisect = create_table_row(table);
    add_button(bisect, "--", "TESTZ Z=--", "primary");
    add_button(bisect, "-", "TESTZ Z=-", "primary");
    add_button(bisect, "+", "TESTZ Z=+", "primary");
    add_button(bisect, "++", "TESTZ Z=++", "primary");

    // fluidd's offsets (1, 0.1 and its default z adjust distances) to pick
    // from, and a - and + that move by the one picked
    lv_obj_t *steps = create_table_row(table);
#ifdef GUPPY_SMALL_SCREEN
    // 480x272: - and + share the step row so it all fits without scrolling
    lv_obj_t *move = steps;
    add_button(move, LV_SYMBOL_MINUS, "TESTZ Z=-{step}", "primary");
#endif
    for (const char *step : {"1", "0.1", "0.05", "0.025", "0.01", "0.005"}) {
      lv_obj_t *btn = add_button(steps, step, "", "none");
      lv_obj_remove_event_cb(btn, _handle_callback);
      lv_obj_add_event_cb(btn, _handle_step, LV_EVENT_CLICKED, this);
      lv_obj_set_style_bg_color(btn, theme_primary(), LV_STATE_CHECKED);
      lv_obj_set_style_text_color(btn, col(ON_PRIMARY), LV_STATE_CHECKED);
      if (probe_step == step) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
      }
    }
#ifndef GUPPY_SMALL_SCREEN
    // 800x480: - and + get their own row
    lv_obj_t *move = create_table_row(table);
    add_button(move, LV_SYMBOL_MINUS, "TESTZ Z=-{step}", "primary");
#endif
    add_button(move, LV_SYMBOL_PLUS, "TESTZ Z=+{step}", "primary");

    add_button(footer_cont, "Abort", "ABORT", "none");
    add_button(footer_cont, "Accept", "ACCEPT", "primary");
  }

  lv_label_set_text(z_min_label, z_field("Z Min", mp["z_position_lower"]).c_str());
  lv_label_set_text(info_label, z_field("Z", mp["z_position"]).c_str());
  lv_label_set_text(z_max_label, z_field("Z Max", mp["z_position_upper"]).c_str());

  if (opening) {
    fit_manual_probe(lv_obj_get_child(flex, 0));
  }
}

void PromptPanel::fit_manual_probe(lv_obj_t *table) {
  fit_box();
  const int overflow = lv_obj_get_scroll_bottom(flex);
  if (overflow <= 0) {
    return;
  }
  // Android's fonts keep scaling beyond the printer fonts' 28px limit. Fit
  // the control rows below the Z readout before resorting to scrolling.
  const int rows = lv_obj_get_child_cnt(table) - 1;
  const int height = std::max(scale_r(32), touch_h() - (overflow + rows - 1) / rows);
  for (uint32_t i = 1; i < lv_obj_get_child_cnt(table); ++i) {
    lv_obj_t *row = lv_obj_get_child(table, i);
    for (uint32_t b = 0; b < lv_obj_get_child_cnt(row); ++b) {
      lv_obj_set_height(lv_obj_get_child(row, b), height);
    }
  }
  fit_box();
}

void PromptPanel::show_screws_tilt(json &st) {
  if (!Config::get_instance()->get<bool>("/ui/show_screws_tilt", true)) {
    return;
  }

  // MAX_DEVIATION makes it a check (a print start), not something to read off
  json max_deviation = State::get_instance()->get_data("/printer_state/screws_tilt_adjust/max_deviation"_json_pointer);
  auto &results = st["results"];
  if (!max_deviation.is_null() || !results.is_object() || results.empty() || printing()) {
    return;
  }

  json settings = State::get_instance()->get_data("/printer_state/configfile/settings/screws_tilt_adjust"_json_pointer);
  double base_z = 0;
  for (auto &r : results) {
    if (r.value("is_base", false)) {
      base_z = r.value("z", 0.0);
    }
  }

  open(KlipperDialog::SCREWS_TILT, "Screws Tilt Adjust");

  // fluidd's table: name over X/Y, Z under how far it is off the base, and the
  // turn as klipper gives it (CW 00:15), green under 6 minutes
  lv_obj_t *table = create_table(flex);
  for (int n = 1; results.contains(fmt::format("screw{}", n)); n++) {
    auto &r = results[fmt::format("screw{}", n)];
    auto &xy = settings[fmt::format("screw{}", n)];
    double z = r.value("z", 0.0);

    lv_obj_t *row = create_table_row(table);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_style_text_font(name, scale_font(16), 0);
    lv_label_set_recolor(name, true);
    lv_label_set_text(name, fmt::format("{}\n#{} X = {}, Y = {}#", screw_name(settings, n),
                                        hex(col(TEXT_DIM)),
                                        xy.is_array() && xy.size() > 1 ? xy[0].template get<double>() : 0.0,
                                        xy.is_array() && xy.size() > 1 ? xy[1].template get<double>() : 0.0).c_str());

    lv_obj_t *zl = lv_label_create(row);
    lv_obj_set_style_text_font(zl, scale_font(16), 0);
    lv_obj_set_style_text_align(zl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_hor(zl, gap() * 2, 0);
    lv_label_set_recolor(zl, true);
    lv_label_set_text(zl, fmt::format("#{} ({}{:.4f})#\n{:.4f}", hex(col(TEXT_DIM)),
                                      z - base_z < 0 ? "" : "+", z - base_z, z).c_str());

    lv_obj_t *chip = create_row(row);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(chip, gap(), 0);
    lv_obj_set_style_radius(chip, gap(), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);

    std::string adjust = r.value("adjust", "00:00");
    if (r.value("is_base", false)) {
      lv_obj_set_style_bg_color(chip, col(RAISED), 0);
    } else {
      int minutes = std::stoi(adjust.substr(0, 2)) * 60 + std::stoi(adjust.substr(3, 2));
      lv_obj_set_style_bg_color(chip, minutes < 6 ? col(SUCCESS) : col(DANGER), 0);
    }
    lv_obj_t *adj = lv_label_create(chip);
    lv_obj_set_style_text_font(adj, scale_font(16), 0);
    lv_label_set_text(adj, r.value("is_base", false) ? "base"
                           : fmt::format("{} {}", r.value("sign", ""), adjust).c_str());
  }

  add_button(footer_cont, "Retry", "SCREWS_TILT_CALCULATE", "none");
  add_button(footer_cont, "Accept", "", "primary");

  fit_box();
}

PromptPanel::~PromptPanel() {
  if (prompt_cont != NULL) {
    lv_obj_del(prompt_cont);
    prompt_cont = NULL;
  }

  ws.unregister_notify_update(this);
}

void PromptPanel::foreground() {
  lv_obj_move_foreground(prompt_cont);
}

void PromptPanel::background() {
  lv_obj_move_background(prompt_cont);
}

// a prompt taller than the screen takes the full height and scrolls its body,
// so the header and the footer buttons stay in view
void PromptPanel::fit_box() {
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_height(flex, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(flex, 0);
  lv_obj_update_layout(box);
  if (lv_obj_get_height(box) >= popout_max_h()) {
    lv_obj_set_height(box, popout_max_h());
    lv_obj_set_flex_grow(flex, 1);
    lv_obj_update_layout(box);
  }
  lv_obj_scroll_to_y(flex, 0, LV_ANIM_OFF);
  refresh_scroll(flex);
}

void PromptPanel::open(KlipperDialog d, const std::string &title) {
  if (klipper_dialog == KlipperDialog::NONE) {
    restore_print_status_foreground_ = print_status_panel.is_foreground();
  }
  clear();
  klipper_dialog = d;
  lv_label_set_text(header, title.c_str());
  foreground();
}

void PromptPanel::clear() {
  lv_obj_clean(footer_cont);
  lv_obj_clean(flex);
  button_group_cont = NULL;
  info_label = NULL;
  z_min_label = NULL;
  z_max_label = NULL;
  klipper_dialog = KlipperDialog::NONE;
  numpad.background();
  numpad_cmd.clear();
  prompt_shown = false;
  error_label = NULL;
}

void PromptPanel::close() {
  background();

  if (restore_print_status_foreground_) {
    print_status_panel.foreground();
  }
  restore_print_status_foreground_ = false;

  clear();
}

lv_obj_t *PromptPanel::add_text(const std::string &text) {
  lv_obj_t *textfield = lv_label_create(flex);
  lv_obj_set_size(textfield, lv_pct(100), LV_SIZE_CONTENT);
  lv_label_set_long_mode(textfield, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(textfield, scale_r(3), 0);
  lv_label_set_text(textfield, text.c_str());
#ifdef DEBUG_LINES
  lv_obj_set_style_border_width(textfield, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(textfield, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(textfield, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
  return textfield;
}

// footer actions sit right, sized to their label like the screen's dialogs; a
// button on its own in the body is 60% wide; buttons in a row (a button group,
// a dialog row) share it evenly, like keys
lv_obj_t *PromptPanel::add_button(lv_obj_t *parent, const std::string &text, const std::string &cmd, const std::string &type) {
  lv_obj_t *btn = lv_btn_create(parent);
  if (parent == footer_cont) {
    lv_obj_set_size(btn, LV_SIZE_CONTENT, touch_h());
    lv_obj_set_style_min_width(btn, scale_w(96), 0);
    lv_obj_set_style_pad_hor(btn, gap() * 2, 0);
  } else if (parent == flex) {
    lv_obj_set_size(btn, lv_pct(60), touch_h());
  } else {
    lv_obj_set_size(btn, 1, touch_h());
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_add_style(btn, &styles().key, 0);
  }
  lv_obj_add_style(btn, &styles().text_btn, 0);
  if (type == "none") {
    lv_obj_add_style(btn, &styles().text_btn_pressed, LV_STATE_PRESSED);
  }
  lv_obj_t *label = lv_label_create(btn);
  // a hidden label is abused to transfer the command and auto-clean it
  lv_obj_t *command = lv_label_create(btn);
  lv_obj_set_size(command, 1, 1);
  lv_obj_add_flag(command, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(label, text.c_str());
  lv_label_set_text(command, cmd.c_str());
  if (cmd.find("=NUMPAD") != std::string::npos) {
    numpad_cmd = cmd;
  }
  lv_obj_center(label);
  colour_prompt_btn(btn, type);
  lv_obj_add_event_cb(btn, _handle_callback, LV_EVENT_CLICKED, this);
  return btn;
}

void PromptPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);

  lv_obj_t *label = lv_obj_get_child(btn, 0);
  lv_obj_t *command = lv_obj_get_child(btn, 1);
  // check if btn in command map
  LOG_DEBUG("handle event");

  if (btn == NULL) {
    LOG_DEBUG("no button found");
  }

  if (label != NULL) {
    LOG_DEBUG("button: {}", lv_label_get_text(label));
  }

  if (command != NULL) {
    std::string cmd = lv_label_get_text(command);
    // the manual probe's - and + move by the step picked above them
    size_t step = cmd.find("{step}");
    if (step != std::string::npos) {
      cmd.replace(step, strlen("{step}"), probe_step);
    }
    LOG_DEBUG("button: {}", cmd);
    if (klipper_dialog == KlipperDialog::SCREWS_TILT) {
      // klipper never ends the results, the dialog goes on any button; not
      // from inside this button's own click
      lv_async_call([](void *panel) { ((PromptPanel *)panel)->close(); }, this);
    }
    if (cmd.find("=NUMPAD") != std::string::npos) {
      open_numpad(cmd);
    } else if (!cmd.empty()) {
      ws.gcode_script(cmd);
    }
  }
}

// A klipper prompt has no number entry, so a button command with a
// NAME=NUMPAD param opens the numpad, and the number replaces NUMPAD
void PromptPanel::open_numpad(std::string cmd) {
  numpad.set_callback([this, cmd](double v) mutable {
    cmd.replace(cmd.find("=NUMPAD") + 1, strlen("NUMPAD"), fmt::format("{:g}", v));
    ws.gcode_script(cmd);
  });
  numpad.cover_from(0);
  numpad.foreground_reset();
}

// a manual probe step: the one picked shows checked, the others not
void PromptPanel::handle_step(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  lv_obj_t *row = lv_obj_get_parent(btn);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); i++) {
    lv_obj_clear_state(lv_obj_get_child(row, i), LV_STATE_CHECKED);
  }
  lv_obj_add_state(btn, LV_STATE_CHECKED);
  probe_step = lv_label_get_text(lv_obj_get_child(btn, 0));
  LOG_DEBUG("manual probe step {}", probe_step);
}

void PromptPanel::handle_macro_response(json &j) {
  if (get_log_level() <= LogLevel::TRACE) {
    LOG_TRACE("macro response: {}", j.dump());
  }
  auto &v = j["/params/0"_json_pointer];

  if (!v.is_null()) {
    LOG_TRACE("data found");
    std::string resp = v.template get<std::string>();
    std::lock_guard<std::mutex> lock(lv_lock);
    LOG_TRACE("data: {}", resp);

    // a klipper error while the prompt is up shows in it, otherwise a button
    // whose command fails only says so in the console
    if (resp.find("!! ", 0) == 0 && prompt_shown) {
      if (error_label == NULL) {
        error_label = lv_label_create(flex);
        lv_obj_set_size(error_label, lv_pct(100), LV_SIZE_CONTENT);
        lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(error_label, col(DANGER), 0);
      }
      lv_label_set_text(error_label, resp.substr(3).c_str());
      fit_box();
      return;
    }

    // any other klipper error (a door opened during a print, a failed
    // command) shows in a banner over the top of the screen, a newer one
    // replacing the one showing. The screen below stays usable.
    if (resp.find("!! ", 0) == 0) {
      show_banner(resp.substr(3), true);
      return;
    }

    if (resp.find("// action:", 0) == 0) {
      // it is an action
      std::string command = resp.substr(10);
      LOG_DEBUG("action: {}", command);

      if (command.find("prompt_begin") == 0) {
        restore_print_status_foreground_ = false;
        std::string prompt_header = command.substr(13);
        LOG_DEBUG("PROMPT_BEGIN: {}", prompt_header);

        // remove buttons
        clear();

        // set header here
        lv_label_set_text(header, prompt_header.c_str());
      } else if (command.find("prompt_text") == 0) {
        std::string prompt_text = command.substr(12);
        LOG_DEBUG("PROMPT_TEXT: {}", prompt_text);
        add_text(prompt_text);
      // due to using find, order IS important!
      } else if (command.find("prompt_button_group_start") == 0) {
        LOG_DEBUG("Button group created");
        // create new button group in flex window and mark active
        button_group_cont = button_row(flex, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(button_group_cont, scale_r(4), 0);

#ifdef DEBUG_LINES
        lv_obj_set_style_border_width(button_group_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(button_group_cont, lv_palette_main(LV_PALETTE_PINK), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(button_group_cont, lv_palette_lighten(LV_PALETTE_PINK, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
        // lv_obj_set_style_min_height(button_group_cont, lv_pct(5), 0);
      } else if (command.find("prompt_button_group_end") == 0) {
        // does nothing since start creates a new one
        LOG_DEBUG("Button group ended");
        button_group_cont = NULL;
      } else if (command.find("prompt_footer_button") == 0 || command.find("prompt_button") == 0) {
        int index_label = command.find("button", 0) + strlen("button");
        int index_first = command.find("|", index_label);
        int index_second = command.find("|", index_first + 1);
        LOG_DEBUG("indexes: {} {} {}", index_label, index_first, index_second);
        std::string prompt_footer_button = command.substr(index_label, index_first - index_label);
        std::string prompt_button_command;
        std::string prompt_button_type = "none";
        LOG_DEBUG("button: {} |  {} | {}", prompt_footer_button, prompt_button_command, prompt_button_type);
        if (index_second > 0) {
          prompt_button_command = command.substr(index_first + 1, index_second - index_first - 1);
          prompt_button_type = command.substr(index_second + 1, command.length() - index_second - 1);
        } else {
          prompt_button_command = command.substr(index_first + 1);
        }
        LOG_DEBUG("PROMPT_FOOTER_BUTTON: {} CMD: {}, type {}", prompt_footer_button, prompt_button_command, prompt_button_type);
        lv_obj_t *parent;
        if (command.find("prompt_footer_button") == 0) {
          parent = footer_cont;
        } else if (button_group_cont == NULL) {
          parent = flex;
        } else {
          parent = button_group_cont;
        }
        add_button(parent, prompt_footer_button, prompt_button_command, prompt_button_type);
      } else if (command.find("prompt_show") == 0) {
        LOG_DEBUG("PROMPT_SHOW");
        // The recovery prompt replaces any preceding Klipper error banner.
        NotificationBanner::instance().clear(NotificationBanner::Source::Klipper);
        restore_print_status_foreground_ = print_status_panel.is_foreground();
        foreground();
        fit_box();
        prompt_shown = true;
        // a NUMPAD button opens the numpad as the prompt shows, so the screen follows a
        // prompt another client moved on to
        if (!numpad_cmd.empty()) {
          open_numpad(numpad_cmd);
        }
      } else if (command.find("prompt_end") == 0) {
        LOG_DEBUG("PROMPT_END");
        close();
      } else {
        LOG_DEBUG("action {} --- not supported", command);
      }
    }
  }
}
