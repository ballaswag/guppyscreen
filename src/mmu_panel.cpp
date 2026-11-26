#include "mmu_panel.h"
#include "notification_banner.h"
#include "mmu_tools.h"
#include "config.h"
#include "icons.h"
#include "state.h"
#include "logger.h"
#include "theme.h"
#include "utils.h"
#include "simple_dialog.h"

#include <algorithm>
#include <cctype>

// every size, colour, shared style and widget factory this panel draws with
using namespace Theme;

// The classic filament colours. The clear tile ("", offered only when the
// backend can really clear a colour) and the custom-colour button join them in
// the same grid, and the palette gives up its last colour when that is what it
// takes to split the grid into two rows of equal length.
static const char *COLOUR_PRESETS[] = {
  "212121", "FFFFFF", "9E9E9E", "F44336", "FF9800", "FFEB3B", "4CAF50",
  "00BCD4", "2196F3", "3F51B5", "9C27B0", "E91E63", "795548"
};

// The swatches are laid out in two rows of equal length: the presets, the
// optional "no colour" swatch and the custom button are split down the middle,
// and the palette gives up its last colour if that would leave an odd one out.
static const size_t SWATCH_ROWS = 2;

// The edit screen material row fits this many buttons plus the "more" button
static const size_t MAX_MATERIALS = 5;

// "PLA, PETG" -> {"PLA", "PETG"}; blanks dropped
static std::vector<std::string> split_csv(const std::string &s) {
  std::vector<std::string> out;
  for (auto &tok : KUtils::split(s, ',')) {
    tok.erase(0, tok.find_first_not_of(" \t"));
    tok.erase(tok.find_last_not_of(" \t") + 1);
    if (!tok.empty()) {
      out.push_back(tok);
    }
  }
  return out;
}

static const int HEADER_HEIGHT = 40;  // the status bar is tappable: a full touch target
static const size_t CARDS_PER_PAGE = 8;
static const size_t CARDS_PER_ROW = 4;
// below this brightness a filament colour blends into the card, so its rim is
// drawn in a fixed grey instead of a darkened shade of itself
static constexpr int DARK_COLOUR = 60;

// What a tappable box on this panel is filled with. The header and the lane
// tiles are the same kind of thing, so they ask the same question.
static lv_color_t tile_bg() { return col(RAISED); }

// What a card or tile calls a slot: its physical position, counted from 1 in
// the order the backend reports the slots, and the tool(s) mapped to it --
// "S1 (T2)". A tool number alone reads as a position, and T2 need not be in
// the second slot.
static std::string slot_label(const MmuSlot &s, size_t idx) {
  return s.map.empty() ? fmt::format("S{}", idx + 1) : fmt::format("S{} ({})", idx + 1, s.map);
}

// Every colour the panel handles is a vendor string: "RRGGBB", "#rrggbb", ""
// or "NONE". mmu_normalise_hex() and this are the only places that know that.
bool parse_colour(const std::string &colour, lv_color_t *out) {
  const std::string hex = mmu_normalise_hex(colour);
  if (hex.size() >= 6) {
    try {
      *out = lv_color_hex(std::stoul(hex.substr(0, 6), nullptr, 16));
      return true;
    } catch (const std::exception &) {}
  }
  *out = col(BORDER_DIM);
  return false;
}

// The rim round the disc and the rim round its hole are the same ring seen
// from two sides, so they are one width -- and a scaled one: a raw 1 stayed a
// hairline while the disc's grew with the display.
static int spool_rim() { return scale_r(2); }

