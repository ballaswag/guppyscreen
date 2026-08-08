#include "cfs_panel.h"
#include "cfs_override_py.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <vector>
#include <fstream>
#include <sstream>

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(load_filament_img);
LV_IMG_DECLARE(unload_filament_img);

static const char *CFS_GCODE_LOAD    = "BOX_EXTRUDE_MATERIAL TNN={slot}";
static const char *CFS_GCODE_UNLOAD  = "BOX_QUIT_MATERIAL";
static const char *CFS_GCODE_REFRESH = "BOX_INFO_REFRESH ADDR={unit} NUM={num}";

/* Instant apply, via the embedded self-installing cfs_override helper. */
static const char *CFS_GCODE_SET   =
  "CFS_SET_SLOT UNIT={unit} SLOT={letter} CODE={code} COLOR={color}";
static const char *CFS_GCODE_UNSET =
  "CFS_CLEAR_SLOT UNIT={unit} SLOT={letter}";

static const char *HELPER_PATH =
  "/usr/share/klipper/klippy/extras/cfs_override.py";
static const char *PRINTER_CFG =
  "/usr/data/printer_data/config/printer.cfg";

/* ------------------------------------------------------------------ *
 *  Creality material catalogue (66 entries).
 *  Same source as the OrcaSlicer CFS fork: 5-character catalogue id
 *  -> vendor / product / base type.
 *
 *  The stored code is <prefix><id>. Here '0' for Creality and '7' for
 *  Generic; other firmwares use '1'. Lookup therefore ignores the
 *  prefix, exactly like creality_cfs_lookup() in the slicer.
 * ------------------------------------------------------------------ */
struct MatEntry {
  const char *id;
  const char *vendor;
  const char *product;
  const char *type;
  int         group;
};

static const MatEntry kCatalog[] = {
  {"07001", "Creality", "CR-ABS", "ABS", 0},
  {"11001", "Creality", "CR-Nylon", "PA", 0},
  {"06001", "Creality", "CR-PETG", "PETG", 0},
  {"04001", "Creality", "CR-PLA", "PLA", 0},
  {"13001", "Creality", "CR-PLA Carbon", "PLA-CF", 0},
  {"15001", "Creality", "CR-PLA Fluo", "PLA", 0},
  {"14001", "Creality", "CR-PLA Matte", "PLA", 0},
  {"05001", "Creality", "CR-Silk", "PLA", 0},
  {"16001", "Creality", "CR-TPU", "TPU", 0},
  {"17001", "Creality", "CR-Wood", "PLA", 0},
  {"09001", "Creality", "EN-PLA+", "PLA", 0},
  {"09002", "Creality", "ENDER FAST PLA", "PLA", 0},
  {"08001", "Creality", "Ender-PLA", "PLA", 0},
  {"18001", "Creality", "HP Ultra PLA", "PLA", 0},
  {"19001", "Creality", "HP-ASA", "ASA", 0},
  {"10001", "Creality", "HP-TPU", "TPU", 0},
  {"03001", "Creality", "Hyper ABS", "ABS", 0},
  {"01002", "Creality", "Hyper L-W PLA", "PLA", 0},
  {"29001", "Creality", "Hyper Marble", "PLA", 0},
  {"12005", "Creality", "Hyper PA6-CF", "PA6-CF", 0},
  {"12004", "Creality", "Hyper PA612-CF", "PA612-CF", 0},
  {"12003", "Creality", "Hyper PAHT-CF", "PA-CF", 0},
  {"07002", "Creality", "Hyper PC", "PC", 0},
  {"06002", "Creality", "Hyper PETG", "PETG", 0},
  {"06003", "Creality", "Hyper PETG-CF", "PETG-CF", 0},
  {"01001", "Creality", "Hyper PLA", "PLA", 0},
  {"02001", "Creality", "Hyper PLA-CF", "PLA-CF", 0},
  {"12002", "Creality", "Hyper PPA-CF", "PA-CF", 0},
  {"01004", "Creality", "Hyper Stardust", "PLA", 0},
  {"01601", "Creality", "Soleyin Ultra PLA", "PLA", 0},
  {"00004", "Generic", "Generic ABS", "ABS", 1},
  {"00007", "Generic", "Generic ASA", "ASA", 1},
  {"00033", "Generic", "Generic ASA-CF", "ASA-CF", 1},
  {"00010", "Generic", "Generic BVOH", "BVOH", 1},
  {"00012", "Generic", "Generic HIPS", "HIPS", 1},
  {"00008", "Generic", "Generic PA", "PA", 1},
  {"00009", "Generic", "Generic PA-CF", "PA-CF", 1},
  {"00025", "Generic", "Generic PA12-CF", "PA-CF", 1},
  {"00015", "Generic", "Generic PA6-CF", "PA6-CF", 1},
  {"00034", "Generic", "Generic PA6-GF", "PA-GF", 1},
  {"00022", "Generic", "Generic PA612-CF", "PA-CF", 1},
  {"00016", "Generic", "Generic PAHT-CF", "PAHT-CF", 1},
  {"00021", "Generic", "Generic PC", "PC", 1},
  {"00032", "Generic", "Generic PCTG", "PCTG", 1},
  {"00020", "Generic", "Generic PET", "PET", 1},
  {"00013", "Generic", "Generic PET-CF", "PET-CF", 1},
  {"00003", "Generic", "Generic PETG", "PETG", 1},
  {"00014", "Generic", "Generic PETG-CF", "PETG-CF", 1},
  {"00027", "Generic", "Generic PETG-GF", "PETG-GF", 1},
  {"00001", "Generic", "Generic PLA", "PLA", 1},
  {"00006", "Generic", "Generic PLA-CF", "PLA-CF", 1},
  {"00002", "Generic", "Generic PLA-Silk", "PLA", 1},
  {"00019", "Generic", "Generic PP", "PP", 1},
  {"00031", "Generic", "Generic PP-CF", "PP-CF", 1},
  {"00017", "Generic", "Generic PPS", "PPS", 1},
  {"00018", "Generic", "Generic PPS-CF", "PPS-CF", 1},
  {"00011", "Generic", "Generic PVA", "PVA", 1},
  {"00023", "Generic", "Generic Support PA", "PA", 1},
  {"00024", "Generic", "Generic Support PLA", "PLA", 1},
  {"00005", "Generic", "Generic TPU", "TPU", 1},
  {"00026", "Generic", "Generic TPU 64D", "TPU", 1},
  {"E1001", "eSUN", "PLA+", "PLA", 2},
  {"00035", "eSUN", "PLA-LW", "PLA", 2},
  {"P1003", "Polymaker", "Panchroma PLA Matte", "PLA", 2},
  {"P1001", "Polymaker", "Panchroma PLA Satin", "PLA", 2},
  {"P1002", "Polymaker", "PolySonic PLA Pro", "PLA", 2},
};
static const int kCatalogCount = (int)(sizeof(kCatalog) / sizeof(kCatalog[0]));

