#include "spoolman_panel.h"
#include "utils.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(refresh_img);

#define SORTED_BY_ID   1 << 0
#define SORTED_BY_NAME 1 << 1
#define SORTED_BY_MAT  1 << 2
#define SORTED_BY_WT   1 << 3
#define SORTED_BY_LEN  1 << 4

SpoolmanPanel::SpoolmanPanel(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , spool_table(lv_table_create(cont))
  , controls(lv_obj_create(cont))
  , switch_cont(lv_obj_create(controls))
  , show_archived(lv_switch_create(switch_cont))
  , reload_btn(controls, &refresh_img, "Reload", &SpoolmanPanel::_handle_callback, this)
  , back_btn(controls, &back, "Back", &SpoolmanPanel::_handle_callback, this)
  , active_id(-1)
  , sorted_by(SORTED_BY_ID)
{
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);

  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_set_height(spool_table, LV_PCT(75));
  lv_obj_align(spool_table, LV_ALIGN_TOP_MID, 0, 5);

  lv_table_set_col_cnt(spool_table, 8);
  auto screen_width = lv_disp_get_physical_hor_res(NULL);
  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  lv_table_set_col_width(spool_table, 0, 64 * scale); // id
  lv_table_set_col_width(spool_table, 3, 50 * scale); // color
  lv_table_set_col_width(spool_table, 6, 60 * scale); // set active
  lv_table_set_col_width(spool_table, 7, 60 * scale); // archive

  auto remain_width = screen_width - scale * (60 + 50 + 60 + 60);
  double len_field_width = 0.23 * remain_width;
  double material_width = 0.17 * remain_width;
  int name_width = remain_width - (2 * len_field_width) - material_width;
  lv_table_set_col_width(spool_table, 1, name_width); // name - product
  lv_table_set_col_width(spool_table, 2, material_width); // material
  lv_table_set_col_width(spool_table, 4, len_field_width);
  lv_table_set_col_width(spool_table, 5, len_field_width);
  
  // controls
  lv_obj_set_width(controls, LV_PCT(100));
  lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);

  // switch
  lv_obj_set_width(switch_cont, LV_PCT(50)); // push right
  lv_obj_t *label = lv_label_create(switch_cont);
  lv_label_set_text(label, "Show Archived");
  lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, -10);
  lv_obj_align(show_archived, LV_ALIGN_LEFT_MID, 30, -10);
  lv_obj_clear_state(show_archived, LV_STATE_CHECKED);
  lv_obj_add_event_cb(show_archived, &SpoolmanPanel::_handle_spoolman_action, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_add_event_cb(spool_table, &SpoolmanPanel::_handle_spoolman_action, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(spool_table, &SpoolmanPanel::_handle_spoolman_action, LV_EVENT_DRAW_PART_BEGIN, this);

  ws.register_method_callback("notify_active_spool_set",
			      "SpoolmanPanel",
			      [this](json& d) { this->handle_active_id_update(d); });

}

SpoolmanPanel::~SpoolmanPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SpoolmanPanel::init() {
  json param = {
    { "request_method", "GET" },
    { "path", "/v1/spool?allow_archived=true" },
  };

  ws.send_jsonrpc("server.spoolman.proxy", param, [this](json &d) {
    auto &s = d["/result"_json_pointer];
    if (!s.is_null() && !s.empty()) {
      spools.clear();
      for (auto &e : s) {
        if (e.contains("id")) {
          uint32_t spool_id = e["id"].template get<uint32_t>();
          spools.insert({spool_id, e});
        }
      }

      std::vector<json> sorted_spools;
      KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [](json &a, json &b) {
	return a["id"].template get<uint32_t>() < b["id"].template get<uint32_t>();
      });
      sorted_by = SORTED_BY_ID;
      
      std::lock_guard<std::mutex> lock(this->lv_lock);
      populate_spools(sorted_spools);
    }
  });

  ws.send_jsonrpc("server.spoolman.get_spool_id", [this](json &d) {
    spdlog::trace("got spool active id {}", d.dump());
    auto &v = d["/result/spool_id"_json_pointer];
    if (!v.is_null()) {
      this->active_id = v.template get<int>();

      std::vector<json> sorted_spools;
      KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [](json &a, json &b) {
	return a["id"].template get<uint32_t>() < b["id"].template get<uint32_t>();
      });
      sorted_by = SORTED_BY_ID;
      
      std::lock_guard<std::mutex> lock(this->lv_lock);
      populate_spools(sorted_spools);

    }
  });
}

