#include "cfs_mapping.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <cctype>
#include <cstdlib>

static const char *kSlotNames[] = {
  "T1A","T1B","T1C","T1D","T2A","T2B","T2C","T2D",
  "T3A","T3B","T3C","T3D","T4A","T4B","T4C","T4D"
};

static bool slot_empty(const std::string &v) {
  return v.empty() || v == "-1" || v == "None" || v == "none";
}

static lv_color_t hex_colour(const std::string &raw) {
  std::string h = raw;
  if (!h.empty() && h[0] == '#') h = h.substr(1);
  if (h.size() > 6) h = h.substr(h.size() - 6);      /* CFS uses 7 chars */
  if (h.size() != 6) return lv_palette_darken(LV_PALETTE_GREY, 3);
  for (char c : h)
    if (!isxdigit((unsigned char)c)) return lv_palette_darken(LV_PALETTE_GREY, 3);
  return lv_color_hex((uint32_t)strtoul(h.c_str(), NULL, 16));
}

/* Moonraker hands some of these fields back as a JSON-encoded string rather
   than a real array (filament_type and filament_name typically). Normalise. */
static json as_array(const json &v) {
  if (v.is_array()) return v;
  if (v.is_string()) {
    std::string s = v.template get<std::string>();
    try {
      json parsed = json::parse(s);
      if (parsed.is_array()) return parsed;
    } catch (...) { /* fall through to a plain split */ }
    json out = json::array();
    std::string cur;
    for (char ch : s) {
      if (ch == ',') { out.push_back(cur); cur.clear(); }
      else if (ch != '[' && ch != ']' && ch != '"' && ch != ' ') cur += ch;
    }
    if (!cur.empty()) out.push_back(cur);
    if (!out.empty()) return out;
  }
  return json();
}

static std::string jidx_str(const json &arr, size_t i, const std::string &def = "") {
  if (!arr.is_array() || arr.size() <= i || arr[i].is_null()) return def;
  if (arr[i].is_string()) return arr[i].template get<std::string>();
  return arr[i].dump();
}

bool CfsMapping::available() {
  json &box = State::get_instance()
    ->get_data(json::json_pointer("/printer_state/box"));
  bool ok = !box.is_null();
  spdlog::debug("cfs mapping available: {}", ok);
  return ok;                       /* no CFS: nothing to map */
}

CfsMapping::CfsMapping(KWebSocketClient &websocket_client, std::mutex &lock)
  : ws(websocket_client)
  , lv_lock(lock)
  , cont(NULL), title(NULL), rows_cont(NULL)
{
  build();
}

CfsMapping::~CfsMapping() {
  if (cont != NULL) { lv_obj_del(cont); cont = NULL; }
}