/* Type ordering: everyday materials first. */
static const char *kTypePriority[] = {
  "PLA", "PETG", "ABS", "ASA", "TPU", "PC", "PA",
  "PLA-CF", "PETG-CF", "PA-CF"
};
static const int kTypePriorityCount = 10;

static int type_rank(const std::string &t) {
  for (int i = 0; i < kTypePriorityCount; i++)
    if (t == kTypePriority[i]) return i;
  return 100;
}

/* Product label: prepend the vendor only when the name does not imply it. */
static std::string product_label(const MatEntry &m) {
  std::string v = m.vendor, p = m.product;
  if (v == "Creality" || p.rfind(v, 0) == 0) return p;
  return v + " " + p;
}

static std::string code_for(const MatEntry &m) {
  std::string id = m.id;
  bool numeric = true;
  for (char c : id) if (!isdigit((unsigned char)c)) numeric = false;
  if (!numeric) return id;
  return (std::string(m.vendor) == "Generic" ? "7" : "0") + id;
}

static const MatEntry *lookup_code(const std::string &code) {
  if (code.empty()) return NULL;
  std::string tail = code.size() > 5 ? code.substr(code.size() - 5) : code;
  for (int i = 0; i < kCatalogCount; i++)
    if (tail == kCatalog[i].id || code == kCatalog[i].id) return &kCatalog[i];
  return NULL;
}

static const char *kColours[] = {
  "FFFFFF", "C0C0C0", "808080", "000000", "FF1E1E",
  "FF8080", "FF614B", "FF8B1F", "FFA800", "FFF014",
  "F4E076", "2ECC40", "0BA37F", "39CCCC", "0074D9",
  "1B3A93", "B2A1E1", "CE58F8", "FF37AF", "BA552A",
};
static const int kColourCount = 20;

static std::string replace_all(std::string s, const std::string &from,
                               const std::string &to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.length(), to); pos += to.length();
  }
  return s;
}

static std::string jstr(const json &j, const char *key,
                        const std::string &def = "") {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (j[key].is_string()) return j[key].template get<std::string>();
  return j[key].dump();
}

static std::string jidx(const json &arr, size_t i,
                        const std::string &def = "") {
  if (!arr.is_array() || arr.size() <= i || arr[i].is_null()) return def;
  if (arr[i].is_string()) return arr[i].template get<std::string>();
  return arr[i].dump();
}

static bool empty_val(const std::string &v) {
  return v.empty() || v == "-1" || v == "None" || v == "none";
}

static std::string read_file(const char *path) {
  std::ifstream in(path);
  if (!in.good()) return std::string();
  std::stringstream ss; ss << in.rdbuf();
  return ss.str();
}