void SpoolmanPanel::foreground() {
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

void SpoolmanPanel::populate_spools(std::vector<json> &sorted_spools) {
  if (!spools.empty()) {
    lv_table_set_cell_value(spool_table, 0, 0, "ID");
    lv_table_set_cell_value(spool_table, 0, 1, "Name");
    lv_table_set_cell_value(spool_table, 0, 2, "MAT");
    lv_table_set_cell_value(spool_table, 0, 4, "Remain\nWeight");
    lv_table_set_cell_value(spool_table, 0, 5, "Remain\nLength");

    bool skip_archive = !lv_obj_has_state(show_archived, LV_STATE_CHECKED);

    size_t row_idx = 1;
    for (auto &el : sorted_spools) {
      spdlog::trace("spool {}", el.dump());
      bool is_archived = el["archived"].template get<bool>();
      if (skip_archive && is_archived) {
	continue;
      }
      
      auto id = el["/id"_json_pointer].template get<uint32_t>();
      bool is_active = id == active_id;

      auto vendor_json = el["/filament/vendor/name"_json_pointer];
      auto vendor = !vendor_json.is_null() ? vendor_json.template get<std::string>() : "";

      auto filament_name_json =  el["/filament/name"_json_pointer];
      auto filament_name = !filament_name_json.is_null() ? filament_name_json.template get<std::string>() : "";

      auto material_json = el["/filament/material"_json_pointer];
      auto material = !material_json.is_null() ? material_json.template get<std::string>(): "";

      auto remaining_weight_json = el["/remaining_weight"_json_pointer];
      auto remaining_weight = !remaining_weight_json.is_null() ? remaining_weight_json.template get<double>() : 0.0;

      auto remaining_len_json = el["/remaining_length"_json_pointer];
      auto remaining_len = !remaining_len_json.is_null()
      ? remaining_len_json.template get<double>() / 100 // mm to m;
      : 0.0;

      lv_table_set_cell_value(spool_table, row_idx, 0, std::to_string(id).c_str());
      lv_table_set_cell_value(spool_table, row_idx, 1,
			      fmt::format("{} - {}", vendor, filament_name).c_str());
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

      row_idx++;
    }
    lv_table_set_row_cnt(spool_table, row_idx);
  }
}

void SpoolmanPanel::handle_active_id_update(json &j) {
  spdlog::trace("active spool id update {}", j.dump());
  auto &v = j["/params/0/spool_id"_json_pointer];
  if (!v.is_null()) {
    active_id = v.template get<int>();

    std::vector<json> sorted_spools;
    KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [](json &a, json &b) {
      return a["id"].template get<uint32_t>() < b["id"].template get<uint32_t>();
    });
    sorted_by = SORTED_BY_ID;

    std::lock_guard<std::mutex> lock(lv_lock);
    populate_spools(sorted_spools);
  }
}

void SpoolmanPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    spdlog::trace("spoolman back button pressed");
    lv_obj_move_background(cont);
  } else if (btn == reload_btn.get_container()) {
    spdlog::trace("spoolman reload button pressed");
    init();
  }
}

