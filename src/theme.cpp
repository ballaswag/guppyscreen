#include "theme.h"
#include <fmt/core.h>
#include "config.h"
#include "logger.h"
#include "icons.h"
#include "platform.h"

#include <algorithm>
#include <iterator>
#include <cstdlib>

namespace Theme {

namespace {

int knob_pad() { return scale_r(4); }  // the knob is the track's height plus this each side

// LVGL's default theme darkens a pressed button with a colour filter and never
// touches a plain object, so a tappable card has to borrow the same filter to
// react the same way. Same callback, same opacity: one press look everywhere.
lv_color_t darken_filter_cb(const lv_color_filter_dsc_t *f, lv_color_t c, lv_opa_t opa) {
  LV_UNUSED(f);
  return lv_color_darken(c, opa);
}

}  // namespace

// grumpyscreen.cfg is the one place the palette is spelled out, and every
// deployment ships a complete one, so a key that is missing or unreadable is a
// broken config, not something to paper over with a second copy of the colours
// here. It is reported once and drawn magenta so it cannot be missed.
lv_color_t cfg_col(const char *key) {
  const auto s = Config::get_instance()->get<std::string>(std::string("/theme/") + key);
  char *end = NULL;
  const unsigned long v = std::strtoul(s.c_str(), &end, 16);
  if (s.empty() || end == s.c_str() || *end != '\0') {
    LOG_ERROR("[theme] {}: '{}' is not a hex colour like 0x2196F3", key, s);
    return lv_color_hex(0xFF00FF);
  }
  return lv_color_hex(v);
}

namespace {
lv_color_t cache[COLOUR_COUNT];
bool ready = false;
}  // namespace

void set_palette(const lv_color_t (&palette)[COLOUR_COUNT]) {
  std::copy(std::begin(palette), std::end(palette), cache);
  ready = true;
}

lv_color_t col(Colour c) {
  if (!ready) {
    // The greys are LVGL's own dark theme, which is what grumpyscreen has
    // always worn: the page is its card colour (every full-screen panel showed
    // that before it said so explicitly), a button is its DARK_COLOR_GREY, and
    // a hairline is that same grey -- subtle against the page, the way the
    // stock button matrices and text entries have always been outlined.
    // Every role is a [theme] key in grumpyscreen.cfg: <name>_colour.
    cache[BG]         = cfg_col("bg_colour");
    cache[SURFACE]    = cfg_col("surface_colour");
    cache[RAISED]     = cfg_col("raised_colour");
    cache[SELECTED]   = cfg_col("selected_colour");
    cache[BORDER]     = cfg_col("border_colour");
    cache[BORDER_DIM] = cfg_col("border_dim_colour");
    cache[TEXT]       = cfg_col("text_colour");
    cache[TEXT_DIM]   = cfg_col("text_dim_colour");
    cache[ICON]       = cfg_col("icon_colour");
    cache[DISABLED]   = cfg_col("disabled_colour");
    cache[ON_PRIMARY] = cfg_col("on_primary_colour");
    cache[DANGER]     = cfg_col("danger_colour");
    cache[WARNING]    = cfg_col("warning_colour");
    cache[SUCCESS]    = cfg_col("success_colour");
    cache[INFO]       = cfg_col("info_colour");
    ready = true;
  }
  return cache[c];
}

lv_color_t theme_primary() { return lv_theme_get_color_primary(lv_scr_act()); }
lv_color_t theme_secondary() { return lv_theme_get_color_secondary(lv_scr_act()); }

int scale_w(int px) { return px * lv_disp_get_physical_hor_res(NULL) / 480; }
int scale_h(int px) { return px * lv_disp_get_physical_ver_res(NULL) / 272; }
int scale_r(int px) { return std::min(scale_w(px), scale_h(px)); }

const lv_font_t *scale_font(int px) {
#ifdef OS_ANDROID
  // phones and tablets need far more than the built-in sizes' 28px
  return android_text_font(scale_r(px));
#endif
  struct F { int size; const lv_font_t *font; };
  static const F fonts[] = {
    {12, &lv_font_montserrat_12}, {14, &lv_font_montserrat_14},
    {16, &lv_font_montserrat_16}, {18, &lv_font_montserrat_18},
    {20, &lv_font_montserrat_20}, {22, &lv_font_montserrat_22},
#if LV_FONT_MONTSERRAT_24  // the small-screen build leaves the big sizes out of flash
    {24, &lv_font_montserrat_24}, {26, &lv_font_montserrat_26},
    {28, &lv_font_montserrat_28},
#endif
  };
  const int target = scale_r(px);
  for (const F &f : fonts) {
    if (f.size >= target) {
      return f.font;
    }
  }
  return fonts[sizeof(fonts) / sizeof(fonts[0]) - 1].font;
}

int gap() {
  static const int g = scale_r(6);
  return g;
}

// The radii LVGL's default theme derives from the dpi, worked out once at the
// 480x272 baseline (dpi 90): a button is rounder than the box around it.
int border_w() { return 1; }  // a hairline stays a hairline at any size

int radius_sm() { return scale_r(7); }  // buttons, keys, tiles, swatches
int radius_md() { return scale_r(5); }  // cards, entries, key trays
int radius_lg() { return radius_md(); }  // panels, popout boxes

// a popout sits one gap in from every screen edge and pads its content by a
// gap and a half, so the whole thing follows gap()
int popout_w() { return lv_disp_get_physical_hor_res(NULL) - 2 * gap(); }
int popout_max_h() { return lv_disp_get_physical_ver_res(NULL) - 2 * gap(); }
int popout_pad() { return gap() + gap() / 2; }
// box padding, the two borders, and 2px so integer rounding cannot wrap a row
int popout_row_w() { return popout_w() - 2 * popout_pad() - 2 * border_w() - 2; }

Styles &styles() {
  static Styles s;
  static bool ready = false;
  if (ready) {
    return s;
  }

  // The stock button: the grey LVGL has always used, rounded the same amount.
  // Nothing here says what a press looks like -- LVGL's own theme is this
  // one's parent, so its pressed darkening and disabled greying already reach
  // every button, key and image button, and that is the feedback the UI has
  // always had.
  lv_style_init(&s.btn);
  lv_style_set_pad_all(&s.btn, 0);
  lv_style_set_shadow_width(&s.btn, 0);
  lv_style_set_radius(&s.btn, radius_sm());
  lv_style_set_bg_color(&s.btn, col(RAISED));
  lv_style_set_text_color(&s.btn, col(TEXT));  // LVGL's button style sets its own
  lv_style_set_bg_opa(&s.btn, LV_OPA_COVER);
  // The fill says where the button is; it has no outline.
  lv_style_set_border_width(&s.btn, 0);
  lv_style_set_border_color(&s.btn, col(BORDER_DIM));

  lv_style_init(&s.card);
  lv_style_set_radius(&s.card, radius_md());
  lv_style_set_bg_color(&s.card, col(SURFACE));
  lv_style_set_bg_opa(&s.card, LV_OPA_COVER);
  lv_style_set_border_width(&s.card, border_w());
  lv_style_set_border_color(&s.card, col(BORDER));
  lv_style_set_pad_all(&s.card, scale_r(4));

  // shared, not per-tile local writes: LVGL allocates local properties per
  // object, and there are sixty-odd tiles
  // sides stay tight so a seven-tile row fits one-word labels; top and bottom
  // follow the gap, so a roomier theme lifts the label off the edge (the icon
  // gives the room up, see ButtonContainer::fit_icon)
  lv_style_init(&s.tile);
  // No vertical padding, which is what grumpyscreen's tiles have always had:
  // the icon is fitted to the room the cell leaves, so a pad here is taken
  // straight off the icon and the tiles read noticeably smaller than they used
  // to. The sides stay tight so a seven-tile row fits one-word labels.
  lv_style_set_pad_hor(&s.tile, scale_r(4));
  lv_style_set_pad_ver(&s.tile, 0);
  lv_style_set_pad_row(&s.tile, scale_r(2));

  // A tapped card darkens, like every button: the accent is reserved for what
  // is active, and an active card says so with its border.
  static lv_color_filter_dsc_t darken;
  lv_color_filter_dsc_init(&darken, darken_filter_cb);
  lv_style_init(&s.card_pressed);
  lv_style_set_color_filter_dsc(&s.card_pressed, &darken);
  lv_style_set_color_filter_opa(&s.card_pressed, 35);

  // A list row (a fan slider, a temperature readout): no box, just a hairline
  // rule under it, which is what has always separated one row from the next.
  lv_style_init(&s.row_card);
  lv_style_set_radius(&s.row_card, 0);
  lv_style_set_bg_color(&s.row_card, col(SURFACE));
  lv_style_set_bg_opa(&s.row_card, LV_OPA_COVER);
  lv_style_set_border_width(&s.row_card, border_w());
  lv_style_set_border_color(&s.row_card, col(BORDER));
  lv_style_set_border_side(&s.row_card, LV_BORDER_SIDE_BOTTOM);
  lv_style_set_pad_all(&s.row_card, gap());
  lv_style_set_pad_row(&s.row_card, 0);

  // A panel is a group, not a box: the page colour and no outline of its own,
  // so a screen full of them reads as one surface. What gets outlined is the
  // set of controls inside it (a key tray, an entry), never the grouping.
  lv_style_init(&s.panel);
  lv_style_set_radius(&s.panel, radius_lg());
  lv_style_set_bg_color(&s.panel, col(SURFACE));
  lv_style_set_bg_opa(&s.panel, LV_OPA_COVER);
  lv_style_set_border_width(&s.panel, 0);
  lv_style_set_border_color(&s.panel, col(BORDER));
  lv_style_set_pad_all(&s.panel, gap());

  lv_style_init(&s.popout);
  lv_style_set_pad_all(&s.popout, 0);
  lv_style_set_bg_color(&s.popout, lv_color_black());
  lv_style_set_bg_opa(&s.popout, LV_OPA_50);
  lv_style_set_border_width(&s.popout, 0);

  lv_style_init(&s.popout_box);
  lv_style_set_radius(&s.popout_box, radius_lg());
  lv_style_set_bg_color(&s.popout_box, col(SURFACE));
  lv_style_set_bg_opa(&s.popout_box, LV_OPA_COVER);
  lv_style_set_border_width(&s.popout_box, border_w());
  lv_style_set_border_color(&s.popout_box, col(BORDER));
  lv_style_set_pad_all(&s.popout_box, popout_pad());
  lv_style_set_text_color(&s.popout_box, col(TEXT));  // popouts sit on lv_layer_top, not a page
  lv_style_set_pad_row(&s.popout_box, gap());
  lv_style_set_pad_column(&s.popout_box, gap());
  lv_style_set_max_height(&s.popout_box, popout_max_h());

  // no padding of its own, children one gap apart: without an explicit gap the
  // LVGL default theme's DPI-derived pad_row/pad_column would leak in
  lv_style_init(&s.row);
  lv_style_set_pad_all(&s.row, 0);
  lv_style_set_pad_row(&s.row, gap());
  lv_style_set_pad_column(&s.row, gap());
  lv_style_set_bg_opa(&s.row, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.row, 0);

  // A full-screen overlay has to paint its own background: a plain container
  // is transparent scaffolding now, so without this the panel underneath shows
  // through. No radius or border -- it is the page, not a card.
  lv_style_init(&s.screen);
  lv_style_set_bg_color(&s.screen, col(BG));
  lv_style_set_bg_opa(&s.screen, LV_OPA_COVER);
  lv_style_set_radius(&s.screen, 0);
  lv_style_set_border_width(&s.screen, 0);
  lv_style_set_pad_all(&s.screen, gap());
  lv_style_set_pad_row(&s.screen, gap());
  lv_style_set_pad_column(&s.screen, gap());
  // text colour inherits, so setting it on the page reaches every label under
  // it; without this labels keep the LVGL theme's own grey
  lv_style_set_text_color(&s.screen, col(TEXT));

  lv_style_init(&s.swatch);
  lv_style_set_radius(&s.swatch, radius_sm());
  lv_style_set_shadow_width(&s.swatch, 0);
  lv_style_set_pad_all(&s.swatch, 0);
  lv_style_set_border_width(&s.swatch, border_w());
  lv_style_set_border_color(&s.swatch, col(BORDER_DIM));

  lv_style_init(&s.dim_disabled);
  lv_style_set_bg_opa(&s.dim_disabled, LV_OPA_30);

  lv_style_init(&s.dim_label);
  lv_style_set_text_font(&s.dim_label, scale_font(12));
  lv_style_set_text_color(&s.dim_label, col(TEXT_DIM));

  lv_style_init(&s.icon);
  lv_style_set_img_recolor_opa(&s.icon, LV_OPA_COVER);
  lv_style_set_img_recolor(&s.icon, col(ICON));

  lv_style_init(&s.icon_danger);
  lv_style_set_img_recolor_opa(&s.icon_danger, LV_OPA_COVER);
  lv_style_set_img_recolor(&s.icon_danger, col(DANGER));

  lv_style_init(&s.icon_cooldown);
  lv_style_set_img_recolor_opa(&s.icon_cooldown, LV_OPA_COVER);
  // A pale ice blue keeps cooldown distinct from the stronger pressed accent.
  lv_style_set_img_recolor(&s.icon_cooldown, lv_color_hex(0xB3E5FC));

  lv_style_init(&s.icon_pressed);
  lv_style_set_img_recolor_opa(&s.icon_pressed, LV_OPA_COVER);
  lv_style_set_img_recolor(&s.icon_pressed, theme_primary());

  lv_style_init(&s.icon_disabled);
  lv_style_set_img_recolor_opa(&s.icon_disabled, LV_OPA_COVER);
  lv_style_set_img_recolor(&s.icon_disabled, col(DISABLED));

  // Text-only actions use the same accent feedback as selected keys. Setting
  // the pressed face explicitly also overrides LVGL's default darkening, so a
  // press is visibly blue rather than merely a darker grey.
  lv_style_init(&s.text_btn);
  lv_style_set_text_color(&s.text_btn, col(TEXT));

  lv_style_init(&s.text_btn_pressed);
  lv_style_set_bg_color(&s.text_btn_pressed, theme_primary());
  lv_style_set_bg_opa(&s.text_btn_pressed, LV_OPA_COVER);
  lv_style_set_text_color(&s.text_btn_pressed, col(ON_PRIMARY));

  lv_style_init(&s.text_btn_disabled);
  lv_style_set_text_color(&s.text_btn_disabled, col(DISABLED));

  // A keyboard or keypad is all keys: no box of its own and no padding, so the
  // keys get the whole widget.
  lv_style_init(&s.key_body);
  lv_style_set_bg_opa(&s.key_body, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.key_body, 0);
  lv_style_set_pad_all(&s.key_body, 0);
  lv_style_set_pad_gap(&s.key_body, scale_r(4));

  // A set of choices gets exactly one box: the tray is it, a hairline round the
  // set, never round each key. The tray also holds the keys off the edge of the
  // group, so it is what decides how big a key is.
  lv_style_init(&s.key_tray);
  lv_style_set_bg_opa(&s.key_tray, LV_OPA_COVER);
  lv_style_set_bg_color(&s.key_tray, col(SURFACE));
  lv_style_set_radius(&s.key_tray, radius_md());
  lv_style_set_border_width(&s.key_tray, border_w());
  lv_style_set_border_color(&s.key_tray, col(BORDER));
  lv_style_set_pad_all(&s.key_tray, scale_r(4));
  lv_style_set_pad_gap(&s.key_tray, scale_r(4));

  lv_style_init(&s.key);
  lv_style_set_radius(&s.key, radius_sm());
  lv_style_set_bg_color(&s.key, col(RAISED));
  lv_style_set_bg_opa(&s.key, LV_OPA_COVER);
  lv_style_set_border_width(&s.key, 0);
  lv_style_set_text_color(&s.key, col(TEXT));

  // Keyboards and button matrices use the same immediate feedback as text
  // buttons: the key under a finger becomes the accent rather than dark grey.
  lv_style_init(&s.key_pressed);
  lv_style_set_bg_color(&s.key_pressed, theme_primary());
  lv_style_set_bg_opa(&s.key_pressed, LV_OPA_COVER);
  lv_style_set_text_color(&s.key_pressed, col(ON_PRIMARY));

  // A disabled key (the wifi keyboard's OK until the password is long enough)
  // takes the themed disabled colour, not LVGL's grey filter over the glyph.
  lv_style_init(&s.key_disabled);
  lv_style_set_text_color(&s.key_disabled, col(DISABLED));
  lv_style_set_color_filter_opa(&s.key_disabled, LV_OPA_TRANSP);

  // A keyboard's checked keys are its control keys (backspace, OK, mode, shift):
  // the same key as the rest, not LVGL's checked grey.
  lv_style_init(&s.key_ctrl);
  lv_style_set_bg_color(&s.key_ctrl, col(RAISED));
  lv_style_set_bg_opa(&s.key_ctrl, LV_OPA_COVER);
  lv_style_set_text_color(&s.key_ctrl, col(TEXT));

  // the chosen key, and only it, wears the accent
  lv_style_init(&s.key_checked);
  lv_style_set_bg_color(&s.key_checked, theme_primary());
  lv_style_set_text_color(&s.key_checked, col(ON_PRIMARY));

  // an entry is a card-shaped box with the same hairline round it as always
  lv_style_init(&s.input);
  lv_style_set_radius(&s.input, radius_md());
  lv_style_set_border_width(&s.input, border_w());
  lv_style_set_border_color(&s.input, col(BORDER));
  lv_style_set_bg_color(&s.input, col(SURFACE));
  lv_style_set_bg_opa(&s.input, LV_OPA_COVER);
  lv_style_set_text_color(&s.input, col(TEXT));

  lv_style_init(&s.option);
  lv_style_set_bg_color(&s.option, col(SURFACE));
  lv_style_set_bg_opa(&s.option, LV_OPA_COVER);
  lv_style_set_border_width(&s.option, 0);
  lv_style_set_text_color(&s.option, col(TEXT));

  lv_style_init(&s.input_placeholder);
  lv_style_set_text_color(&s.input_placeholder, col(TEXT_DIM));

  // a list is square-cornered and outlined, so its cells meet the edge
  lv_style_init(&s.table);
  lv_style_set_radius(&s.table, 0);
  lv_style_set_bg_color(&s.table, col(SURFACE));
  lv_style_set_bg_opa(&s.table, LV_OPA_COVER);
  lv_style_set_border_width(&s.table, border_w());
  lv_style_set_border_color(&s.table, col(BORDER));
  lv_style_set_pad_all(&s.table, 0);

  lv_style_init(&s.track);
  lv_style_set_bg_color(&s.track, col(RAISED));
  lv_style_set_bg_opa(&s.track, LV_OPA_COVER);
  lv_style_set_border_width(&s.track, 0);

  // a slider's groove is a wash of the accent rather than a grey channel,
  // which is how every slider in the UI has always been drawn
  lv_style_init(&s.groove);
  lv_style_set_bg_color(&s.groove, theme_primary());
  lv_style_set_bg_opa(&s.groove, LV_OPA_20);
  lv_style_set_border_width(&s.groove, 0);

  lv_style_init(&s.fill);
  lv_style_set_bg_color(&s.fill, theme_primary());
  lv_style_set_bg_opa(&s.fill, LV_OPA_COVER);

  lv_style_init(&s.arc_track);
  lv_style_set_arc_color(&s.arc_track, col(RAISED));
  lv_style_set_bg_opa(&s.arc_track, LV_OPA_TRANSP);
  lv_style_set_border_width(&s.arc_track, 0);
  lv_style_init(&s.arc_fill);
  lv_style_set_arc_color(&s.arc_fill, theme_primary());

  lv_style_init(&s.knob);
  lv_style_set_bg_color(&s.knob, theme_primary());
  lv_style_set_bg_opa(&s.knob, LV_OPA_COVER);
  lv_style_set_border_width(&s.knob, 0);
  lv_style_set_pad_all(&s.knob, knob_pad());

  lv_style_init(&s.switch_knob);
  lv_style_set_bg_color(&s.switch_knob, col(ICON));
  lv_style_set_bg_opa(&s.switch_knob, LV_OPA_COVER);

  // A slider's knob is drawn centred on the track's end, so half of it hangs
  // past the widget's box, where the parent clips it at full and at zero.
  // Insetting the track by the tallest slider's overhang keeps every knob
  // inside its box. A negative transform rather than padding: lv_bar applies
  // the transform to both the groove and the fill, but padding to the fill
  // only, and the two must end at the same place.
  lv_style_init(&s.slider);
  lv_style_set_transform_width(&s.slider, -knob_overhang(slider_h()));

  lv_style_init(&s.table_cell);
  lv_style_set_pad_ver(&s.table_cell, gap());
  lv_style_set_pad_hor(&s.table_cell, gap());
  lv_style_set_bg_color(&s.table_cell, col(SURFACE));
  lv_style_set_bg_opa(&s.table_cell, LV_OPA_COVER);
  lv_style_set_border_color(&s.table_cell, col(BORDER));
  lv_style_set_text_color(&s.table_cell, col(TEXT));

  ready = true;
  return s;
}

int fit_img(lv_obj_t *img, int w, int h, int max_zoom) {
  const lv_img_t *i = reinterpret_cast<const lv_img_t *>(img);  // w/h of the decoded source
  // a zoomed image's REAL-size box is a few px wider than the scaled bitmap on
  // each axis (anti-alias margin), so fit the bitmap to that much less or the
  // box overshoots the space it was fitted to
  const int margin = 5;
  int zoom = max_zoom;
  if (i->w > 0 && w > 0) {
    zoom = std::min(zoom, LV_IMG_ZOOM_NONE * (w - margin) / i->w);
  }
  if (i->h > 0 && h > 0) {
    zoom = std::min(zoom, LV_IMG_ZOOM_NONE * (h - margin) / i->h);
  }
  zoom = std::max(zoom, LV_IMG_ZOOM_NONE / 8);
  // a no-op when nothing changes: lv_img_set_zoom and the box refresh each
  // cost a layout pass, and size-changed handlers call this on every layout
  if (zoom == lv_img_get_zoom(img) && lv_img_get_size_mode(img) == LV_IMG_SIZE_MODE_REAL) {
    return zoom;
  }
  lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_zoom(img, zoom);
  lv_obj_refresh_self_size(img);
  return zoom;
}

void fit_first_icon(lv_event_t *e) {
  lv_obj_t *box = lv_event_get_target(e);
  const int h = lv_obj_get_content_height(box);
  fit_img(lv_obj_get_child(box, 0), h, h);
}

int touch_h() { return scale_r(44); }

int action_tile_h() { return scale_r(76); }

int slider_h() { return scale_r(16); }
int knob_overhang(int track_h) { return track_h / 2 + knob_pad(); }

std::string recolor(Colour c) { return fmt::format("#{:06x} ", lv_color_to32(col(c)) & 0xffffff); }

lv_obj_t *create_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_add_style(row, &styles().row, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

// A list that fits still slides under a finger and springs back, which reads
// as a broken tap. Keep the scroll flag in step with the content instead: set
// on overflow, cleared when everything fits.
void style_icon(lv_obj_t *img) {
  const void *src = lv_img_get_src(img);
  const bool danger = src == Icons::EMERGENCY || src == Icons::FACTORY_RESET_IMG ||
                      src == Icons::SHUTDOWN_IMG || src == Icons::SWITCH_STOCK_IMG ||
                      src == Icons::UPDATE_IMG;
  lv_obj_add_style(img, danger ? &styles().icon_danger
                               : src == Icons::COOLDOWN_IMG ? &styles().icon_cooldown
                                                            : &styles().icon, 0);
  lv_obj_add_style(img, &styles().icon_pressed, LV_STATE_PRESSED);
  lv_obj_add_style(img, &styles().icon_disabled, LV_STATE_DISABLED);
}

void manage_scroll(lv_obj_t *scrollee) {
  lv_obj_set_scrollbar_mode(scrollee, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(scrollee, LV_DIR_VER);
  auto follow = [](lv_event_t *e) { refresh_scroll(lv_event_get_target(e)); };
  lv_obj_add_event_cb(scrollee, follow, LV_EVENT_SIZE_CHANGED, NULL);
  refresh_scroll(scrollee);
}

void refresh_scroll(lv_obj_t *scrollee) {
  const bool overflows = lv_obj_get_scroll_top(scrollee) > 0 || lv_obj_get_scroll_bottom(scrollee) > 0;
  if (overflows) {
    lv_obj_add_flag(scrollee, LV_OBJ_FLAG_SCROLLABLE);
  }
  else {
    lv_obj_clear_flag(scrollee, LV_OBJ_FLAG_SCROLLABLE);
  }
}

lv_obj_t *create_screen(lv_obj_t *parent) {
  lv_obj_t *scr = lv_obj_create(parent != NULL ? parent : lv_scr_act());
  lv_obj_add_style(scr, &styles().screen, 0);
  lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

void style_embedded_tabview(lv_obj_t *tabview) {
  lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_radius(tab_btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_pad_all(tab_btns, 0, 0);
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tab_btns, col(SURFACE), 0);
  lv_obj_set_style_border_width(tab_btns, 0, 0);
}

// The base button look comes from the theme callback. This adds its label,
// click handling, and the shared blue pressed state for text actions.
lv_obj_t *create_text_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_center(lbl);
  if (cb != NULL) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  }
  lv_obj_add_style(btn, &styles().text_btn, 0);
  lv_obj_add_style(btn, &styles().text_btn_pressed, LV_STATE_PRESSED);
  lv_obj_add_style(btn, &styles().text_btn_disabled, LV_STATE_DISABLED);
  return btn;
}

void set_btn_label(lv_obj_t *btn, const char *text) {
  if (btn != NULL && lv_obj_get_child_cnt(btn) > 0) {
    lv_label_set_text(lv_obj_get_child(btn, 0), text);
  }
}

void set_action_btn(lv_obj_t *btn, bool enabled, lv_color_t enabled_colour) {
  lv_obj_t *label = lv_obj_get_child_cnt(btn) > 0 ? lv_obj_get_child(btn, 0) : NULL;
  if (enabled) {
    lv_obj_clear_state(btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn, enabled_colour, 0);
    if (label) {
      lv_obj_set_style_text_color(label, col(TEXT), 0);
    }
  } else {
    // grey text on the plain button grey: reads as off, unlike TEXT which
    // would make it identical to a live flat button
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn, col(RAISED), 0);
    if (label) {
      lv_obj_set_style_text_color(label, col(DISABLED), 0);
    }
  }
}

}  // namespace Theme