CfsPanel::CfsPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , cont(lv_obj_create(lv_scr_act()))
  , header(lv_obj_create(cont))
  , unit_cont(lv_obj_create(header))
  , state_label(lv_label_create(header))
  , temp_label(lv_label_create(header))
  , hum_label(lv_label_create(header))
  , slots_cont(lv_obj_create(cont))
  , controls(lv_obj_create(cont))
  , load_btn(controls, &load_filament_img, "Load", &CfsPanel::_load, this)
  , unload_btn(controls, &unload_filament_img, "Unload", &CfsPanel::_unload, this)
  , refresh_btn(controls, &refresh_img, "Refresh", &CfsPanel::_refresh, this)
  , back_btn(controls, &back, "Back", &CfsPanel::_back, this)
  , editor(NULL), editor_title(NULL), prev_swatch(NULL), prev_label(NULL)
  , type_roller(NULL), prod_roller(NULL), col_grid(NULL)
  , type_idx(0), prod_idx(0), edit_slot(0)
  , helper_pending(false), unit_idx(0), slot_idx(0)
{
  unit_present.fill(false);
  ensure_helper();

  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  /* -------- header -------- */
  lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(header, 4, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, 14, 0);

  lv_obj_set_size(unit_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(unit_cont, 2, 0);
  lv_obj_set_style_border_width(unit_cont, 0, 0);
  lv_obj_clear_flag(unit_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(unit_cont, LV_FLEX_FLOW_ROW);

  for (int i = 0; i < CFS_MAX_UNITS; i++) {
    lv_obj_t *b = lv_btn_create(unit_cont);
    lv_obj_set_size(b, 54, 38);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_fmt(l, "T%d", i + 1);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, &CfsPanel::_unit, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    unit_btns[i] = b;
  }

  lv_label_set_text(state_label, "CFS --");

  lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
  lv_obj_set_style_bg_color(temp_label, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
  lv_obj_set_style_bg_opa(temp_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(temp_label, 7, 0);
  lv_obj_set_style_radius(temp_label, 6, 0);
  lv_label_set_text(temp_label, "-- C");

  lv_obj_set_style_text_font(hum_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(hum_label, lv_color_white(), 0);
  lv_obj_set_style_bg_color(hum_label, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_set_style_bg_opa(hum_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(hum_label, 7, 0);
  lv_obj_set_style_radius(hum_label, 6, 0);
  lv_label_set_text(hum_label, "-- %");

  /* -------- slots -------- */
  lv_obj_set_width(slots_cont, LV_PCT(100));
  lv_obj_set_flex_grow(slots_cont, 1);
  lv_obj_set_style_pad_all(slots_cont, 3, 0);
  lv_obj_set_style_border_width(slots_cont, 0, 0);
  lv_obj_clear_flag(slots_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(slots_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(slots_cont, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < CFS_SLOTS_PER_UNIT; i++) create_slot(i);

  /* -------- buttons -------- */
  lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(controls, 2, 0);
  lv_obj_set_style_border_width(controls, 0, 0);
  lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  build_editor();
  select_slot(0);

  ws.register_notify_update(this);
  lv_obj_move_background(cont);
}

CfsPanel::~CfsPanel() {
  if (cont != NULL) { lv_obj_del(cont); cont = NULL; }
  ws.unregister_notify_update(this);
}

/* ------------------------------------------------------ helper self-install */

void CfsPanel::ensure_helper() {
  bool changed = false;

  /* No CFS in this firmware: leave the machine alone. */
  {
    std::ifstream box("/usr/share/klipper/klippy/extras/box.py");
    if (!box.good()) {
      spdlog::info("cfs: no CFS on this printer; helper not installed");
      return;
    }
  }

  if (read_file(HELPER_PATH) != std::string(CFS_OVERRIDE_PY)) {
    std::ofstream out(HELPER_PATH, std::ios::trunc);
    if (out.good()) {
      out << CFS_OVERRIDE_PY;
      out.close();
      changed = true;
      spdlog::info("cfs: helper installed at {}", HELPER_PATH);
    } else {
      spdlog::error("cfs: cannot write {}", HELPER_PATH);
      return;
    }
  }

  std::string cfg = read_file(PRINTER_CFG);
  if (!cfg.empty() && cfg.find("[cfs_override]") == std::string::npos) {
    std::ofstream out(PRINTER_CFG, std::ios::app);
    if (out.good()) {
      out << "\n[cfs_override]\n";
      out.close();
      changed = true;
      spdlog::info("cfs: added [cfs_override] to printer.cfg");
    }
  }

  if (changed) helper_pending = true;   /* restart once when connected */
}

void CfsPanel::create_slot(int idx) {
  SlotView &s = slots[idx];

  s.card = lv_obj_create(slots_cont);
  lv_obj_set_size(s.card, LV_PCT(23), LV_PCT(98));
  lv_obj_set_style_pad_all(s.card, 3, 0);
  lv_obj_set_style_border_width(s.card, 2, 0);
  lv_obj_set_style_border_color(s.card, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
  lv_obj_set_style_radius(s.card, 6, 0);
  lv_obj_clear_flag(s.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s.card, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s.card, &CfsPanel::_slot, LV_EVENT_CLICKED, this);

  s.header = lv_label_create(s.card);
  lv_label_set_text_fmt(s.header, "%c", 'A' + idx);
  lv_obj_set_style_text_font(s.header, &lv_font_montserrat_20, 0);

  s.swatch = lv_obj_create(s.card);
  lv_obj_set_size(s.swatch, 44, 44);
  lv_obj_set_style_radius(s.swatch, 22, 0);
  lv_obj_set_style_border_width(s.swatch, 1, 0);
  lv_obj_set_style_border_color(s.swatch, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_bg_color(s.swatch, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
  lv_obj_clear_flag(s.swatch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s.swatch, LV_OBJ_FLAG_EVENT_BUBBLE);

  s.material = lv_label_create(s.card);
  lv_label_set_text(s.material, "empty");
  lv_obj_set_style_text_align(s.material, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s.material, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s.material, LV_PCT(96));

  s.bar = lv_bar_create(s.card);
  lv_obj_set_size(s.bar, LV_PCT(88), 7);
  lv_bar_set_range(s.bar, 0, 100);
  lv_bar_set_value(s.bar, 0, LV_ANIM_OFF);
  lv_obj_add_flag(s.bar, LV_OBJ_FLAG_EVENT_BUBBLE);

  s.remain = lv_label_create(s.card);
  lv_label_set_text(s.remain, "--");

  s.edit = lv_btn_create(s.card);
  lv_obj_set_size(s.edit, LV_PCT(94), 36);
  lv_obj_set_style_pad_all(s.edit, 2, 0);
  lv_obj_t *el = lv_label_create(s.edit);
  lv_label_set_text(el, LV_SYMBOL_EDIT " Assign");
  lv_obj_center(el);
  lv_obj_add_event_cb(s.edit, &CfsPanel::_edit, LV_EVENT_CLICKED, this);
}

lv_obj_t *CfsPanel::get_container() { return cont; }

/* ========================== EDITOR: type -> product ========================== */

void CfsPanel::build_editor() {
  editor = lv_obj_create(lv_scr_act());
  lv_obj_set_size(editor, LV_PCT(97), LV_PCT(96));
  lv_obj_center(editor);
  lv_obj_set_style_pad_all(editor, 6, 0);
  lv_obj_set_style_radius(editor, 8, 0);
  lv_obj_set_style_border_width(editor, 2, 0);
  lv_obj_set_style_border_color(editor, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_clear_flag(editor, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(editor, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(editor, 5, 0);

  editor_title = lv_label_create(editor);
  lv_label_set_text(editor_title, "Slot");
  lv_obj_set_style_text_font(editor_title, &lv_font_montserrat_20, 0);

  lv_obj_t *mid = lv_obj_create(editor);
  lv_obj_set_width(mid, LV_PCT(100));
  lv_obj_set_flex_grow(mid, 1);
  lv_obj_set_style_pad_all(mid, 0, 0);
  lv_obj_set_style_border_width(mid, 0, 0);
  lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(mid, 6, 0);

  /* --- 1. material type --- */
  lv_obj_t *c1 = lv_obj_create(mid);
  lv_obj_set_size(c1, LV_PCT(23), LV_PCT(100));
  lv_obj_set_style_pad_all(c1, 3, 0);
  lv_obj_set_style_border_width(c1, 0, 0);
  lv_obj_clear_flag(c1, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c1, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c1, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *t1 = lv_label_create(c1);
  lv_label_set_text(t1, "1. Material");
  type_roller = lv_roller_create(c1);
  lv_obj_set_width(type_roller, LV_PCT(100));
  lv_roller_set_visible_row_count(type_roller, 4);
  lv_obj_set_style_text_font(type_roller, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_font(type_roller, &lv_font_montserrat_20, LV_PART_SELECTED);
  lv_obj_add_event_cb(type_roller, &CfsPanel::_typerol, LV_EVENT_VALUE_CHANGED, this);

  /* --- 2. product --- */
  lv_obj_t *c2 = lv_obj_create(mid);
  lv_obj_set_size(c2, LV_PCT(33), LV_PCT(100));
  lv_obj_set_style_pad_all(c2, 3, 0);
  lv_obj_set_style_border_width(c2, 0, 0);
  lv_obj_clear_flag(c2, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c2, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c2, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *t2 = lv_label_create(c2);
  lv_label_set_text(t2, "2. Product");
  prod_roller = lv_roller_create(c2);
  lv_obj_set_width(prod_roller, LV_PCT(100));
  lv_roller_set_visible_row_count(prod_roller, 4);
  lv_obj_set_style_text_font(prod_roller, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_font(prod_roller, &lv_font_montserrat_20, LV_PART_SELECTED);
  lv_obj_add_event_cb(prod_roller, &CfsPanel::_prodrol, LV_EVENT_VALUE_CHANGED, this);

  /* --- 3. colour + live preview --- */
  lv_obj_t *c3 = lv_obj_create(mid);
  lv_obj_set_size(c3, LV_PCT(42), LV_PCT(100));
  lv_obj_set_style_pad_all(c3, 3, 0);
  lv_obj_set_style_border_width(c3, 0, 0);
  lv_obj_clear_flag(c3, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c3, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c3, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *prev = lv_obj_create(c3);
  lv_obj_set_size(prev, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(prev, 3, 0);
  lv_obj_set_style_border_width(prev, 0, 0);
  lv_obj_clear_flag(prev, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(prev, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(prev, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(prev, 8, 0);

  prev_swatch = lv_obj_create(prev);
  lv_obj_set_size(prev_swatch, 44, 44);
  lv_obj_set_style_radius(prev_swatch, 22, 0);
  lv_obj_set_style_border_width(prev_swatch, 2, 0);
  lv_obj_set_style_border_color(prev_swatch, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_clear_flag(prev_swatch, LV_OBJ_FLAG_SCROLLABLE);

  prev_label = lv_label_create(prev);
  lv_label_set_text(prev_label, "--");
  lv_label_set_long_mode(prev_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(prev_label, LV_PCT(70));

  col_grid = lv_obj_create(c3);
  lv_obj_set_width(col_grid, LV_PCT(100));
  lv_obj_set_flex_grow(col_grid, 1);
  lv_obj_set_style_pad_all(col_grid, 2, 0);
  lv_obj_set_style_border_width(col_grid, 0, 0);
  lv_obj_clear_flag(col_grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(col_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(col_grid, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col_grid, 5, 0);
  lv_obj_set_style_pad_column(col_grid, 5, 0);

  for (int i = 0; i < kColourCount; i++) {
    lv_obj_t *b = lv_obj_create(col_grid);
    lv_obj_set_size(b, 56, 40);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    bool ok = false;
    lv_obj_set_style_bg_color(b, parse_color(kColours[i], &ok), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, (void*)(intptr_t)i);
    lv_obj_add_event_cb(b, &CfsPanel::_colpick, LV_EVENT_CLICKED, this);
  }

  /* --- actions --- */
  lv_obj_t *row = lv_obj_create(editor);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(row, 2, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  struct { const char *t; lv_event_cb_t cb; lv_palette_t c; } acts[] = {
    { LV_SYMBOL_OK " Apply",     &CfsPanel::_apply,  LV_PALETTE_GREEN  },
    { LV_SYMBOL_TRASH " Clear",   &CfsPanel::_clear,  LV_PALETTE_ORANGE },
    { LV_SYMBOL_CLOSE " Cancel", &CfsPanel::_cancel, LV_PALETTE_GREY   },
  };
  for (auto &a : acts) {
    lv_obj_t *b = lv_btn_create(row);
    lv_obj_set_size(b, 175, 48);
    lv_obj_set_style_bg_color(b, lv_palette_main(a.c), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, a.t);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, a.cb, LV_EVENT_CLICKED, this);
  }

  rebuild_types();
  lv_obj_add_flag(editor, LV_OBJ_FLAG_HIDDEN);
}

void CfsPanel::rebuild_types() {
  types.clear();
  for (int i = 0; i < kCatalogCount; i++) {
    std::string t = kCatalog[i].type;
    if (std::find(types.begin(), types.end(), t) == types.end()) types.push_back(t);
  }
  std::sort(types.begin(), types.end(),
            [](const std::string &a, const std::string &b) {
              int ra = type_rank(a), rb = type_rank(b);
              if (ra != rb) return ra < rb;
              return a < b;
            });
  std::string opts;
  for (size_t i = 0; i < types.size(); i++) {
    if (i) opts += "\n";
    opts += types[i];
  }
  lv_roller_set_options(type_roller, opts.c_str(), LV_ROLLER_MODE_NORMAL);
  if (type_idx < 0 || type_idx >= (int)types.size()) type_idx = 0;
  lv_roller_set_selected(type_roller, type_idx, LV_ANIM_OFF);
  rebuild_products();
}

void CfsPanel::rebuild_products() {
  prods.clear();
  if (type_idx >= 0 && type_idx < (int)types.size()) {
    const std::string &t = types[type_idx];
    for (int i = 0; i < kCatalogCount; i++)
      if (t == kCatalog[i].type) prods.push_back(i);
  }
  std::string opts;
  for (size_t i = 0; i < prods.size(); i++) {
    if (i) opts += "\n";
    opts += product_label(kCatalog[prods[i]]);
  }
  if (opts.empty()) opts = "--";
  lv_roller_set_options(prod_roller, opts.c_str(), LV_ROLLER_MODE_NORMAL);
  if (prod_idx < 0 || prod_idx >= (int)prods.size()) prod_idx = 0;
  lv_roller_set_selected(prod_roller, prod_idx, LV_ANIM_OFF);
  update_preview();
}

void CfsPanel::update_preview() {
  if (prod_idx >= 0 && prod_idx < (int)prods.size()) {
    const MatEntry &m = kCatalog[prods[prod_idx]];
    lv_label_set_text(prev_label, product_label(m).c_str());
  } else {
    lv_label_set_text(prev_label, "--");
  }
  bool ok = false;
  lv_obj_set_style_bg_color(prev_swatch, parse_color(pick_colour, &ok), 0);
  lv_obj_set_style_bg_opa(prev_swatch, LV_OPA_COVER, 0);

  for (int i = 0; i < kColourCount; i++) {
    lv_obj_t *b = lv_obj_get_child(col_grid, i);
    if (b == NULL) continue;
    bool sel = (pick_colour == kColours[i]);
    lv_obj_set_style_border_color(b, sel ? lv_palette_main(LV_PALETTE_BLUE)
                                         : lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_border_width(b, sel ? 5 : 2, 0);
  }
}

void CfsPanel::open_editor() {
  lv_label_set_text_fmt(editor_title, "Slot %s", slot_id(edit_slot).c_str());

  json &box = State::get_instance()
    ->get_data(json::json_pointer("/printer_state/box"));
  std::string cur_code, cur_col;
  if (!box.is_null() && box.contains(unit_key())) {
    cur_code = jidx(box[unit_key()]["material_type"], edit_slot, "");
    cur_col  = jidx(box[unit_key()]["color_value"], edit_slot, "");
  }

  type_idx = 0; prod_idx = 0;
  const MatEntry *cur = lookup_code(cur_code);
  if (cur != NULL) {
    rebuild_types();
    for (size_t i = 0; i < types.size(); i++)
      if (types[i] == cur->type) { type_idx = (int)i; break; }
    lv_roller_set_selected(type_roller, type_idx, LV_ANIM_OFF);
    rebuild_products();
    for (size_t i = 0; i < prods.size(); i++)
      if (&kCatalog[prods[i]] == cur) { prod_idx = (int)i; break; }
    lv_roller_set_selected(prod_roller, prod_idx, LV_ANIM_OFF);
  } else {
    rebuild_types();
  }

  pick_colour.clear();
  if (!empty_val(cur_col)) {
    std::string h = cur_col.size() > 6 ? cur_col.substr(cur_col.size() - 6) : cur_col;
    for (auto &ch : h) ch = toupper((unsigned char)ch);
    pick_colour = h;
  }

  update_preview();
  lv_obj_clear_flag(editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(editor);
}

void CfsPanel::close_editor() {
  lv_obj_add_flag(editor, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(editor);
}

void CfsPanel::handle_edit_click(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *t = lv_event_get_current_target(e);
  for (int i = 0; i < CFS_SLOTS_PER_UNIT; i++)
    if (slots[i].edit == t) { edit_slot = i; select_slot(i); open_editor(); return; }
}

void CfsPanel::handle_type_roller(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  type_idx = (int)lv_roller_get_selected(type_roller);
  prod_idx = 0;
  rebuild_products();
}

void CfsPanel::handle_prod_roller(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  prod_idx = (int)lv_roller_get_selected(prod_roller);
  update_preview();
}

void CfsPanel::handle_col_pick(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
  if (i >= 0 && i < kColourCount) pick_colour = kColours[i];
  update_preview();
}

void CfsPanel::apply_now() {
  if (prod_idx < 0 || prod_idx >= (int)prods.size()) return;
  std::string code = code_for(kCatalog[prods[prod_idx]]);
  std::string gc = CFS_GCODE_SET;
  gc = replace_all(gc, "{unit}",   std::to_string(unit_idx + 1));
  gc = replace_all(gc, "{letter}", std::string(1, (char)('A' + edit_slot)));
  gc = replace_all(gc, "{code}",   code);
  gc = replace_all(gc, "{color}",  pick_colour.empty() ? std::string("000000")
                                                       : pick_colour);
  send_gcode(gc);
}

void CfsPanel::handle_apply(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  apply_now();
  close_editor();
}

void CfsPanel::handle_clear(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  std::string gc = CFS_GCODE_UNSET;
  gc = replace_all(gc, "{unit}",   std::to_string(unit_idx + 1));
  gc = replace_all(gc, "{letter}", std::string(1, (char)('A' + edit_slot)));
  send_gcode(gc);
  close_editor();
}

void CfsPanel::handle_cancel(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  close_editor();
}

/* ============================== helpers ============================== */

lv_color_t CfsPanel::parse_color(const std::string &raw, bool *ok) {
  if (ok) *ok = false;
  if (empty_val(raw)) return lv_palette_darken(LV_PALETTE_GREY, 3);
  std::string hex = raw;
  if (hex.size() > 6) hex = hex.substr(hex.size() - 6);
  if (hex.size() != 6) return lv_palette_darken(LV_PALETTE_GREY, 3);
  for (char ch : hex)
    if (!isxdigit((unsigned char)ch)) return lv_palette_darken(LV_PALETTE_GREY, 3);
  if (ok) *ok = true;
  return lv_color_hex((uint32_t)strtoul(hex.c_str(), NULL, 16));
}

std::string CfsPanel::unit_key() const {
  return std::string("T") + std::to_string(unit_idx + 1);
}
std::string CfsPanel::slot_id(int idx) const { return unit_key() + (char)('A' + idx); }
std::string CfsPanel::slot_id() const { return slot_id(slot_idx); }

std::string CfsPanel::material_name(const std::string &code) const {
  if (empty_val(code)) return "empty";
  const MatEntry *m = lookup_code(code);
  if (m != NULL) return product_label(*m);
  auto it = material_names.find(code);
  if (it != material_names.end()) return it->second;
  return code;
}

void CfsPanel::learn_materials(json &same_material) {
  if (!same_material.is_array()) return;
  for (auto &e : same_material) {
    if (!e.is_array() || e.size() < 4) continue;
    if (!e[0].is_string() || !e[3].is_string()) continue;
    material_names[e[0].template get<std::string>()] =
      e[3].template get<std::string>();
  }
}

void CfsPanel::select_slot(int idx) {
  if (idx < 0 || idx >= CFS_SLOTS_PER_UNIT) return;
  slot_idx = idx;
  for (int i = 0; i < CFS_SLOTS_PER_UNIT; i++) {
    bool sel = (i == slot_idx);
    lv_obj_set_style_border_color(slots[i].card,
      sel ? lv_palette_main(LV_PALETTE_BLUE)
          : lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_border_width(slots[i].card, sel ? 3 : 2, 0);
  }
}

void CfsPanel::select_unit(int idx) {
  if (idx < 0 || idx >= CFS_MAX_UNITS) return;
  unit_idx = idx;
  for (int i = 0; i < CFS_MAX_UNITS; i++) {
    if (i == unit_idx) lv_obj_add_state(unit_btns[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(unit_btns[i], LV_STATE_CHECKED);
  }
  refresh_from_state();
}

void CfsPanel::update_unit_tabs(json &box) {
  int first = -1;
  for (int i = 0; i < CFS_MAX_UNITS; i++) {
    std::string key = std::string("T") + std::to_string(i + 1);
    bool present = false;
    if (box.contains(key) && box[key].is_object())
      present = !empty_val(jstr(box[key], "state", "None"));
    unit_present[i] = present;
    if (present) {
      lv_obj_clear_flag(unit_btns[i], LV_OBJ_FLAG_HIDDEN);
      if (first < 0) first = i;
    } else lv_obj_add_flag(unit_btns[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (unit_idx >= 0 && unit_idx < CFS_MAX_UNITS
      && !unit_present[unit_idx] && first >= 0) unit_idx = first;
  for (int i = 0; i < CFS_MAX_UNITS; i++) {
    if (i == unit_idx) lv_obj_add_state(unit_btns[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(unit_btns[i], LV_STATE_CHECKED);
  }
}

void CfsPanel::update_header(json &box, json &unit) {
  std::string st = jstr(box, "state", "--");
  bool refill = false;
  if (box.contains("auto_refill") && !box["auto_refill"].is_null()) {
    refill = box["auto_refill"].is_number()
      ? (box["auto_refill"].template get<int>() != 0)
      : (jstr(box, "auto_refill") == "1");
  }
  lv_label_set_text_fmt(state_label, "CFS %s%s", st.c_str(),
                        refill ? "  auto-refill" : "");
  if (unit.is_object()) {
    lv_label_set_text_fmt(temp_label, "%s C",
                          jstr(unit, "temperature", "--").c_str());
    lv_label_set_text_fmt(hum_label, "%s %%",
                          jstr(unit, "dry_and_humidity", "--").c_str());
  } else {
    lv_label_set_text(temp_label, "-- C");
    lv_label_set_text(hum_label, "-- %");
  }
}

void CfsPanel::update_slots(json &unit) {
  for (int i = 0; i < CFS_SLOTS_PER_UNIT; i++) {
    SlotView &s = slots[i];
    std::string colour, mat, rem;
    if (unit.is_object()) {
      colour = jidx(unit["color_value"], i, "-1");
      mat    = jidx(unit["material_type"], i, "-1");
      rem    = jidx(unit["remain_len"], i, "-1");
    }
    bool ok = false;
    lv_obj_set_style_bg_color(s.swatch, parse_color(colour, &ok), 0);
    lv_obj_set_style_bg_opa(s.swatch, LV_OPA_COVER, 0);
    lv_label_set_text(s.material, material_name(mat).c_str());

    if (empty_val(rem)) {
      lv_bar_set_value(s.bar, 0, LV_ANIM_OFF);
      lv_label_set_text(s.remain, "--");
    } else {
      int pct = atoi(rem.c_str());
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      lv_bar_set_value(s.bar, pct, LV_ANIM_OFF);
      lv_label_set_text_fmt(s.remain, "%d%%", pct);
    }
  }
}

void CfsPanel::update_box(json &box) {
  if (!box.is_object()) return;
  if (box.contains("same_material")) learn_materials(box["same_material"]);
  update_unit_tabs(box);
  json empty = json::object();
  json &unit = (box.contains(unit_key()) && box[unit_key()].is_object())
                 ? box[unit_key()] : empty;
  update_header(box, unit);
  update_slots(unit);
}

void CfsPanel::refresh_from_state() {
  json &box = State::get_instance()
    ->get_data(json::json_pointer("/printer_state/box"));
  if (box.is_null()) { lv_label_set_text(state_label, "CFS not detected"); return; }
  update_box(box);
}

void CfsPanel::consume(json &j) {
  if (helper_pending) {          /* helper just installed or updated */
    helper_pending = false;
    spdlog::info("cfs: restarting the klipper service once to load the helper");
    /* A firmware_restart is not enough: python caches already-imported
       modules, so an updated helper would keep the old code. Restarting the
       service re-execs klippy and picks it up. */
    json params = {{"service", "klipper"}};
    ws.send_jsonrpc("machine.services.restart", params);
    return;
  }
  json &box = j[json::json_pointer("/params/0/box")];
  if (box.is_null()) return;
  std::lock_guard<std::mutex> lock(lv_lock);
  json &cached = State::get_instance()
    ->get_data(json::json_pointer("/printer_state/box"));
  if (!cached.is_null()) {
    json merged = cached; merged.merge_patch(box); update_box(merged);
  } else update_box(box);
}

void CfsPanel::foreground() {
  refresh_from_state();
  lv_obj_move_foreground(cont);
}

void CfsPanel::send_gcode(const std::string &gcode) {
  spdlog::debug("cfs gcode: {}", gcode);
  ws.gcode_script(gcode);
}

void CfsPanel::confirm_and_send(const std::string &question,
                                const std::string &gcode) {
  static const char *btns[] = {"Confirm", "Cancel", ""};
  std::string *payload = new std::string(gcode);
  lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, question.c_str(), btns, false);
  lv_obj_t *msg = ((lv_msgbox_t*)mbox)->text;
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(msg, LV_PCT(100));
  lv_obj_center(msg);
  lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
  lv_btnmatrix_set_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_btnmatrix_set_btn_ctrl(btnm, 1, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_size(btnm, LV_PCT(90), 50);
  lv_obj_set_size(mbox, LV_PCT(66), LV_PCT(44));
  lv_obj_set_user_data(mbox, payload);
  lv_obj_add_event_cb(mbox, [](lv_event_t *e) {
    lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(e));
    CfsPanel *panel = (CfsPanel*)e->user_data;
    std::string *gc = (std::string*)lv_obj_get_user_data(obj);
    if (lv_msgbox_get_active_btn(obj) == 0 && gc != NULL) panel->send_gcode(*gc);
    delete gc;
    lv_obj_set_user_data(obj, NULL);
    lv_msgbox_close(obj);
  }, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_center(mbox);
}

void CfsPanel::handle_slot_click(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *t = lv_event_get_current_target(e);
  for (int i = 0; i < CFS_SLOTS_PER_UNIT; i++)
    if (slots[i].card == t) { select_slot(i); return; }
}

void CfsPanel::handle_unit_click(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *t = lv_event_get_current_target(e);
  for (int i = 0; i < CFS_MAX_UNITS; i++)
    if (unit_btns[i] == t) { select_unit(i); return; }
}

void CfsPanel::handle_load(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  confirm_and_send("Load " + slot_id() + "?\nThe toolhead will move and the "
                   "nozzle will heat.",
                   replace_all(CFS_GCODE_LOAD, "{slot}", slot_id()));
}

void CfsPanel::handle_unload(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  confirm_and_send("Unload current filament?\nThe toolhead will move and the "
                   "nozzle will heat.",
                   replace_all(CFS_GCODE_UNLOAD, "{slot}", slot_id()));
}

void CfsPanel::handle_refresh(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  std::string gc = replace_all(CFS_GCODE_REFRESH, "{unit}",
                               std::to_string(unit_idx + 1));
  gc = replace_all(gc, "{num}", std::to_string(slot_idx));
  gc = replace_all(gc, "{slot}", slot_id());
  send_gcode(gc);
}

void CfsPanel::handle_back(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (lv_event_get_current_target(e) == back_btn.get_container())
    lv_obj_move_background(cont);
}