static void style_spool_icon(lv_obj_t *spool, lv_obj_t *hole, int diameter) {
  lv_obj_set_size(spool, diameter, diameter);
  lv_obj_set_style_pad_all(spool, 0, 0);
  lv_obj_set_style_radius(spool, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(spool, true, 0);
  lv_obj_set_style_border_width(spool, spool_rim(), 0);
  lv_obj_clear_flag(spool, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(spool, LV_OBJ_FLAG_CLICKABLE);

  // The empty-slot transparency grid is a tiled background image, switched on
  // and off with bg_img_opa: with clip_corner set LVGL draws a bg image in a
  // second, radius-masked pass, so it lands inside the circle. It used to be a
  // 4x4 grid of child objects per spool -- ~150 across a full page of cards,
  // for a fixed pattern. The image is 1-bit alpha: its set bits are painted in
  // bg_img_recolor and bg_color shows through the rest.
  lv_obj_set_style_bg_img_src(spool, Icons::CHECKER, 0);
  lv_obj_set_style_bg_img_tiled(spool, true, 0);
  lv_obj_set_style_bg_img_recolor(spool, col(SURFACE), 0);
  lv_obj_set_style_bg_img_recolor_opa(spool, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_img_opa(spool, LV_OPA_TRANSP, 0);

  // the hole shows the page through the spool
  int hole_size = std::max(scale_r(10), diameter / 3);
  lv_obj_set_size(hole, hole_size, hole_size);
  lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hole, col(BG), 0);
  lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hole, 0, 0);
  lv_obj_center(hole);
  lv_obj_move_foreground(hole);
  lv_obj_clear_flag(hole, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(hole, LV_OBJ_FLAG_CLICKABLE);
}

// What is used of the spool, dimmed over the disc clockwise from 12 o'clock:
// the rest keeps the filament's colour, so the disc reads as a pie chart. An
// arc as wide as its radius is a wedge; the hole stays on top of it.
static lv_obj_t *create_used_arc(lv_obj_t *spool, lv_obj_t *hole, int diameter) {
  lv_obj_t *arc = lv_arc_create(spool);
  lv_obj_remove_style_all(arc);
  const int d = diameter - 2 * spool_rim();
  lv_obj_set_size(arc, d, d);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, d / 2, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, col(BG), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(arc, LV_OPA_70, LV_PART_INDICATOR);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(hole);
  return arc;
}

static void paint_used_arc(lv_obj_t *arc, int remaining_pct) {
  if (remaining_pct < 0) {
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_arc_set_value(arc, 100 - remaining_pct);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
}

// two colours near enough that a ring of one round a disc of the other would
// not read as a ring: the channel differences summed, on a 0..765 scale
// The spool shows what is in the lane, never whether it is the loaded one:
// that is the lane's own accent border and its accent tool label, and a third
// marker on the same card only competes with them.
static void paint_spool_icon(lv_obj_t *spool, lv_obj_t *hole, lv_color_t colour,
                             bool colour_valid, bool has_filament) {
  if (has_filament) {
    // Ready (assumed normal state). Dark filament blends into the card
    // background, so give it a grey rim instead of a darkened one
    lv_obj_set_style_bg_img_opa(spool, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(spool, colour, 0);
    lv_obj_set_style_bg_opa(spool, LV_OPA_COVER, 0);
    const bool dark = lv_color_brightness(colour) < DARK_COLOUR;
    const lv_color_t rim = dark ? col(TEXT_DIM) : lv_color_darken(colour, LV_OPA_30);
    lv_obj_set_style_border_color(spool, rim, 0);
    lv_obj_set_style_border_width(spool, spool_rim(), 0);
    // the hole is rimmed like the disc, so it reads as a hole and not a gap
    if (hole != NULL) {
      lv_obj_set_style_border_color(hole, rim, 0);
      lv_obj_set_style_border_width(hole, spool_rim(), 0);
    }
  } else if (colour_valid) {
    // Empty but a colour is configured: show it translucent so fill state stays readable
    lv_obj_set_style_bg_img_opa(spool, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(spool, colour, 0);
    lv_obj_set_style_bg_opa(spool, LV_OPA_50, 0);
    lv_obj_set_style_border_color(spool, col(BORDER_DIM), 0);
    lv_obj_set_style_border_width(spool, spool_rim(), 0);
    if (hole != NULL) {
      lv_obj_set_style_border_width(hole, 0, 0);
    }
  } else {
    // Empty spool, no colour -> alpha-channel checkerboard, the light tiles
    // being the spool's own background showing through the image
    lv_obj_set_style_bg_img_opa(spool, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(spool, col(DISABLED), 0);
    lv_obj_set_style_bg_opa(spool, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(spool, col(BORDER_DIM), 0);
    lv_obj_set_style_border_width(spool, spool_rim(), 0);
    if (hole != NULL) {
      // the same grey as the disc's own rim: RAISED is a shade off the hole's
      // fill, so it read as a slightly bigger hole rather than a ring
      lv_obj_set_style_border_color(hole, col(BORDER_DIM), 0);
      lv_obj_set_style_border_width(hole, spool_rim(), 0);
    }
  }
}

MmuPanel::MmuPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , backend(NULL)
  , cont(NULL)
  , header_row(NULL)
  , status_bar(NULL)
  , status_label(NULL)
  , cards_row1(NULL)
  , cards_row2(NULL)
  , nav_row(NULL)
  , nav_prev_btn(NULL)
  , nav_next_btn(NULL)
  , nav_label(NULL)
  , current_page(0)
  , built_page_count(SIZE_MAX)
  , edit_panel_cont(NULL)
  , edit_preview_spool(NULL)
  , edit_preview_hole(NULL)
  , edit_preview_used(NULL)
  , edit_name_lbl(NULL)
  , edit_tool_lbl(NULL)
  , edit_mat_lbl(NULL)
  , edit_load_btn(NULL)
  , edit_eject_btn(NULL)
  , edit_swatches_row1(NULL)
  , edit_swatches_row2(NULL)
  , backup_picker(NULL)
  , backup_picker_list(NULL)
  , colour_picker(NULL)
  , colour_wheel(NULL)
  , colour_sat_slider(NULL)
  , colour_val_slider(NULL)
  , colour_pick_preview(NULL)
  , colour_pick_ok(NULL)
  , colour_pick_cancel(NULL)
  , custom_colour_btn(NULL)
  , material_picker(NULL)
  , material_picker_list(NULL)
  , more_mat_btn(NULL)
  , edit_slot_idx(-1)
  , loaded_idx(-1)
  , activity(MmuActivity::Idle)
  , message_error(false)
  , error_state(false)
  , bypass(false)
{
  // Screens are built lazily in create() so printers without an MMU
  // never allocate any of this panel's LVGL objects
  ws.register_notify_update(this);
}

MmuPanel::~MmuPanel() {
  NotificationBanner::instance().clear(NotificationBanner::Source::Mmu);
  ws.unregister_notify_update(this);
  if (backup_picker != NULL) {
    lv_obj_del(backup_picker);
    backup_picker = NULL;
  }
  if (colour_picker != NULL) {
    lv_obj_del(colour_picker);
    colour_picker = NULL;
  }
  if (material_picker != NULL) {
    lv_obj_del(material_picker);
    material_picker = NULL;
  }
  if (edit_panel_cont != NULL) {
    lv_obj_del(edit_panel_cont);
    edit_panel_cont = NULL;
  }
  // cont is owned by the tabview; MainPanel deletes it with the tab
}

// =========================================================================
// MAIN TAB VIEW: paginated slot grid with status header
// =========================================================================
void MmuPanel::create(lv_obj_t *parent) {
  if (cont != NULL) {
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  cont = create_screen(parent);  // fills the tab: it is the page
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // Top Header Row (status container)
  header_row = create_row(cont);
  lv_obj_set_size(header_row, LV_PCT(100), scale_r(HEADER_HEIGHT));
  lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);

  // Status Bar inside header row (Flex grow fills available space)
  status_bar = lv_obj_create(header_row);
  lv_obj_set_height(status_bar, LV_PCT(100));
  lv_obj_set_flex_grow(status_bar, 1);
  lv_obj_add_style(status_bar, &styles().card, 0);
  lv_obj_add_style(status_bar, &styles().card_pressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(status_bar, tile_bg(), 0);  // it is a button, so it wears one
  lv_obj_set_style_pad_hor(status_bar, gap() * 2, 0);
  lv_obj_set_style_pad_ver(status_bar, gap() / 2, 0);
  lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(status_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(status_bar, &MmuPanel::_handle_status_bar, LV_EVENT_CLICKED, this);

  status_label = lv_label_create(status_bar);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(status_label, "MMU Standby");
  lv_obj_set_style_text_font(status_label, scale_font(12), 0);
  lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_width(status_label, LV_PCT(100));
  lv_obj_clear_flag(status_label, LV_OBJ_FLAG_CLICKABLE);

  if (open_spoolman) {
    spoolman_btn = std::make_unique<ButtonContainer>(header_row, Icons::NAV_SPOOLMAN, "Spoolman",
        [](lv_event_t *e) {
          auto *self = static_cast<MmuPanel *>(lv_event_get_user_data(e));
          if (self->spoolman_available && self->open_spoolman) {
            self->open_spoolman();
          }
        }, this);
    spoolman_btn->use_plain();
    spoolman_btn->hide_label();
    lv_obj_set_size(spoolman_btn->get_container(), scale_r(HEADER_HEIGHT), LV_PCT(100));
    set_spoolman_available(spoolman_available);
  }

  // Row 1: Spools 1 - 4. The card rows flex-grow to split whatever height
  // the header and nav rows leave over
  cards_row1 = create_row(cont);
  lv_obj_set_width(cards_row1, LV_PCT(100));
  lv_obj_set_flex_grow(cards_row1, 1);
  lv_obj_set_flex_flow(cards_row1, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cards_row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Row 2: Spools 5 - 8
  cards_row2 = create_row(cont);
  lv_obj_set_width(cards_row2, LV_PCT(100));
  lv_obj_set_flex_grow(cards_row2, 1);
  lv_obj_set_flex_flow(cards_row2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cards_row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Row 3: Page navigation (if > 8 spools). Aligned, not flexed: hiding one
  // arrow must not shift the page label. Kept low: every pixel here comes
  // off the two card rows above
  nav_row = create_row(cont);
  lv_obj_set_size(nav_row, LV_PCT(100), scale_h(26));
  lv_obj_add_flag(nav_row, LV_OBJ_FLAG_HIDDEN);

  nav_prev_btn = create_text_btn(nav_row, "< Prev", &MmuPanel::_handle_page_prev, this);
  lv_obj_set_size(nav_prev_btn, scale_w(70), LV_PCT(100));
  lv_obj_align(nav_prev_btn, LV_ALIGN_LEFT_MID, 0, 0);

  nav_label = lv_label_create(nav_row);
  lv_label_set_text(nav_label, "Page 1 / 1");
  lv_obj_center(nav_label);

  nav_next_btn = create_text_btn(nav_row, "Next >", &MmuPanel::_handle_page_next, this);
  lv_obj_set_size(nav_next_btn, scale_w(70), LV_PCT(100));
  lv_obj_align(nav_next_btn, LV_ALIGN_RIGHT_MID, 0, 0);
}

void MmuPanel::set_spoolman_available(bool available) {
  spoolman_available = available;
  if (spoolman_btn != NULL) {
    if (available) {
      spoolman_btn->enable();
    } else {
      spoolman_btn->disable();
    }
  }
}

// =========================================================================
// FULL-SCREEN SPOOL EDIT PANEL (480x272 on lv_scr_act())
// =========================================================================
void MmuPanel::create_edit_screen() {
  if (edit_panel_cont != NULL) {
    return;
  }

  lv_color_t primary = theme_primary();

  edit_panel_cont = create_screen(NULL);
  lv_obj_set_flex_flow(edit_panel_cont, LV_FLEX_FLOW_ROW);

  // Left Column: preview, info and slot actions. The panel style has no
  // pad_row of its own, so the columns say so or the LVGL default leaks in
  lv_obj_t *left_col = lv_obj_create(edit_panel_cont);
  lv_obj_set_size(left_col, scale_w(185), LV_PCT(100));
  lv_obj_add_style(left_col, &styles().panel, 0);
  lv_obj_set_style_pad_row(left_col, gap(), 0);
  lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);

  // Left Top Info Box: takes whatever the action buttons leave
  lv_obj_t *preview_box = create_row(left_col);
  lv_obj_set_width(preview_box, LV_PCT(100));
  lv_obj_set_flex_grow(preview_box, 1);
  lv_obj_set_flex_flow(preview_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(preview_box, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  edit_name_lbl = lv_label_create(preview_box);
  lv_label_set_text(edit_name_lbl, ""); // the slot's own name lands here on open
  lv_obj_set_style_text_font(edit_name_lbl, scale_font(14), 0);

  edit_preview_spool = lv_obj_create(preview_box);
  edit_preview_hole = lv_obj_create(edit_preview_spool);
  style_spool_icon(edit_preview_spool, edit_preview_hole, scale_r(48));
  edit_preview_used = create_used_arc(edit_preview_spool, edit_preview_hole, scale_r(48));

  // both info lines can grow ("PLA - 750g (locked)", "Tool: T0 - Backup for
  // T1, T2"), so they truncate rather than spill out of the column
  edit_mat_lbl = lv_label_create(preview_box);
  lv_label_set_text(edit_mat_lbl, "-");
  edit_tool_lbl = lv_label_create(preview_box);
  lv_label_set_text(edit_tool_lbl, "");  // the slot's label lands here on open
  lv_obj_set_style_text_color(edit_tool_lbl, primary, 0);
  for (lv_obj_t *lbl : {edit_mat_lbl, edit_tool_lbl}) {
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  }

  // Left Bottom Actions Box: Load/Unload toggle + Eject
  lv_obj_t *left_actions = create_row(left_col);
  lv_obj_set_size(left_actions, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(left_actions, LV_FLEX_FLOW_COLUMN);

  edit_load_btn = create_text_btn(left_actions, "Load", &MmuPanel::_handle_edit_action, this);
  lv_obj_set_size(edit_load_btn, LV_PCT(100), scale_r(40));

  edit_eject_btn = create_text_btn(left_actions, "Eject Spool", &MmuPanel::_handle_edit_action, this);
  lv_obj_set_size(edit_eject_btn, LV_PCT(100), scale_r(40));

  // These two take the accent under a finger, the way a lane tile does. The
  // colour filter LVGL's own theme puts on a pressed button is switched off
  // for that state, or it would darken the accent instead of showing it.
  for (lv_obj_t *b : {edit_load_btn, edit_eject_btn}) {
    lv_obj_set_style_bg_color(b, theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_opa(b, LV_OPA_TRANSP, LV_STATE_PRESSED);
  }

  // Right Column: colour presets, material, backup, save/back
  lv_obj_t *right_col = lv_obj_create(edit_panel_cont);
  lv_obj_set_height(right_col, LV_PCT(100));
  lv_obj_set_flex_grow(right_col, 1);
  lv_obj_add_style(right_col, &styles().panel, 0);
  // a rule between what the lane does and what the spool in it is, the same
  // hairline as every other border in the UI
  lv_obj_set_style_border_side(right_col, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(right_col, border_w(), 0);
  lv_obj_set_style_border_color(right_col, col(BORDER), 0);
  lv_obj_set_style_pad_left(right_col, gap() * 2, 0);
  lv_obj_set_style_pad_row(right_col, gap(), 0);
  lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);

  // 1. Colour presets (2x6 grid including the custom button). The section
  // takes the height the fixed-height rows below leave, and the two swatch
  // rows split it, so the grid can never push the buttons off the screen
  lv_obj_t *colour_sec = create_row(right_col);
  lv_obj_set_size(colour_sec, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(colour_sec, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *col_title = lv_label_create(colour_sec);
  lv_label_set_text(col_title, "COLOUR PRESETS:");
  lv_obj_add_style(col_title, &styles().dim_label, 0);

  // One colour out of a set, so the set gets the tray the materials and the
  // extruder's selectors wear: the hairline goes round the group, never round
  // each swatch. Content-sized, because the swatches below are squares and so
  // decide the height themselves.
  lv_obj_t *colour_tray = create_row(colour_sec);
  lv_obj_add_style(colour_tray, &styles().key_tray, 0);
  lv_obj_set_size(colour_tray, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(colour_tray, LV_FLEX_FLOW_COLUMN);

  for (lv_obj_t **row : {&edit_swatches_row1, &edit_swatches_row2}) {
    *row = create_row(colour_tray);
    lv_obj_set_width(*row, LV_PCT(100));
    lv_obj_set_flex_flow(*row, LV_FLEX_FLOW_ROW);
  }

  create_colour_swatches();

  // 2. Materials: inline commons plus the configured catalog popout
  lv_obj_t *mat_sec = create_row(right_col);
  lv_obj_set_size(mat_sec, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(mat_sec, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *mat_title = lv_label_create(mat_sec);
  lv_label_set_text(mat_title, "MATERIAL:");
  lv_obj_add_style(mat_title, &styles().dim_label, 0);

  // One material out of a set, which is what a selector is, so it wears the
  // selector's tray: the hairline goes round the set, never round each chip.
  // The row grows by the tray's inset so the chips keep their own height.
  lv_obj_t *mat_row = create_row(mat_sec);
  lv_obj_add_style(mat_row, &styles().key_tray, 0);
  lv_obj_set_size(mat_row, LV_PCT(100), scale_r(40) + 2 * (scale_r(4) + border_w()));
  lv_obj_set_flex_flow(mat_row, LV_FLEX_FLOW_ROW);

  // /mmu/materials owns the full material catalog. Anything past what the
  // inline row fits is still reachable from the popout instead of vanishing.
  material_catalog = split_csv(Config::get_instance()->get<std::string>("/mmu/materials", ""));
  material_catalog.erase(
      std::remove_if(material_catalog.begin(), material_catalog.end(),
                     [](const std::string &m) {
                       // a name with a space cannot be passed as a gcode
                       // parameter; the vendor would see only its first word
                       if (m.find_first_of(" \t") == std::string::npos) {
                         return false;
                       }
                       LOG_INFO("/mmu/materials: ignoring '{}', names cannot contain spaces", m);
                       return true;
                     }),
      material_catalog.end());

  // The inline chips share the row equally, so the row holds as many of the
  // first MAX_MATERIALS names as fit without clipping the longest of them
  // ("PETG-CF" is wider than "PLA"); the rest stay reachable in the popout
  const int more_w = scale_r(40);
  lv_obj_update_layout(mat_row);
  const int avail = lv_obj_get_content_width(mat_row) - more_w - gap();
  const lv_font_t *chip_font = scale_font(14);
  materials.clear();
  int widest = 0;
  for (const auto &m : material_catalog) {
    if (materials.size() >= MAX_MATERIALS) {
      break;
    }
    widest = std::max(widest, (int)lv_txt_get_width(m.c_str(), m.size(), chip_font, 0, LV_TEXT_FLAG_NONE) + gap());
    const int n = (int)materials.size() + 1;
    if (n * widest + (n - 1) * gap() > avail) {
      break;
    }
    materials.push_back(m);
  }

  material_btns.clear();
  for (const auto &mat_name : materials) {
    lv_obj_t *b = create_text_btn(mat_row, mat_name.c_str(), &MmuPanel::_handle_edit_action, this);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_add_style(b, &styles().dim_disabled, LV_STATE_DISABLED);
    material_btns.push_back(b);
  }

  // more materials: opens the catalog popout
  more_mat_btn = lv_btn_create(mat_row);
  lv_obj_set_size(more_mat_btn, more_w, LV_PCT(100));
  lv_obj_add_style(more_mat_btn, &styles().dim_disabled, LV_STATE_DISABLED);
  lv_obj_t *mm_icon = lv_label_create(more_mat_btn);
  lv_label_set_text(mm_icon, LV_SYMBOL_LIST);
  lv_obj_set_style_text_color(mm_icon, col(ICON), 0);
  lv_obj_center(mm_icon);
  lv_obj_add_event_cb(more_mat_btn, &MmuPanel::_handle_edit_action, LV_EVENT_CLICKED, this);

  // 3. Backup, Save and Back: three icon tiles sharing a row, built and
  // styled the way the extruder panel's tiles are, so a tap turns the icon the
  // accent colour rather than shading a box.
  lv_obj_t *action_row = create_row(right_col);
  lv_obj_set_width(action_row, LV_PCT(100));
  lv_obj_set_height(action_row, 0);
  lv_obj_set_flex_grow(action_row, 1);  // the leftover height, so the icons fill it
  lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);

  // the label is the vendor's own term for the feature, set when the screen
  // opens (no backend is selected yet when this is built)
  edit_backup_btn.reset(new ButtonContainer(action_row, Icons::INFINITY_IMG, "Backup",
                                            &MmuPanel::_handle_edit_action, this));
  edit_save_btn.reset(new ButtonContainer(action_row, Icons::SD_IMG, "Save",
                                          &MmuPanel::_handle_edit_action, this));
  edit_back_btn.reset(new ButtonContainer(action_row, Icons::BACK, "Back",
                                          &MmuPanel::_handle_edit_action, this));
  // these sit inside the right-hand panel, and a set of controls gets one box
  // round it, not a box each.
  for (ButtonContainer *b : {edit_backup_btn.get(), edit_save_btn.get(), edit_back_btn.get()}) {
    b->use_plain();
    lv_obj_set_width(b->get_container(), 0);
    lv_obj_set_height(b->get_container(), LV_PCT(100));
    lv_obj_set_flex_grow(b->get_container(), 1);
  }

  // built hidden below everything; open_edit() lifts it
  lv_obj_add_flag(edit_panel_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(edit_panel_cont);
}

void MmuPanel::create_colour_swatches() {
  lv_obj_clean(edit_swatches_row1);
  lv_obj_clean(edit_swatches_row2);
  lv_obj_t *colour_tray = lv_obj_get_parent(edit_swatches_row1);

  // Clear ("") first when the backend supports it; the panel does not decide
  // that, the backend does. Everything below indexes colour_swatch_hex, so a
  // missing clear tile simply shifts the grid up by one. Rebuild on backend
  // detection so a reconnect can switch vendors.
  colour_swatch_hex.clear();
  if (backend == NULL || backend->can_clear_colour()) {
    colour_swatch_hex.push_back("");
  }
  for (const char *hex : COLOUR_PRESETS) {
    colour_swatch_hex.push_back(hex);
  }
  // one colour short of two full rows: drop the last, rather than leave a hole
  while ((colour_swatch_hex.size() + 1) % SWATCH_ROWS != 0) {
    colour_swatch_hex.pop_back();
  }
  const size_t per_row = (colour_swatch_hex.size() + 1) / SWATCH_ROWS;

  colour_swatch_btns.clear();
  for (size_t c_idx = 0; c_idx < colour_swatch_hex.size(); c_idx++) {
    const std::string &hex_str = colour_swatch_hex[c_idx];
    lv_obj_t *parent_row = (c_idx < per_row) ? edit_swatches_row1 : edit_swatches_row2;

    lv_obj_t *swatch = lv_btn_create(parent_row);
    lv_obj_set_height(swatch, LV_PCT(100));
    lv_obj_set_flex_grow(swatch, 1);
    lv_obj_add_style(swatch, &styles().swatch, 0);
    lv_obj_add_style(swatch, &styles().dim_disabled, LV_STATE_DISABLED);

    if (hex_str.empty()) {
      lv_obj_set_style_bg_color(swatch, col(RAISED), 0);
      lv_obj_t *icon = lv_label_create(swatch);
      lv_label_set_text(icon, LV_SYMBOL_CLOSE);
      lv_obj_add_style(icon, &styles().dim_label, 0);
      lv_obj_center(icon);
    } else {
      lv_color_t c;
      parse_colour(hex_str, &c);
      lv_obj_set_style_bg_color(swatch, c, 0);
    }
    lv_obj_add_event_cb(swatch, &MmuPanel::_handle_edit_action, LV_EVENT_CLICKED, this);
    colour_swatch_btns.push_back(swatch);
  }

  // custom colour: opens the colour wheel popout
  custom_colour_btn = lv_btn_create(edit_swatches_row2);
  lv_obj_set_height(custom_colour_btn, LV_PCT(100));
  lv_obj_set_flex_grow(custom_colour_btn, 1);
  lv_obj_add_style(custom_colour_btn, &styles().swatch, 0);
  lv_obj_add_style(custom_colour_btn, &styles().dim_disabled, LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(custom_colour_btn, col(RAISED), 0);
  lv_obj_t *cc_icon = lv_label_create(custom_colour_btn);
  lv_label_set_text(cc_icon, LV_SYMBOL_EDIT);
  lv_obj_set_style_text_color(cc_icon, col(ICON), 0);  // a glyph, but an icon to the theme
  lv_obj_center(cc_icon);
  lv_obj_add_event_cb(custom_colour_btn, &MmuPanel::_handle_edit_action, LV_EVENT_CLICKED, this);

  // A swatch is a square. Its width is its share of the row, so the row is
  // given that as its height and the swatches -- full height, equal width --
  // come out square. Measured rather than assumed: the row's width is whatever
  // the tray leaves after its own padding.
  lv_obj_update_layout(colour_tray);
  const lv_coord_t swatch_row_w = lv_obj_get_content_width(edit_swatches_row1);
  const lv_coord_t swatch_side =
      (swatch_row_w - (lv_coord_t)(per_row - 1) * gap()) / (lv_coord_t)per_row;
  for (lv_obj_t *row : {edit_swatches_row1, edit_swatches_row2}) {
    lv_obj_set_height(row, swatch_side);
  }
}

MmuBackend *MmuPanel::select_backend() {
  // runs on the websocket thread, and again on every klipper reconnect, so it
  // can race LVGL event callbacks reading the backend on the UI thread
  std::lock_guard<std::mutex> guard(lv_lock);
  clear();
  for (MmuBackend *b : backends) {
    if (!b->detect()) {
      continue;
    }
    LOG_INFO("MMU backend: {}", b->vendor());
    backend = b;
    if (edit_panel_cont != NULL) {
      create_colour_swatches();
    }
    backend->changed = [this]() {
      std::lock_guard<std::mutex> lock(lv_lock);
      if (cont == NULL) {
        return;
      }
      refresh();
      populate();
    };
    return backend;
  }

  LOG_INFO("no supported MMU backend reported by Klipper");
  return NULL;
}

void MmuPanel::init_state() {
  if (cont == NULL) {
    return;
  }
  refresh();
  populate();
}

// Drop the binding and any controls or messages from the previous session.
void MmuPanel::clear() {
  if (backend != NULL) {
    backend->changed = nullptr;
  }
  backend = NULL;
  NotificationBanner::instance().clear(NotificationBanner::Source::Mmu);
  dismissed_message.clear();
  tool_map.close();
  current_page = 0;
  refresh();
  if (cont != NULL) {
    close_edit();
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void MmuPanel::refresh() {
  slots.clear();
  loaded_idx = -1;
  activity = MmuActivity::Idle;
  message = "";
  message_error = false;
  error_state = false;
  bypass = false;
  spoolman_active = false;
  weight_active = false;

  if (backend == NULL) {
    return;
  }

  backend->refresh();
  slots = backend->slots;
  loaded_idx = backend->loaded_slot;
  activity = backend->activity;
  message = backend->message;
  message_error = backend->message_error;
  error_state = backend->error;
  bypass = backend->bypass;
  spoolman_active = backend->spoolman;
  weight_active = backend->weight_tracked;

  // Stop suppressing as soon as the backend reports something else. Only
  // clearing on an empty message would swallow a repeat: a queue-backed
  // backend can go A -> B -> A without ever passing through "".
  if (message != dismissed_message) {
    dismissed_message.clear();
  }
}

// The backend reports a neutral activity; the wording is the panel's
static const char *activity_text(MmuActivity a) {
  switch (a) {
    case MmuActivity::Loading:   return "Loading";
    case MmuActivity::Unloading: return "Unloading";
    case MmuActivity::Swapping:  return "Swapping";
    case MmuActivity::Ejecting:  return "Ejecting";
    case MmuActivity::Moving:    return "Moving";
    case MmuActivity::Error:     return "Error";
    case MmuActivity::Idle:      break;
  }
  return "Idle";
}

int MmuPanel::remaining_pct(const MmuSlot &slot) const {
  if (!(spoolman_active || weight_active) || !mmu_has_filament(slot) ||
      slot.weight <= 0 || slot.full_weight <= 0) {
    return -1;
  }
  return std::min(100, (slot.weight * 100 + slot.full_weight / 2) / slot.full_weight);
}

// a slot is a backup when another slot names it as its backup (infinite spool)
bool MmuPanel::is_backup_slot(int idx) const {
  for (size_t i = 0; i < slots.size(); i++) {
    if ((int)i != idx && slots[i].backup == idx) {
      return true;
    }
  }
  return false;
}

lv_color_t MmuPanel::slot_colour(const MmuSlot &slot, bool *valid) {
  lv_color_t c;
  *valid = parse_colour(slot.colour, &c);
  return c;
}
// Builds the cards for a page *shape*: how many, and whether they share the
// height with a second row. Nothing slot-specific is set here -- populate()
// fills that on every redraw, so flipping to a page of the same shape reuses
// these objects instead of destroying and rebuilding eight cards.
void MmuPanel::rebuild_grid(size_t page_count) {
  visible_cards.clear();
  lv_obj_clean(cards_row1);
  lv_obj_clean(cards_row2);
  built_page_count = page_count;

  bool single_row_mode = page_count <= CARDS_PER_ROW;
  int spool_diam = single_row_mode ? scale_r(60) : scale_r(42);

  // rows flex-grow, so hiding row 2 hands its share of the height to row 1
  if (single_row_mode) {
    lv_obj_add_flag(cards_row2, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(cards_row2, LV_OBJ_FLAG_HIDDEN);
  }

  for (size_t i = 0; i < page_count; i++) {
    bool is_row2 = i >= CARDS_PER_ROW;
    lv_obj_t *parent_row = is_row2 ? cards_row2 : cards_row1;

    Card card;
    card.cont = lv_obj_create(parent_row);
    lv_obj_set_height(card.cont, LV_PCT(100));
    lv_obj_set_flex_grow(card.cont, 1);
    // A lane is the biggest button on the screen, so it is styled as one: the
    // button grey inside the same hairline every button set wears, and the
    // accent under a finger.
    lv_obj_add_style(card.cont, &styles().card, 0);
    lv_obj_set_style_bg_color(card.cont, tile_bg(), 0);
    lv_obj_set_style_bg_color(card.cont, theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(card.cont, radius_sm(), 0);
    lv_obj_clear_flag(card.cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card.cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card.cont, &MmuPanel::_handle_card, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(card.cont, &MmuPanel::_handle_card, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(card.cont, &MmuPanel::_handle_card, LV_EVENT_LONG_PRESSED, this);

    lv_obj_set_flex_flow(card.cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card.cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    card.spool = lv_obj_create(card.cont);
    card.hole = lv_obj_create(card.spool);
    style_spool_icon(card.spool, card.hole, spool_diam);
    card.used = create_used_arc(card.spool, card.hole, spool_diam);

    // Group the two text lines tightly; SPACE_EVENLY on the card then puts
    // the breathing room above the spool and below the text
    lv_obj_t *text_box = create_row(card.cont);
    lv_obj_set_size(text_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(text_box, scale_r(1), 0);
    lv_obj_clear_flag(text_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(text_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // both lines truncate with an ellipsis: a long name would otherwise wrap
    // and have its second line cut off by the card
    card.title = lv_label_create(text_box);
    lv_label_set_long_mode(card.title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(card.title, LV_PCT(100));
    lv_obj_set_style_text_align(card.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(card.title, "");
    lv_obj_set_style_text_font(card.title, scale_font(14), 0);
    lv_obj_clear_flag(card.title, LV_OBJ_FLAG_CLICKABLE);

    card.material = lv_label_create(text_box);
    lv_label_set_long_mode(card.material, LV_LABEL_LONG_DOT);
    lv_obj_set_width(card.material, LV_PCT(100));
    lv_obj_set_style_text_align(card.material, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(card.material, "");
    lv_obj_add_style(card.material, &styles().dim_label, 0);
    lv_obj_clear_flag(card.material, LV_OBJ_FLAG_CLICKABLE);

    visible_cards.push_back(card);
  }

  // pad partial rows with invisible spacers so cards keep the same width
  // as a full row (cards flex-grow, spacers absorb the leftover)
  size_t row1_cards = std::min(page_count, CARDS_PER_ROW);
  size_t row2_cards = page_count > CARDS_PER_ROW ? page_count - CARDS_PER_ROW : 0;
  auto fill_row = [](lv_obj_t *row, size_t missing) {
    for (size_t i = 0; i < missing; i++) {
      lv_obj_t *sp = create_row(row);
      lv_obj_set_height(sp, LV_PCT(100));
      lv_obj_set_flex_grow(sp, 1);
      lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE);
    }
  };
  fill_row(cards_row1, CARDS_PER_ROW - row1_cards);
  if (!single_row_mode) {
    fill_row(cards_row2, CARDS_PER_ROW - row2_cards);
  }
}

// Pagination row; hide the arrow that has nowhere to go
void MmuPanel::update_nav(size_t total_pages) {
  if (total_pages <= 1) {
    lv_obj_add_flag(nav_row, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(nav_row, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(nav_label, fmt::format("Page {} / {}", current_page + 1, total_pages).c_str());
  if (current_page == 0) {
    lv_obj_add_flag(nav_prev_btn, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_clear_flag(nav_prev_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (current_page + 1 >= total_pages) {
    lv_obj_add_flag(nav_next_btn, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_clear_flag(nav_next_btn, LV_OBJ_FLAG_HIDDEN);
  }
}

void MmuPanel::populate() {
  if (cont == NULL) {
    return;
  }

  // slots can shrink on a klipper reconfig; don't strand the view on an empty page
  size_t total_pages = slots.empty() ? 1 : (slots.size() + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
  if (current_page >= total_pages) {
    current_page = total_pages - 1;
  }

  size_t start_idx = current_page * CARDS_PER_PAGE;
  size_t end_idx = std::min(start_idx + CARDS_PER_PAGE, slots.size());
  size_t page_count = end_idx > start_idx ? (end_idx - start_idx) : 0;

  if (page_count != built_page_count || visible_cards.size() != page_count) {
    rebuild_grid(page_count);
  }
  update_nav(total_pages);

  lv_color_t primary = theme_primary();

  // Populate visible cards
  for (size_t c = 0; c < visible_cards.size(); c++) {
    size_t slot_idx = start_idx + c;
    if (slot_idx >= slots.size()) {
      break;
    }

    const MmuSlot &slot = slots[slot_idx];
    Card &card = visible_cards[c];
    // which slot this card stands for changes with the page
    lv_obj_set_user_data(card.cont, (void*)(intptr_t)slot_idx);

    bool has_filament = mmu_has_filament(slot);
    bool colour_valid = false;
    lv_color_t colour = slot_colour(slot, &colour_valid);
    bool backup = is_backup_slot((int)slot_idx);

    paint_spool_icon(card.spool, card.hole, colour, colour_valid, has_filament);
    const int pct = remaining_pct(slot);
    paint_used_arc(card.used, pct);

    // Line 1: slot and tool (e.g. "S1 (T0)", "S1 (T0) (B)")
    std::string tool_str = slot_label(slot, slot_idx);
    if (backup) {
      tool_str += " (B)";
    }
    lv_label_set_text(card.title, tool_str.c_str());
    lv_obj_set_style_text_color(card.title, slot.tool_loaded ? primary : col(TEXT), 0);

    // Line 2: Material. A configured material shows even when the slot is
    // physically empty (the translucent spool conveys emptiness); bare slots say "Empty"
    std::string mat_line = !slot.material.empty() ? slot.material : has_filament ? "-" : "Empty";
    if (spoolman_active && slot.spool_id >= 0) {
      mat_line += fmt::format(" #{}", slot.spool_id);
    }
    if (pct >= 0) {
      mat_line += fmt::format(" {}%", pct);
    }
    lv_label_set_text(card.material, mat_line.c_str());

    // the loaded lane outlines itself in the accent, a step heavier than the
    // hairline the others wear
    lv_obj_set_style_border_color(card.cont, slot.tool_loaded ? primary : col(BORDER), 0);
    lv_obj_set_style_border_width(card.cont, slot.tool_loaded ? scale_r(2) : border_w(), 0);
  }

  // Detailed messages use the same top overlay as Klipper errors, even on
  // another tab. Closing a fault hides its text; reset stays in the MMU panel.
  auto &banner = NotificationBanner::instance();
  // Some backends forward fault text through the Klipper console and leave
  // message empty. Don't cover that detailed banner with a generic MMU error.
  if (!message.empty() && (error_state || message != dismissed_message)) {
    banner.show(NotificationBanner::Source::Mmu,
                message,
                message_error || error_state ? NotificationBanner::Level::Error
                                             : NotificationBanner::Level::Warning,
                [this]() {
                  if (!error_state && backend != NULL) {
                    dismissed_message = message;
                    backend->dismiss_message();
                  }
                });
  } else {
    banner.clear(NotificationBanner::Source::Mmu);
  }

  // Keep the status header useful after a fault's overlay is dismissed.
  if (error_state) {
    lv_obj_set_style_bg_color(status_bar, col(DANGER), 0);
    lv_label_set_text(status_label, "MMU error - Tap to reset");
  } else if (bypass) {
    lv_obj_set_style_bg_color(status_bar, col(WARNING), 0);
    lv_label_set_text(status_label, "Bypass Active - Single Spool");
  } else {
    lv_obj_set_style_bg_color(status_bar, tile_bg(), 0);  // resting: same fill as a lane
    std::string text;
    if (activity != MmuActivity::Idle) {
      text = activity == MmuActivity::Error ? activity_text(activity)
                                            : fmt::format("{}...", activity_text(activity));
    } else if (loaded_idx >= 0 && (size_t)loaded_idx < slots.size()) {
      const MmuSlot &slot = slots[loaded_idx];
      const std::string label = slot_label(slot, loaded_idx);
      text = slot.material.empty() ? fmt::format("Loaded: {}", label)
                                   : fmt::format("Loaded: {} - {}", label, slot.material);
    } else if (slots.empty()) {
      text = "No slots reported";
    } else {
      text = "Tap spool to configure / load";
    }
    lv_label_set_text(status_label, text.c_str());
  }

  // Keep an open edit screen in sync. The index alone is not enough: the
  // backend rebuilds `slots` on every refresh and a slot can disappear from
  // it, which would silently re-point this screen -- and its Save and its
  // Load/Eject buttons -- at a different slot. Re-resolve by name instead.
  if (edit_slot_idx >= 0) {
    edit_slot_idx = -1;
    for (size_t i = 0; i < slots.size(); i++) {
      if (slots[i].name == edit_slot_name) { edit_slot_idx = (int)i; break; }
    }
    if (edit_slot_idx >= 0) {
      // no local edit in progress on a field: follow changes made elsewhere
      // (an RFID scan, the web UI, another screen). the backend owns these
      // values, and each field follows on its own
      if (!draft_colour_dirty) {
        draft_colour = slots[edit_slot_idx].colour;
      }
      if (!draft_material_dirty) {
        draft_material = slots[edit_slot_idx].material;
      }
      update_edit_preview();
    } else {
      close_edit(); // slot disappeared on a klipper reconfig
    }
  }

  tool_map.update();
}

bool MmuPanel::open_tool_map(const json &metadata, const std::string &filename,
                             std::function<void()> print) {
  if (backend == NULL) {
    return false;
  }
  const std::vector<MmuFileTool> tools = mmu_file_tools(metadata);
  // a vendor with no tool map, or one refusing every tool the file uses,
  // has nothing to show here
  const bool mappable = std::any_of(tools.begin(), tools.end(), [this](const MmuFileTool &t) {
    return backend->can_map_tool(t.tool);
  });
  if (!mappable) {
    return false;
  }
  refresh();
  tool_map.open(backend, tools, filename, print);
  return true;
}

void MmuPanel::consume(json &j) {
  if (cont == NULL) {
    return;
  }

  auto &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  const bool mmu_updated = backend != NULL && backend->owns_update(j);

  if (pstat_state.is_null() && !mmu_updated) {
    return;
  }

  std::lock_guard<std::mutex> lock(lv_lock);
  refresh();
  populate();
}

void MmuPanel::handle_card(lv_event_t *e) {
  lv_obj_t *card = lv_event_get_current_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(card);
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    card_long_pressed = false;
    return;
  }
  if (code == LV_EVENT_LONG_PRESSED) {
    card_long_pressed = true;
    pick_spool_for(idx);
    return;
  }
  if (card_long_pressed) {
    return;
  }
  LOG_TRACE("mmu card {} clicked -> opening full-screen config", idx);
  open_edit(idx);
}

void MmuPanel::pick_spool_for(int idx) {
  if (backend == NULL || idx < 0 || (size_t)idx >= backend->slots.size()) {
    return;
  }
  if (!backend->can_assign_spool()) {
    const std::string why = fmt::format("{} {}.", backend->vendor(), backend->assign_spool_refusal());
    create_simple_dialog(lv_layer_top(), "Spoolman", why.c_str(), true, false);
    return;
  }
  if (!pick_spool) {
    return;
  }
  const std::string lane = backend->slots[idx].name;
  const int current = backend->slots[idx].spool_id;
  pick_spool(lane, current, [this, idx, lane, current](int spool_id) {
    // the unit may have changed while the list was open
    if (backend == NULL || !backend->can_assign_spool() || (size_t)idx >= backend->slots.size() ||
        backend->slots[idx].name != lane) {
      return;
    }
    if (spool_id < 0 && backend->slots[idx].spool_id != current) {
      return;
    }
    backend->assign_spool(idx, spool_id);
  });
}

void MmuPanel::handle_page_prev(lv_event_t *e) {
  if (current_page > 0) {
    current_page--;
    populate();
  }
}

void MmuPanel::handle_page_next(lv_event_t *e) {
  size_t total_pages = (slots.size() + CARDS_PER_PAGE - 1) / CARDS_PER_PAGE;
  if (current_page + 1 < total_pages) {
    current_page++;
    populate();
  }
}

void MmuPanel::handle_status_bar(lv_event_t *e) {
  if (backend == NULL) {
    return;
  }
  if (error_state) {
    backend->reset_failure();
  }
}

// =========================================================================
// FULL-SCREEN EDIT PANEL LIFECYCLE
// =========================================================================
void MmuPanel::open_edit(int idx) {
  if (idx < 0 || (size_t)idx >= slots.size()) {
    return;
  }
  create_edit_screen(); // first tap on a card pays for it, as with the popouts
  edit_slot_idx = idx;
  edit_slot_name = slots[idx].name;

  const MmuSlot &slot = slots[idx];
  draft_colour = slot.colour;
  draft_material = slot.material;
  draft_colour_dirty = false;
  draft_material_dirty = false;

  if (edit_panel_cont != NULL) {
    lv_obj_clear_flag(edit_panel_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(edit_panel_cont);
  }
  update_edit_preview();
}

void MmuPanel::close_edit() {
  edit_slot_idx = -1;
  edit_slot_name.clear();
  // popouts belong to the edit screen; never leave one stranded on top
  close_backup_picker();
  close_colour_picker();
  close_material_picker();
  if (edit_panel_cont != NULL) {
    lv_obj_add_flag(edit_panel_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(edit_panel_cont);
  }
  populate();
}

void MmuPanel::update_edit_preview() {
  if (edit_slot_idx < 0 || (size_t)edit_slot_idx >= slots.size()) {
    return;
  }
  const MmuSlot &slot = slots[edit_slot_idx];

  lv_label_set_text(edit_name_lbl, slot.name.c_str());

  lv_color_t colour;
  bool draft_colour_valid = parse_colour(draft_colour, &colour);

  bool has_filament = mmu_has_filament(slot);
  paint_spool_icon(edit_preview_spool, edit_preview_hole, colour, draft_colour_valid, has_filament);
  const int pct = remaining_pct(slot);
  paint_used_arc(edit_preview_used, pct);

  // Material line: "PLA - 750g" when a spool weight is known, else just
  // "PLA". A bare empty slot shows nothing; the weight belongs to the
  // physical spool, so it never shows without filament present.
  std::string mat_str;
  if (!has_filament && slot.material.empty()) {
    mat_str = "-";
  } else {
    mat_str = draft_material.empty() ? "-" : draft_material;
    // remaining grams only mean something when spoolman or the backend
    // tracks the spool
    if (has_filament && (spoolman_active || weight_active) && slot.weight > 0) {
      mat_str += fmt::format(" - {}g", slot.weight);
      if (pct >= 0) {
        mat_str += fmt::format(" ({}%)", pct);
      }
    }
  }
  // a slot whose metadata something else owns says so, so a greyed-out Save
  // does not look like a bug
  if (!slot.can_configure) {
    mat_str += " (locked)";
  }
  lv_label_set_text(edit_mat_lbl, mat_str.c_str());

  // Tool assignment is read-only info; it comes from the MMU config. So is the
  // infinite-spool wiring, named both ways: the slot that takes over when this
  // one runs out, and the slots this one stands in for
  // labelled as its card is, "S1 (T0)"; the title above keeps the vendor's
  // own name for the slot
  std::string tool_str = slot_label(slot, edit_slot_idx);
  const bool backed_up = slot.backup >= 0 && (size_t)slot.backup < slots.size();
  if (backed_up) {
    tool_str += fmt::format(" backed up by {}", slot_label(slots[slot.backup], slot.backup));
  }
  std::string covers;
  for (size_t i = 0; i < slots.size(); i++) {
    if ((int)i != edit_slot_idx && slots[i].backup == edit_slot_idx) {
      covers += (covers.empty() ? "" : ", ") + slot_label(slots[i], i);
    }
  }
  // "S12 (T2) backs up S16 (T11)"; a comma when it is backed up as well
  if (!covers.empty()) {
    tool_str += fmt::format("{} backs up {}", backed_up ? "," : "", covers);
  }
  lv_label_set_text(edit_tool_lbl, tool_str.c_str());

  // Whether a verb is allowed right now is the backend's call -- it knows its
  // own rules about printing, faults and how far a slot is fed. The panel adds
  // only two gates of its own, both to keep the nozzle safe rather than to
  // model a vendor: unload is offered on the loaded slot only, and a slot must
  // be unloaded before it can be ejected.
  const bool swapping = !slot.tool_loaded && loaded_idx >= 0;
  set_btn_label(edit_load_btn, slot.tool_loaded ? "Unload" : swapping ? "Swap" : "Load");
  set_action_btn(edit_load_btn,
                 slot.tool_loaded ? backend != NULL && backend->can_unload()
                                  : backend != NULL && backend->can_load(edit_slot_idx),
                 slot.tool_loaded ? col(DANGER) : col(RAISED));
  set_action_btn(edit_eject_btn,
                 !slot.tool_loaded && backend != NULL && backend->can_eject(edit_slot_idx),
                 col(RAISED));

  // Whether the spool metadata can be edited is the backend's call, not ours:
  // AFC accepts colour/material on an empty slot, so presence is the wrong
  // test. See MmuSlot::can_configure.
  bool configurable = slot.can_configure;

  // Backup toggle: off is always allowed so a stale assignment can be cleared,
  // whatever the backend says about setting a new one
  bool backup = is_backup_slot(edit_slot_idx);
  bool can_toggle = backup || (backend != NULL && backend->can_set_backup(edit_slot_idx));
  // An icon tile has no "on" fill, so an assigned backup says so the way an
  // active icon does anywhere else: the glyph wears the accent.
  if (backend != NULL) {
    edit_backup_btn->set_label(backend->backup_label());
  }
  if (can_toggle) {
    edit_backup_btn->enable();
  } else {
    edit_backup_btn->disable();
  }
  // A local recolour outranks the shared icon style, so the resting colour has
  // to be asked for here rather than left to it.
  lv_obj_set_style_img_recolor(edit_backup_btn->get_button(),
                               backup ? theme_primary() : col(ICON), 0);
  lv_obj_set_style_img_recolor_opa(edit_backup_btn->get_button(), LV_OPA_COVER, 0);

  if (configurable) {
    edit_save_btn->enable();
  } else {
    edit_save_btn->disable();
  }

  // Update square colour swatches active outline
  const std::string cur_hex = mmu_normalise_hex(draft_colour);

  for (size_t i = 0; i < colour_swatch_btns.size(); i++) {
    lv_obj_t *s = colour_swatch_btns[i];
    const std::string &hex = colour_swatch_hex[i];
    bool active = configurable && (hex.empty() ? cur_hex.empty() : cur_hex == hex);
    lv_obj_set_style_border_width(s, active ? scale_r(2) : border_w(), 0);
    lv_obj_set_style_border_color(s, active ? col(TEXT) : col(BORDER_DIM), 0);
    if (configurable) {
      lv_obj_clear_state(s, LV_STATE_DISABLED);
    }
    else {
      lv_obj_add_state(s, LV_STATE_DISABLED);
    }
  }
  if (configurable) {
    lv_obj_clear_state(custom_colour_btn, LV_STATE_DISABLED);
  }
  else {
    lv_obj_add_state(custom_colour_btn, LV_STATE_DISABLED);
  }

  // Update Material Buttons checked state
  for (size_t i = 0; i < material_btns.size(); i++) {
    lv_obj_t *b = material_btns[i];
    bool active = configurable && mmu_same_material(draft_material, materials[i]);
    // Chosen = the accent fill, the way every other control in the UI says it:
    // a selector key, an action button, the nav tab. No outline -- the colour
    // swatches wear one only because a fill would cover the colour they show.
    lv_obj_set_style_bg_color(b, active ? theme_primary() : col(RAISED), 0);
    lv_obj_set_style_text_color(b, active ? col(ON_PRIMARY) : col(TEXT), 0);
    if (configurable) {
      lv_obj_clear_state(b, LV_STATE_DISABLED);
    }
    else {
      lv_obj_add_state(b, LV_STATE_DISABLED);
    }
  }
  if (configurable) {
    lv_obj_clear_state(more_mat_btn, LV_STATE_DISABLED);
  }
  else {
    lv_obj_add_state(more_mat_btn, LV_STATE_DISABLED);
  }
}

void MmuPanel::save_edit() {
  if (backend == NULL || edit_slot_idx < 0 || (size_t)edit_slot_idx >= slots.size()) {
    return;
  }
  const MmuSlot &slot = slots[edit_slot_idx];

  // Only push what the user actually changed. The backend owns this metadata
  // -- it may come from an RFID scan or be edited elsewhere -- so writing back
  // values we merely displayed would let the panel invent or resurrect data.
  const std::string hex = mmu_normalise_hex(draft_colour);
  if (hex != mmu_normalise_hex(slot.colour)) {
    backend->set_colour(edit_slot_idx, hex);
  }
  if (!mmu_same_material(draft_material, slot.material)) {
    backend->set_material(edit_slot_idx, draft_material);
  }

  close_edit();
}

void MmuPanel::handle_edit_action(lv_event_t *e) {
  lv_obj_t *target = lv_event_get_current_target(e);

  if (edit_back_btn != nullptr && target == edit_back_btn->get_container()) {
    close_edit();
    return;
  }
  if (edit_save_btn != nullptr && target == edit_save_btn->get_container()) {
    save_edit();
    return;
  }
  if (backend == NULL || edit_slot_idx < 0 || (size_t)edit_slot_idx >= slots.size()) {
    return;
  }
  const MmuSlot &slot = slots[edit_slot_idx];

  if (target == edit_load_btn) {
    // one verb: "end with this slot in the tool". Whether that means a plain
    // load or swapping out what is loaded now is the backend's decision.
    if (slot.tool_loaded) {
      backend->unload();
    }
    else {
      backend->load(edit_slot_idx);
    }
    close_edit();
    return;
  }
  if (target == edit_eject_btn) {
    backend->eject(edit_slot_idx);
    close_edit();
    return;
  }
  if (edit_backup_btn != nullptr && target == edit_backup_btn->get_container()) {
    if (is_backup_slot(edit_slot_idx)) {
      // turning it off clears every slot pointing here -- the button is a
      // single toggle, so there is nothing finer for the user to aim at
      for (size_t i = 0; i < slots.size(); i++) {
        if ((int)i != edit_slot_idx && slots[i].backup == edit_slot_idx) {
          backend->set_backup((int)i, -1);
        }
      }
    } else {
      open_backup_picker(); // choose which slot this one backs up
    }
    return; // stays open; state comes back via the subscription
  }

  if (target == backup_picker) { // tap outside the popout cancels
    close_backup_picker();
    return;
  }

  for (size_t i = 0; i < backup_pick_btns.size(); i++) {
    if (target == backup_pick_btns[i]) {
      // tiles are built once per open, so a slot list rebuilt underneath them
      // would leave their positions pointing at the wrong slot -- resolve the
      // name the tile was built for instead
      int idx = -1;
      if (i < backup_pick_names.size()) {
        for (size_t s = 0; s < slots.size(); s++) {
          if (slots[s].name == backup_pick_names[i]) { idx = (int)s; break; }
        }
      }
      // one more slot pointing here, nothing cleared: a vendor that lets one
      // spare cover several slots keeps that, and one that does not enforces
      // it in its own backend
      if (idx >= 0) {
        backend->set_backup(idx, edit_slot_idx);
      }
      close_backup_picker();
      return;
    }
  }

  if (target == custom_colour_btn) {
    open_colour_picker();
    return;
  }
  if (target == colour_picker || target == colour_pick_cancel) {
    close_colour_picker();
    return;
  }
  if (target == colour_wheel || target == colour_sat_slider || target == colour_val_slider) {
    lv_obj_set_style_bg_color(colour_pick_preview, picker_colour(), 0);
    return;
  }
  if (target == colour_pick_ok) {
    lv_color32_t c32;
    c32.full = lv_color_to32(picker_colour());
    draft_colour = fmt::format("{:02X}{:02X}{:02X}", c32.ch.red, c32.ch.green, c32.ch.blue);
    draft_colour_dirty = true;
    close_colour_picker();
    update_edit_preview();
    return;
  }

  if (target == more_mat_btn) {
    open_material_picker();
    return;
  }
  if (target == material_picker) {
    close_material_picker();
    return;
  }
  for (size_t i = 0; i < mat_pick_btns.size(); i++) {
    if (target == mat_pick_btns[i]) {
      // built once from material_catalog, in order, with Cancel appended
      if (i < material_catalog.size()) {
        draft_material = material_catalog[i];
        draft_material_dirty = true;
      }
      close_material_picker();
      update_edit_preview();
      return;
    }
  }

  // Colour swatches and the inline material row are built once, in order, so
  // the button's position is its value
  for (size_t i = 0; i < colour_swatch_btns.size(); i++) {
    if (target == colour_swatch_btns[i]) {
      draft_colour = colour_swatch_hex[i];
      draft_colour_dirty = true;
      update_edit_preview();
      return;
    }
  }

  for (size_t i = 0; i < material_btns.size(); i++) {
    if (target == material_btns[i]) {
      draft_material = materials[i];
      draft_material_dirty = true;
      update_edit_preview();
      return;
    }
  }
}
// =========================================================================
// POPOUTS: a dim full-screen sheet that cancels on a tap outside, with a box
// centred on it. All three are built and shown through these three helpers.
// =========================================================================
lv_obj_t *create_popout(lv_obj_t **overlay, lv_event_cb_t cb, void *user_data) {
  *overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(*overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_add_style(*overlay, &styles().popout, 0);
  lv_obj_clear_flag(*overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(*overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(*overlay, cb, LV_EVENT_CLICKED, user_data);

  lv_obj_t *box = lv_obj_create(*overlay);
  lv_obj_add_style(box, &styles().popout_box, 0);
  lv_obj_set_width(box, popout_w());
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_center(box);
  return box;
}

lv_obj_t *MmuPanel::create_popout(lv_obj_t **overlay) {
  return ::create_popout(overlay, &MmuPanel::_handle_edit_action, this);
}

void show_popout(lv_obj_t *overlay) {
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
}

void hide_popout(lv_obj_t *overlay) {
  if (overlay == NULL) {
    return;
  }
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(overlay);
}

lv_obj_t *create_slot_tile(lv_obj_t *parent, const MmuSlot &l, size_t idx, int width,
                           lv_event_cb_t cb, void *user_data, bool by_name) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, width, scale_h(74));
  lv_obj_set_style_pad_all(b, scale_r(4), 0);
  lv_obj_set_style_pad_row(b, scale_r(2), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // mini spool mirrors the slot's colour; translucent when the slot is empty
  lv_color_t c;
  bool colour_valid = parse_colour(l.colour, &c);
  bool has_filament = mmu_has_filament(l);
  lv_obj_t *dot = lv_obj_create(b);
  lv_obj_set_size(dot, scale_r(30), scale_r(30));
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, c, 0);
  lv_obj_set_style_bg_opa(dot, has_filament && colour_valid ? LV_OPA_COVER
                               : colour_valid ? LV_OPA_50 : LV_OPA_20, 0);
  lv_obj_set_style_border_width(dot, scale_r(2), 0);
  lv_obj_set_style_border_color(dot, l.tool_loaded ? theme_primary()
                                     : (colour_valid && lv_color_brightness(c) < DARK_COLOUR)
                                       ? col(TEXT_DIM)
                                       : col(DISABLED), 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

  std::string name = by_name ? l.name : slot_label(l, idx);
  if (l.tool_loaded) {
    name += " *";
  }
  lv_obj_t *lbl = lv_label_create(b);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl, LV_PCT(100));
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl, name.c_str());
  lv_obj_set_style_text_font(lbl, scale_font(12), 0);
  lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *mat = lv_label_create(b);
  lv_label_set_long_mode(mat, LV_LABEL_LONG_DOT);
  lv_obj_set_width(mat, LV_PCT(100));
  lv_obj_set_style_text_align(mat, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(mat, l.material.empty() ? "Empty" : l.material.c_str());
  lv_obj_add_style(mat, &styles().dim_label, 0);
  lv_obj_clear_flag(mat, LV_OBJ_FLAG_CLICKABLE);
  return b;
}

// =========================================================================
// BACKUP PICKER: choose which slot the edited slot backs up
// =========================================================================
void MmuPanel::open_backup_picker() {
  if (edit_slot_idx < 0 || (size_t)edit_slot_idx >= slots.size()) {
    return;
  }
  const MmuSlot &editing = slots[edit_slot_idx];

  if (backup_picker == NULL) {
    backup_picker_list = create_popout(&backup_picker);
    lv_obj_set_flex_flow(backup_picker_list, LV_FLEX_FLOW_ROW_WRAP);
  }

  lv_obj_clean(backup_picker_list);
  backup_pick_btns.clear();
  backup_pick_names.clear();

  // mini spool tiles fill the row: grow doesn't wrap in lv_flex, so compute
  // the tile width from the slot count instead
  int n = slots.size() > 0 ? (int)slots.size() - 1 : 0;
  int cols = std::max(1, std::min(n, 4));
  int tile_w = (popout_row_w() - (cols - 1) * gap()) / cols;

  lv_obj_t *title = lv_label_create(backup_picker_list);
  lv_label_set_text(title, fmt::format("{} backs up:", slot_label(editing, edit_slot_idx)).c_str());
  lv_obj_set_width(title, LV_PCT(100));
  lv_obj_set_style_text_font(title, scale_font(14), 0);

  for (size_t i = 0; i < slots.size(); i++) {
    const MmuSlot &l = slots[i];
    if (l.name == editing.name) {
      continue;
    }

    lv_obj_t *b = create_slot_tile(backup_picker_list, l, i, tile_w,
                                   &MmuPanel::_handle_edit_action, this);
    backup_pick_btns.push_back(b);
    backup_pick_names.push_back(l.name);
  }

  lv_obj_t *cancel = create_text_btn(backup_picker_list, "Cancel",
                                     &MmuPanel::_handle_edit_action, this);
  lv_obj_set_size(cancel, LV_PCT(100), scale_r(40));
  lv_obj_set_style_bg_color(cancel, theme_primary(), LV_STATE_PRESSED);
  lv_obj_set_style_color_filter_opa(cancel, LV_OPA_TRANSP, LV_STATE_PRESSED);
  backup_pick_btns.push_back(cancel); // no name: falls through as "cancel"

  lv_obj_scroll_to_y(backup_picker_list, 0, LV_ANIM_OFF);
  show_popout(backup_picker);
}

void MmuPanel::close_backup_picker() {
  hide_popout(backup_picker);
}

// =========================================================================
// CUSTOM COLOUR PICKER (colour wheel popout)
// =========================================================================
void MmuPanel::open_colour_picker() {
  if (colour_picker == NULL) {
    // the wheel needs the full height, so this one box is not content-sized
    lv_obj_t *box = create_popout(&colour_picker);
    lv_obj_set_height(box, popout_max_h());
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 4 * gap(), 0);  // clear air between the wheel and the controls

    // big hue wheel on the left; less fiddly to grab
    colour_wheel = lv_colorwheel_create(box, true);
    lv_obj_set_size(colour_wheel, scale_r(180), scale_r(180));
    // ring thickness keeps the designed 20px-per-180px-wheel proportion at
    // any resolution (the theme's DPI-derived default barely grows)
    lv_obj_set_style_arc_width(colour_wheel, scale_r(20), LV_PART_MAIN);
    lv_obj_add_event_cb(colour_wheel, &MmuPanel::_handle_edit_action, LV_EVENT_VALUE_CHANGED, this);

    // right side takes the rest: preview, saturation, brightness, save/cancel
    lv_obj_t *right = create_row(box);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);

    colour_pick_preview = lv_obj_create(right);
    lv_obj_set_size(colour_pick_preview, LV_PCT(100), scale_h(40));
    lv_obj_add_style(colour_pick_preview, &styles().swatch, 0);
    lv_obj_clear_flag(colour_pick_preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(colour_pick_preview, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *sat_lbl = lv_label_create(right);
    lv_label_set_text(sat_lbl, "SATURATION:");
    lv_obj_add_style(sat_lbl, &styles().dim_label, 0);

    colour_sat_slider = lv_slider_create(right);

    lv_obj_t *val_lbl = lv_label_create(right);
    lv_label_set_text(val_lbl, "BRIGHTNESS:");
    lv_obj_add_style(val_lbl, &styles().dim_label, 0);

    colour_val_slider = lv_slider_create(right);

    // A finger-sized knob on a track that stays a track. No local inset here:
    // the theme insets every track by the knob's overhang, which is exactly
    // what keeps the knob inside the column at both ends, so it stops clear of
    // the box's padding instead of hanging over the edge of the screen.
    for (lv_obj_t *s : {colour_sat_slider, colour_val_slider}) {
      lv_obj_set_size(s, LV_PCT(100), slider_h());
      lv_slider_set_range(s, 0, 100);
      lv_slider_set_value(s, 100, LV_ANIM_OFF);
      lv_obj_add_event_cb(s, &MmuPanel::_handle_edit_action, LV_EVENT_VALUE_CHANGED, this);
    }

    // spacer pushes the buttons to the bottom, away from the sliders
    lv_obj_t *btn_spacer = create_row(right);
    lv_obj_set_width(btn_spacer, LV_PCT(100));
    lv_obj_set_flex_grow(btn_spacer, 1);
    lv_obj_clear_flag(btn_spacer, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *btn_row = create_row(right);
    lv_obj_set_size(btn_row, LV_PCT(100), scale_r(40));
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);

    colour_pick_ok = create_text_btn(btn_row, "Save", &MmuPanel::_handle_edit_action, this);
    lv_obj_set_height(colour_pick_ok, LV_PCT(100));
    lv_obj_set_flex_grow(colour_pick_ok, 1);
    lv_obj_set_style_bg_color(colour_pick_ok, theme_primary(), 0);

    colour_pick_cancel = create_text_btn(btn_row, "Cancel", &MmuPanel::_handle_edit_action, this);
    lv_obj_set_height(colour_pick_cancel, LV_PCT(100));
    lv_obj_set_flex_grow(colour_pick_cancel, 1);
  }

  // seed wheel + sliders from the current draft colour
  lv_color_t seed;
  if (!parse_colour(draft_colour, &seed)) {
    seed = theme_primary();
  }  // no colour yet: start on the accent
  lv_color32_t s32;
  s32.full = lv_color_to32(seed);
  lv_color_hsv_t hsv = lv_color_rgb_to_hsv(s32.ch.red, s32.ch.green, s32.ch.blue);
  lv_colorwheel_set_hsv(colour_wheel, {hsv.h, 100, 100});
  lv_slider_set_value(colour_sat_slider, hsv.s, LV_ANIM_OFF);
  lv_slider_set_value(colour_val_slider, hsv.v, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(colour_pick_preview, seed, 0);

  show_popout(colour_picker);
}

// hue from the wheel, saturation/brightness from the sliders
lv_color_t MmuPanel::picker_colour() {
  lv_color_hsv_t hsv = lv_colorwheel_get_hsv(colour_wheel);
  return lv_color_hsv_to_rgb(hsv.h,
                             (uint8_t)lv_slider_get_value(colour_sat_slider),
                             (uint8_t)lv_slider_get_value(colour_val_slider));
}

void MmuPanel::close_colour_picker() {
  hide_popout(colour_picker);
}

// =========================================================================
// MATERIAL PICKER (catalog popout)
// =========================================================================
void MmuPanel::open_material_picker() {
  if (material_picker == NULL) {
    material_picker_list = create_popout(&material_picker);
    lv_obj_set_flex_flow(material_picker_list, LV_FLEX_FLOW_ROW_WRAP);

    lv_obj_t *title = lv_label_create(material_picker_list);
    lv_label_set_text(title, "Material:");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, scale_font(14), 0);

    mat_pick_btns.clear();
    const int cols = 4;
    const int chip_w = (popout_row_w() - (cols - 1) * gap()) / cols;
    // chip height is what lets the title, the 14 built-in names in four
    // rows and Cancel share popout_max_h() at 480x272 without scrolling
    for (const auto &mat_name : material_catalog) {
      lv_obj_t *b = create_text_btn(material_picker_list, mat_name.c_str(),
                                    &MmuPanel::_handle_edit_action, this);
      lv_obj_set_size(b, chip_w, scale_r(36));
      mat_pick_btns.push_back(b);
    }

    lv_obj_t *cancel = create_text_btn(material_picker_list, "Cancel",
                                       &MmuPanel::_handle_edit_action, this);
    lv_obj_set_size(cancel, LV_PCT(100), scale_r(40));
    mat_pick_btns.push_back(cancel); // last tile, no material of its own
  }

  // highlight the currently selected material
  for (size_t i = 0; i < mat_pick_btns.size(); i++) {
    const bool active = i < material_catalog.size() && mmu_same_material(draft_material, material_catalog[i]);
    lv_obj_set_style_bg_color(mat_pick_btns[i], active ? theme_primary() : col(RAISED), 0);
  }

  lv_obj_scroll_to_y(material_picker_list, 0, LV_ANIM_OFF);
  show_popout(material_picker);
}

void MmuPanel::close_material_picker() {
  hide_popout(material_picker);
}