void SpoolmanPanel::handle_spoolman_action(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *clicked = lv_event_get_target(e);
    if (clicked == show_archived) {
      std::vector<json> sorted_spools;
      KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [](json &a, json &b) {
	return a["id"].template get<uint32_t>() < b["id"].template get<uint32_t>();
      });
      sorted_by = SORTED_BY_ID;

      populate_spools(sorted_spools);
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
	std::vector<json> sorted_spools;
	KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [reversed](json &a, json &b) {
	  auto x = a["/id"_json_pointer].template get<uint32_t>();
	  auto y = b["/id"_json_pointer].template get<uint32_t>();

	  return reversed ? x > y : y > x;
	});

	sorted_by = (sorted_by ^ SORTED_BY_ID) & SORTED_BY_ID;

	populate_spools(sorted_spools);
	
      } else if (col == 1) {
	// sort by name
	bool reversed = sorted_by & SORTED_BY_NAME;
	std::vector<json> sorted_spools;
	KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [reversed](json &a, json &b) {
	  auto vendor = a["/filament/vendor/name"_json_pointer].template get<std::string>();
	  auto filament_name = a["/filament/name"_json_pointer].template get<std::string>();
	  auto x = fmt::format("{} - {}", vendor, filament_name);

	  vendor = b["/filament/vendor/name"_json_pointer].template get<std::string>();
	  filament_name = b["/filament/name"_json_pointer].template get<std::string>();
	  auto y = fmt::format("{} - {}", vendor, filament_name);

	  return reversed ? x > y : y > x;
	});
	sorted_by = (sorted_by ^ SORTED_BY_NAME) & SORTED_BY_NAME;

	populate_spools(sorted_spools);
	
      } else if (col == 2) {
	// sort by material
	bool reversed = sorted_by & SORTED_BY_MAT;
	std::vector<json> sorted_spools;
	KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [reversed](json &a, json &b) {
	  auto x = a["/filament/material"_json_pointer].template get<std::string>();
	  auto y = b["/filament/material"_json_pointer].template get<std::string>();

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
	std::vector<json> sorted_spools;
	KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [reversed](json &a, json &b) {
	  auto x = a["/remaining_weight"_json_pointer].template get<double>();
	  auto y = b["/remaining_weight"_json_pointer].template get<double>();	  

	  return reversed ? x > y : y > x;
	});
	sorted_by = (sorted_by ^ SORTED_BY_WT) & SORTED_BY_WT;

	populate_spools(sorted_spools);
	
      } else if (col == 5) {
	// sort by length
	bool reversed = sorted_by & SORTED_BY_LEN;
	std::vector<json> sorted_spools;
	KUtils::sort_map_values<uint32_t, json>(spools, sorted_spools, [reversed](json &a, json &b) {
	  auto x = a["/remaining_length"_json_pointer].template get<double>();
	  auto y = b["/remaining_length"_json_pointer].template get<double>();
	  return reversed ? x > y : y > x;
	});
	sorted_by = (sorted_by ^ SORTED_BY_LEN) & SORTED_BY_LEN;

	populate_spools(sorted_spools);

      }
    }
    
    const char *selected = lv_table_get_cell_value(spool_table, row, col);
    const char *spool_id = lv_table_get_cell_value(spool_table, row, 0);
    spdlog::trace("selected {}, {}, value {}", row, col, spool_id);
    if (row != 0) {
      if (col == 6 && selected != NULL && strlen(selected) != 0
	  && std::memcmp(LV_SYMBOL_PLAY, selected, 3) == 0) {
	// set active spool
	int id = std::stoi(spool_id);
	spdlog::trace("set active spool id {}", id);
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
	dsc->rect_dsc->bg_color = lv_color_mix(lv_palette_main(LV_PALETTE_BLUE),
					       dsc->rect_dsc->bg_color, LV_OPA_20);
	dsc->rect_dsc->bg_opa = LV_OPA_COVER;
      }

      if (row != 0 && col == 3) {
	const char *spool_id = lv_table_get_cell_value(spool_table, row, 0);
	uint32_t id = std::stoi(spool_id);
	const auto &spool = spools.find(id);
	if (spool != spools.end()) {
	  auto &c = spool->second["/filament/color_hex"_json_pointer];
	  if (!c.is_null()) {
	    dsc->rect_dsc->bg_color = lv_color_hex(std::stoul(c.template get<std::string>(),
							      nullptr, 16));
	  }
	}
      }

      if((row != 0 && row % 2) == 0) {
	dsc->rect_dsc->bg_color = lv_color_mix(lv_palette_main(LV_PALETTE_GREY),
					       dsc->rect_dsc->bg_color, LV_OPA_10);
	dsc->rect_dsc->bg_opa = LV_OPA_COVER;
      }
    }
  }
}
