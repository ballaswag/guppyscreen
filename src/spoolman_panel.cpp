#include "spoolman_panel.h"
#include "mmu_panel.h"  // parse_colour
#include "theme.h"
#include "utils.h"
#include "logger.h"
#include "icons.h"

#include <cstdlib>

using namespace Theme;

#define SORTED_BY_ID   (1 << 0)
#define SORTED_BY_NAME (1 << 1)
#define SORTED_BY_MAT  (1 << 2)
#define SORTED_BY_WT   (1 << 3)
#define SORTED_BY_LEN  (1 << 4)

// how far the header row and every other body row lean towards the accent
// and the raised grey, so the eye can follow a row across eight columns
static const lv_opa_t HEADER_TINT = LV_OPA_20;
static const lv_opa_t ZEBRA_TINT = LV_OPA_40;

static std::string string_at(const json &j, const json::json_pointer &ptr, const std::string &def = "") {
  if (!j.contains(ptr)) {
    return def;
  }
  const auto &v = j[ptr];
  return v.is_string() ? v.template get<std::string>() : def;
}

static double number_at(const json &j, const json::json_pointer &ptr, double def = 0.0) {
  if (!j.contains(ptr)) {
    return def;
  }
  const auto &v = j[ptr];
  return v.is_number() ? v.template get<double>() : def;
}

static uint32_t uint_at(const json &j, const json::json_pointer &ptr, uint32_t def = 0) {
  if (!j.contains(ptr)) {
    return def;
  }
  const auto &v = j[ptr];
  return v.is_number_unsigned() || v.is_number_integer() ? v.template get<uint32_t>() : def;
}

static bool bool_at(const json &j, const json::json_pointer &ptr, bool def = false) {
  if (!j.contains(ptr)) {
    return def;
  }
  const auto &v = j[ptr];
  return v.is_boolean() ? v.template get<bool>() : def;
}

// "Vendor - Filament", either half blank when spoolman has none
static std::string spool_name(const json &spool) {
  return fmt::format("{} - {}",
                     string_at(spool, "/filament/vendor/name"_json_pointer),
                     string_at(spool, "/filament/name"_json_pointer));
}