void CfsMapping::build() {
  cont = lv_obj_create(lv_scr_act());
  lv_obj_set_size(cont, LV_PCT(92), LV_PCT(92));
  lv_obj_center(cont);
  lv_obj_set_style_pad_all(cont, 8, 0);
  lv_obj_set_style_radius(cont, 8, 0);
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_border_color(cont, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 6, 0);

  title = lv_label_create(cont);
  lv_label_set_text(title, "Filament mapping");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  rows_cont = lv_obj_create(cont);
  lv_obj_set_width(rows_cont, LV_PCT(100));
  lv_obj_set_flex_grow(rows_cont, 1);
  lv_obj_set_style_pad_all(rows_cont, 2, 0);
  lv_obj_set_style_border_width(rows_cont, 0, 0);
  lv_obj_set_flex_flow(rows_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rows_cont, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(rows_cont, 8, 0);

  lv_obj_t *actions = lv_obj_create(cont);
  lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(actions, 2, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *pb = lv_btn_create(actions);
  lv_obj_set_size(pb, 200, 50);
  lv_obj_set_style_bg_color(pb, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_t *pl = lv_label_create(pb);
  lv_label_set_text(pl, LV_SYMBOL_OK " Print");
  lv_obj_set_style_text_font(pl, &lv_font_montserrat_20, 0);
  lv_obj_center(pl);
  lv_obj_add_event_cb(pb, &CfsMapping::_print, LV_EVENT_CLICKED, this);

  lv_obj_t *cb = lv_btn_create(actions);
  lv_obj_set_size(cb, 200, 50);
  lv_obj_set_style_bg_color(cb, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_t *cl = lv_label_create(cb);
  lv_label_set_text(cl, LV_SYMBOL_CLOSE " Cancel");
  lv_obj_set_style_text_font(cl, &lv_font_montserrat_20, 0);
  lv_obj_center(cl);
  lv_obj_add_event_cb(cb, &CfsMapping::_cancel, LV_EVENT_CLICKED, this);

  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
}

/* Physical slots that currently hold filament, with their colour. */
void CfsMapping::rebuild_slots() {
  slots.clear();
  colours.clear();
  json &box = State::get_instance()
    ->get_data(json::json_pointer("/printer_state/box"));
  if (box.is_null()) return;

  for (int u = 1; u <= 4; u++) {
    std::string unit = "T" + std::to_string(u);
    if (!box.contains(unit) || !box[unit].is_object()) continue;
    json &m = box[unit]["material_type"];
    json &c = box[unit]["color_value"];
    for (int i = 0; i < 4; i++) {
      std::string code = jidx_str(m, i, "-1");
      if (slot_empty(code)) continue;
      slots.push_back(unit + (char)('A' + i));
      colours.push_back(jidx_str(c, i, "-1"));
    }
  }
}

void CfsMapping::refresh_row(Row &r) {
  if (slots.empty()) {
    lv_label_set_text(r.slot_label, "none");
    return;
  }
  if (r.choice < 0 || r.choice >= (int)slots.size()) r.choice = 0;
  lv_label_set_text(r.slot_label, slots[r.choice].c_str());
  lv_obj_set_style_bg_color(r.slot_swatch, hex_colour(colours[r.choice]), 0);
  lv_obj_set_style_bg_opa(r.slot_swatch, LV_OPA_COVER, 0);
}

void CfsMapping::clear_rows() {
  lv_obj_clean(rows_cont);
  rows.clear();
}

void CfsMapping::show(json &metadata, std::function<void()> confirm) {
  on_confirm = confirm;
  rebuild_slots();
  clear_rows();

  /* The cached metadata is the whole moonraker reply, so the fields we want
     live under "result". Accept both shapes. */
  json meta = json::object();
  if (metadata.is_object()) {
    meta = metadata.contains("result") && metadata["result"].is_object()
             ? metadata["result"] : metadata;
  }
  json colours_j = as_array(meta.contains("filament_colors") ? meta["filament_colors"] : json());
  json types_j   = as_array(meta.contains("filament_type")   ? meta["filament_type"]   : json());
  json weights_j = as_array(meta.contains("filament_weights") ? meta["filament_weights"] : json());

  size_t n = colours_j.is_array() ? colours_j.size()
                                  : (types_j.is_array() ? types_j.size() : 0);
  if (n == 0) n = 1;               /* no metadata: still offer one mapping row */
  spdlog::debug("cfs mapping: {} filaments, {} loaded slots", n, slots.size());

  for (size_t i = 0; i < n && i < 16; i++) {
    Row r;
    r.tool = (int)i;
    r.choice = (int)i;                     /* default: 1:1, like the slicer */

    r.cont = lv_obj_create(rows_cont);
    lv_obj_set_size(r.cont, LV_PCT(96), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(r.cont, 8, 0);
    lv_obj_set_style_border_width(r.cont, 1, 0);
    lv_obj_set_style_radius(r.cont, 6, 0);
    lv_obj_clear_flag(r.cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r.cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r.cont, 10, 0);

    r.swatch = lv_obj_create(r.cont);
    lv_obj_set_size(r.swatch, 38, 38);
    lv_obj_set_style_radius(r.swatch, 19, 0);
    lv_obj_set_style_border_width(r.swatch, 1, 0);
    lv_obj_clear_flag(r.swatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(r.swatch, hex_colour(jidx_str(colours_j, i, "")), 0);
    lv_obj_set_style_border_color(r.swatch, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_opa(r.swatch, LV_OPA_COVER, 0);

    r.label = lv_label_create(r.cont);
    double grams = 0.0;
    if (weights_j.is_array() && weights_j.size() > i && weights_j[i].is_number())
      grams = weights_j[i].template get<double>();
    /* lvgl's printf has no float support, so round to grams ourselves */
    if (grams > 0.0)
      lv_label_set_text_fmt(r.label, "%d  %s  %d g", (int)i + 1,
                            jidx_str(types_j, i, "-").c_str(),
                            (int)(grams + 0.5));
    else
      lv_label_set_text_fmt(r.label, "%d  %s  unused", (int)i + 1,
                            jidx_str(types_j, i, "-").c_str());
    lv_obj_set_style_text_font(r.label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(r.label, 210);

    lv_obj_t *arrow = lv_label_create(r.cont);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);

    r.slot_btn = lv_btn_create(r.cont);
    lv_obj_set_size(r.slot_btn, 170, 48);
    lv_obj_set_style_pad_all(r.slot_btn, 4, 0);
    lv_obj_set_flex_flow(r.slot_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.slot_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r.slot_btn, 8, 0);
    lv_obj_add_event_cb(r.slot_btn, &CfsMapping::_slot, LV_EVENT_CLICKED, this);

    r.slot_swatch = lv_obj_create(r.slot_btn);
    lv_obj_set_size(r.slot_swatch, 26, 26);
    lv_obj_set_style_radius(r.slot_swatch, 13, 0);
    lv_obj_set_style_border_width(r.slot_swatch, 1, 0);
    lv_obj_clear_flag(r.slot_swatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r.slot_swatch, LV_OBJ_FLAG_EVENT_BUBBLE);

    r.slot_label = lv_label_create(r.slot_btn);
    lv_obj_set_style_text_font(r.slot_label, &lv_font_montserrat_20, 0);
    lv_obj_add_flag(r.slot_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    rows.push_back(r);
    refresh_row(rows.back());
  }

  lv_label_set_text_fmt(title, "Filament mapping   -   %d filament%s",
                        (int)rows.size(), rows.size() == 1 ? "" : "s");
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

void CfsMapping::handle_slot(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *t = lv_event_get_current_target(e);
  for (auto &r : rows) {
    if (r.slot_btn == t) {
      if (!slots.empty()) r.choice = (r.choice + 1) % (int)slots.size();
      refresh_row(r);
      return;
    }
  }
}

void CfsMapping::handle_print(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  for (auto &r : rows) {
    if (r.choice < 0 || r.choice >= (int)slots.size()) continue;
    if (r.tool < 0 || r.tool >= 16) continue;
    std::string gc = std::string("CFS_MAP_TOOL TOOL=") + kSlotNames[r.tool]
                   + " SLOT=" + slots[r.choice];
    spdlog::debug("cfs mapping: {}", gc);
    ws.gcode_script(gc);
  }

  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
  if (on_confirm) on_confirm();
}

void CfsMapping::handle_cancel(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
}