SpoolmanPanel::SpoolmanPanel(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
  , cont(create_screen(NULL))
  , table_box(create_row(cont))
  , spool_table(lv_table_create(table_box))
  , empty_box(create_row(cont))
  , controls(create_row(cont))
  , switch_cont(create_row(controls))
  , show_archived(lv_switch_create(switch_cont))
  , reload_btn(controls, Icons::REFRESH_IMG, "Reload", &SpoolmanPanel::_handle_callback, this)
  , back_btn(controls, Icons::BACK, "Back", &SpoolmanPanel::_handle_callback, this)
  , active_id(-1)
  , sorted_by(SORTED_BY_ID)
{
  // icon-only square tiles a finger tall: the table gets the rest of the screen
  for (ButtonContainer *b : {&reload_btn, &back_btn}) {
    b->use_plain();
    b->hide_label();
    lv_obj_set_size(b->get_container(), touch_h(), touch_h());
  }
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // the table takes the height above the controls, its scrollbar beside it
  lv_obj_set_width(table_box, LV_PCT(100));
  lv_obj_set_flex_grow(table_box, 1);
  lv_obj_set_flex_flow(table_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(spool_table, 0, LV_PCT(100));
  lv_obj_set_flex_grow(spool_table, 1);
  manage_scroll(spool_table);
  // rows tall enough to tap: the theme pads cells by a gap, these action
  // columns need a finger's worth
  const lv_font_t *cell_font = lv_obj_get_style_text_font(spool_table, LV_PART_ITEMS);
  lv_obj_set_style_pad_ver(spool_table, (touch_h() - lv_font_get_line_height(cell_font)) / 2, LV_PART_ITEMS);

  lv_table_set_col_cnt(spool_table, 8);
  lv_table_set_col_width(spool_table, 0, scale_w(38)); // id
  lv_table_set_col_width(spool_table, 3, scale_w(30)); // color
  lv_table_set_col_width(spool_table, 6, scale_w(44)); // set active
  lv_table_set_col_width(spool_table, 7, scale_w(44)); // archive
  // the flexible columns are set by layout_columns() once the table has a width

  // stands in for the table when there is nothing to list
  lv_obj_set_width(empty_box, LV_PCT(100));
  lv_obj_set_flex_grow(empty_box, 1);
  lv_obj_add_flag(empty_box, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *empty_lbl = lv_label_create(empty_box);
  lv_label_set_text(empty_lbl, "No spools");
  lv_obj_add_style(empty_lbl, &styles().dim_label, 0);
  lv_obj_set_style_text_font(empty_lbl, scale_font(16), 0);
  lv_obj_center(empty_lbl);

  // controls: the archive switch on the left, the two tiles on the right
  lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_pad_left(switch_cont, gap(), 0);  // off the screen edge, like the table border
  lv_obj_set_flex_grow(switch_cont, 1);
  lv_obj_set_height(switch_cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(switch_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(switch_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *label = lv_label_create(switch_cont);
  lv_label_set_text(label, "Show Archived");
  // the same arrangement as a switch in Settings: a dim title, then the switch
  lv_obj_add_style(label, &styles().dim_label, 0);
  lv_obj_move_to_index(label, 0);
  lv_obj_clear_state(show_archived, LV_STATE_CHECKED);
  lv_obj_add_event_cb(show_archived, &SpoolmanPanel::_handle_spoolman_action, LV_EVENT_VALUE_CHANGED, this);


  lv_obj_add_event_cb(spool_table, &SpoolmanPanel::_handle_spoolman_action, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(spool_table, &SpoolmanPanel::_handle_spoolman_action, LV_EVENT_DRAW_PART_BEGIN, this);

  // shown above the table while picking a spool for an MMU lane
  pick_bar = create_row(cont);
  lv_obj_move_to_index(pick_bar, 0);
  lv_obj_set_size(pick_bar, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(pick_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pick_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  pick_label = lv_label_create(pick_bar);
  lv_obj_set_flex_grow(pick_label, 1);
  lv_obj_set_style_pad_left(pick_label, gap(), 0);
  lv_obj_add_flag(pick_bar, LV_OBJ_FLAG_HIDDEN);
}

SpoolmanPanel::~SpoolmanPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SpoolmanPanel::init() {
  // Register only when the global tab or MMU picker actually uses the list.
  if (!listening) {
    ws.register_method_callback("notify_active_spool_set", "SpoolmanPanel",
                                [this](json &d) { handle_active_id_update(d); });
    listening = true;
  }
  json param = {
    { "request_method", "GET" },
    { "path", "/v1/spool?allow_archived=true" },
  };

  ws.send_jsonrpc("server.spoolman.proxy", param, [this](json &d) {
    const auto result_ptr = "/result"_json_pointer;
    if (d.contains(result_ptr) && d[result_ptr].is_array()) {
      std::map<uint32_t, SpoolRec> fresh;
      for (auto &e : d[result_ptr]) {
        if (!e.contains("id") || !e["id"].is_number()) {
          continue;
        }
        SpoolRec r;
        r.id = uint_at(e, "/id"_json_pointer);
        r.name = spool_name(e);
        r.material = string_at(e, "/filament/material"_json_pointer);
        r.colour = string_at(e, "/filament/color_hex"_json_pointer);
        r.weight = number_at(e, "/remaining_weight"_json_pointer);
        r.length = number_at(e, "/remaining_length"_json_pointer) / 1000;  // mm to m
        r.archived = bool_at(e, "/archived"_json_pointer);
        fresh.insert({r.id, std::move(r)});
      }

      std::lock_guard<std::mutex> lock(this->lv_lock);
      spools.swap(fresh);
      repopulate();
    }
  });

  ws.send_jsonrpc("server.spoolman.get_spool_id", [this](json &d) {
    if (get_log_level() <= LogLevel::TRACE) {
      LOG_TRACE("got spool active id {}", d.dump());
    }
    const auto spool_id_ptr = "/result/spool_id"_json_pointer;
    if (d.contains("/result"_json_pointer)) {
      // null: no active spool
      const bool set = d.contains(spool_id_ptr) && d[spool_id_ptr].is_number();
      std::lock_guard<std::mutex> lock(this->lv_lock);
      this->active_id = set ? d[spool_id_ptr].template get<int>() : -1;
      repopulate();
    }
  });
}

void SpoolmanPanel::attach(lv_obj_t *parent) {
  tab_parent = parent;
  lv_obj_set_parent(cont, parent);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  back_btn.hide();
}

void SpoolmanPanel::detach() {
  end_pick();
  tab_parent = NULL;
  lv_obj_set_parent(cont, lv_scr_act());
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
}

void SpoolmanPanel::foreground() {
  end_pick();
  if (tab_parent == NULL) {
    back_btn.show();
  }
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
  init();
}

void SpoolmanPanel::background() {
  end_pick();
  if (tab_parent == NULL) {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

void SpoolmanPanel::pick(const std::string &lane, int current_spool, std::function<void(int)> done) {
  lv_obj_set_parent(cont, lv_scr_act());
  back_btn.show();
  pick_done = std::move(done);
  pick_current = current_spool;
  lv_label_set_text(pick_label, fmt::format("Spool for {}", lane).c_str());
  lv_obj_clear_flag(pick_bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
  repopulate();
  init();
}

void SpoolmanPanel::end_pick() {
  pick_done = nullptr;
  pick_current = -1;
  lv_obj_add_flag(pick_bar, LV_OBJ_FLAG_HIDDEN);
  if (tab_parent != NULL) {
    lv_obj_set_parent(cont, tab_parent);
    back_btn.hide();
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void SpoolmanPanel::repopulate() {
  std::vector<SpoolRec> sorted_spools;
  KUtils::sort_map_values<uint32_t, SpoolRec>(spools, sorted_spools, [](SpoolRec &a, SpoolRec &b) {
    return a.id < b.id;
  });
  sorted_by = SORTED_BY_ID;
  populate_spools(sorted_spools);
}

void SpoolmanPanel::populate_spools(std::vector<SpoolRec> &sorted_spools) {
  size_t row_idx = 1;
  if (!spools.empty()) {
    lv_table_set_cell_value(spool_table, 0, 0, "ID");
    lv_table_set_cell_value(spool_table, 0, 1, "Name");
    lv_table_set_cell_value(spool_table, 0, 2, "MAT");
    lv_table_set_cell_value(spool_table, 0, 4, "Weight");
    lv_table_set_cell_value(spool_table, 0, 5, "Length");

    bool skip_archive = !lv_obj_has_state(show_archived, LV_STATE_CHECKED);

    for (auto &el : sorted_spools) {
      bool is_archived = el.archived;
      if (skip_archive && is_archived) {
	      continue;
      }

      auto id = el.id;
      bool is_active = (int32_t)id == active_id;

      const std::string &material = el.material;
      const double remaining_weight = el.weight;
      const double remaining_len = el.length;

      lv_table_set_cell_value(spool_table, row_idx, 0, std::to_string(id).c_str());
      // a second line for the lanes it is in
      std::string name = el.name;
      const std::string lanes = lanes_of ? lanes_of((int)id) : "";
      if (!lanes.empty()) {
        name += "\n" + lanes;
      }
      lv_table_set_cell_value(spool_table, row_idx, 1, name.c_str());
      lv_table_set_cell_value(spool_table, row_idx, 2, material.c_str());
      lv_table_set_cell_value(spool_table, row_idx, 3, "");

      lv_table_set_cell_value(spool_table, row_idx, 4, fmt::format("{:.1f} g", remaining_weight).c_str());
      lv_table_set_cell_value(spool_table, row_idx, 5, fmt::format("{:.1f} m", remaining_len).c_str());

      lv_table_clear_cell_ctrl(spool_table, row_idx, 6, LV_TABLE_CELL_CTRL_MERGE_RIGHT);

      if (is_archived) {
	      lv_table_set_cell_value(spool_table, row_idx, 7, LV_SYMBOL_UPLOAD);
      } else {
        if (!is_active) {
          // prevent archiving active spool
          lv_table_set_cell_value(spool_table, row_idx, 7, LV_SYMBOL_DRIVE);
        }
      }

      if (is_active) {
        lv_table_add_cell_ctrl(spool_table, row_idx, 6, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
        lv_table_set_cell_value(spool_table, row_idx, 6, "(active)");
      } else {
        if (is_archived) {
          lv_table_set_cell_value(spool_table, row_idx, 6, "");
        } else {
          lv_table_set_cell_value(spool_table, row_idx, 6, LV_SYMBOL_PLAY);
        }
      }
      if (pick_done && (int)id == pick_current) {
        lv_table_add_cell_ctrl(spool_table, row_idx, 6, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
        lv_table_set_cell_value(spool_table, row_idx, 6, "Remove");
      }
      row_idx++;
    }
    lv_table_set_row_cnt(spool_table, row_idx);
  }

  // nothing listed (no spools, or all of them archived): say so instead of
  // showing a bare header
  const bool empty = row_idx <= 1;
  if (empty) {
    lv_obj_add_flag(table_box, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_clear_flag(table_box, LV_OBJ_FLAG_HIDDEN);
  }
  if (empty) {
    lv_obj_clear_flag(empty_box, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(empty_box, LV_OBJ_FLAG_HIDDEN);
  }

  // the bar takes or gives back its lane first, then the columns share the
  // width the table is left with (names wrap, so a narrower table only gets
  // taller and a wider one shorter: this settles)
  lv_obj_update_layout(table_box);
  refresh_scroll(spool_table);
  layout_columns();
}

void SpoolmanPanel::layout_columns() {
  lv_obj_update_layout(table_box);
  const int fixed = scale_w(38) + scale_w(30) + scale_w(44) + scale_w(44);
  const int remain = lv_obj_get_content_width(spool_table) - fixed;
  const int len_field_width = remain * 23 / 100;
  const int material_width = remain * 17 / 100;
  lv_table_set_col_width(spool_table, 1, remain - 2 * len_field_width - material_width); // name - product
  lv_table_set_col_width(spool_table, 2, material_width); // material
  lv_table_set_col_width(spool_table, 4, len_field_width);
  lv_table_set_col_width(spool_table, 5, len_field_width);
}

void SpoolmanPanel::handle_active_id_update(json &j) {
  if (get_log_level() <= LogLevel::TRACE) {
    LOG_TRACE("active spool id update {}", j.dump());
  }
  const auto spool_id_ptr = "/params/0/spool_id"_json_pointer;
  // null when the active spool was cleared
  const bool set = j.contains(spool_id_ptr) && j[spool_id_ptr].is_number();
  std::lock_guard<std::mutex> lock(lv_lock);
  active_id = set ? j[spool_id_ptr].template get<int>() : -1;
  repopulate();
}

void SpoolmanPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    LOG_TRACE("spoolman back button pressed");
    background();
  } else if (btn == reload_btn.get_container()) {
    LOG_TRACE("spoolman reload button pressed");
    init();
  }
}

void SpoolmanPanel::handle_spoolman_action(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *clicked = lv_event_get_target(e);
    if (clicked == show_archived) {
      repopulate();  // already on the UI thread, which holds lv_lock
      return;
    }

    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(spool_table, &row, &col);
    uint16_t row_count = lv_table_get_row_cnt(spool_table);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE || row >= row_count) {
      return;
    }

    if (row == 0) {
      if (col == 0) {
        // sort by id
        bool reversed = sorted_by & SORTED_BY_ID;
        std::vector<SpoolRec> sorted_spools;
        KUtils::sort_map_values<uint32_t, SpoolRec>(spools, sorted_spools, [reversed](SpoolRec &a, SpoolRec &b) {
          auto x = a.id;
          auto y = b.id;
	        return reversed ? x > y : y > x;
	      });

        sorted_by = (sorted_by ^ SORTED_BY_ID) & SORTED_BY_ID;
        populate_spools(sorted_spools);
      } else if (col == 1) {
        // sort by name
        bool reversed = sorted_by & SORTED_BY_NAME;
        std::vector<SpoolRec> sorted_spools;
        KUtils::sort_map_values<uint32_t, SpoolRec>(spools, sorted_spools, [reversed](SpoolRec &a, SpoolRec &b) {
          auto x = a.name;
          auto y = b.name;
          return reversed ? x > y : y > x;
        });
        sorted_by = (sorted_by ^ SORTED_BY_NAME) & SORTED_BY_NAME;
        populate_spools(sorted_spools);
      } else if (col == 2) {
        // sort by material
        bool reversed = sorted_by & SORTED_BY_MAT;
        std::vector<SpoolRec> sorted_spools;
        KUtils::sort_map_values<uint32_t, SpoolRec>(spools, sorted_spools, [reversed](SpoolRec &a, SpoolRec &b) {
          auto x = a.material;
          auto y = b.material;

          return reversed ? x > y : y > x;
        });
        sorted_by = (sorted_by ^ SORTED_BY_MAT) & SORTED_BY_MAT;

        populate_spools(sorted_spools);
      } else if (col == 3) {
        // sort by color
        // TODO: calculate color distance
      } else if (col == 4) {
        // sort by weight
        bool reversed = sorted_by & SORTED_BY_WT;
        std::vector<SpoolRec> sorted_spools;
        KUtils::sort_map_values<uint32_t, SpoolRec>(spools, sorted_spools, [reversed](SpoolRec &a, SpoolRec &b) {
          auto x = a.weight;
          auto y = b.weight;

          return reversed ? x > y : y > x;
        });
        sorted_by = (sorted_by ^ SORTED_BY_WT) & SORTED_BY_WT;

        populate_spools(sorted_spools);
      } else if (col == 5) {
        // sort by length
        bool reversed = sorted_by & SORTED_BY_LEN;
        std::vector<SpoolRec> sorted_spools;
        KUtils::sort_map_values<uint32_t, SpoolRec>(spools, sorted_spools, [reversed](SpoolRec &a, SpoolRec &b) {
          auto x = a.length;
          auto y = b.length;
          return reversed ? x > y : y > x;
        });
        sorted_by = (sorted_by ^ SORTED_BY_LEN) & SORTED_BY_LEN;
        populate_spools(sorted_spools);
      }
    }
    
    const char *selected = lv_table_get_cell_value(spool_table, row, col);
    const char *spool_id = lv_table_get_cell_value(spool_table, row, 0);
    LOG_TRACE("selected {}, {}, value {}", row, col, spool_id);
    if (row != 0) {
      if (pick_done && spool_id != NULL && spool_id[0] != '\0') {
        auto done = pick_done;
        const int id = std::atoi(spool_id);
        const bool remove = id == pick_current;
        end_pick();
        if (tab_parent == NULL) {
          lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
          lv_obj_move_background(cont);
        }
        done(remove ? -1 : id);
        return;
      }
      // "(active)" spans both action columns: a tap on it clears the active spool
      const char *marker = lv_table_get_cell_value(spool_table, row, 6);
      if ((col == 6 || col == 7) && marker != NULL && strcmp(marker, "(active)") == 0) {
        ws.send_jsonrpc("server.spoolman.post_spool_id");
        return;
      }
      if (col == 6 && selected != NULL && strlen(selected) != 0 && std::memcmp(LV_SYMBOL_PLAY, selected, 3) == 0) {
        // set active spool
        int id = std::stoi(spool_id);
        LOG_TRACE("set active spool id {}", id);
        json param = {
          {"spool_id", id}
        };

        ws.send_jsonrpc("server.spoolman.post_spool_id", param);
      }

      if (col == 7 && selected != NULL && strlen(selected) != 0) {
        if (std::memcmp(LV_SYMBOL_DRIVE, selected, 3) == 0) {
          // archive
          json param = {
            { "request_method", "PATCH" },
            { "path", fmt::format("/v1/spool/{}", spool_id) },
            { "body", {
                { "archived", true }
              }
            }
          };
          ws.send_jsonrpc("server.spoolman.proxy", param, [this](json &d) {
            this->init();
          });
        } else if (std::memcmp(LV_SYMBOL_UPLOAD, selected, 3) == 0) {
          // unarchive
          json param = {
            { "request_method", "PATCH" },
            { "path", fmt::format("/v1/spool/{}", spool_id) },
            { "body", {
          { "archived", false }
              }
            }
          };

          ws.send_jsonrpc("server.spoolman.proxy", param, [this](json &d) {
            this->init();
          });
        }
      }
    }
  } else if (code == LV_EVENT_DRAW_PART_BEGIN) {
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc->part == LV_PART_ITEMS) {
      uint32_t row = dsc->id /  lv_table_get_col_cnt(spool_table);
      uint32_t col = dsc->id - row * lv_table_get_col_cnt(spool_table);

      if(row == 0) {
        dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
        dsc->rect_dsc->bg_color = lv_color_mix(theme_primary(), dsc->rect_dsc->bg_color, HEADER_TINT);
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
      }

      if (row != 0 && col == 3) {
        const char *spool_id = lv_table_get_cell_value(spool_table, row, 0);
        uint32_t id = std::stoi(spool_id);
        const auto &spool = spools.find(id);
        if (spool != spools.end()) {
          const auto &color = spool->second.colour;
          lv_color_t colour;
          // a spool without a colour, or with one spoolman spells oddly, keeps the row colour
          if (parse_colour(color, &colour)) {
            dsc->rect_dsc->bg_color = colour;
          }
        }
      }

      if((row != 0 && row % 2) == 0) {
        dsc->rect_dsc->bg_color = lv_color_mix(Theme::col(RAISED), dsc->rect_dsc->bg_color, ZEBRA_TINT);  // `col` is the cell column here
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
      }

      // the lane's own spool while picking
      if (row != 0 && col != 3 && pick_done && pick_current >= 0) {
        const char *id_txt = lv_table_get_cell_value(spool_table, row, 0);
        if (id_txt != NULL && std::atoi(id_txt) == pick_current) {
          dsc->rect_dsc->bg_color = lv_color_mix(theme_primary(), dsc->rect_dsc->bg_color, LV_OPA_50);
          dsc->rect_dsc->bg_opa = LV_OPA_COVER;
        }
      }
    }
  }
}
