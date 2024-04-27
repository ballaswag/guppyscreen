#include "print_panel.h"
#include "file_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <map>
#include <sstream>

LV_IMG_DECLARE(info_img);
LV_IMG_DECLARE(print);
LV_IMG_DECLARE(back);

#define SORTED_BY_NAME 1 << 0
#define SORTED_BY_MODIFIED  1 << 1

PrintPanel::PrintPanel(KWebSocketClient &websocket, std::mutex &lock, PrintStatusPanel &ps)
  : NotifyConsumer(lock)
  , ws(websocket)
  , files_cont(lv_obj_create(lv_scr_act()))
  , prompt_cont(lv_obj_create(lv_scr_act()))
  , msgbox(lv_obj_create(prompt_cont))
  , job_btn(lv_btn_create(msgbox))
  , cancel_btn(lv_btn_create(msgbox))
  , queue_btn(lv_btn_create(msgbox))
  , left_cont(lv_obj_create(files_cont))
  , file_table_btns(lv_obj_create(left_cont))
  , refresh_btn(lv_btn_create(file_table_btns))
  , modified_sort_btn(lv_btn_create(file_table_btns))
  , az_sort_btn(lv_btn_create(file_table_btns))
  , file_table(lv_table_create(left_cont))
  , file_view(lv_obj_create(files_cont))
  , status_btn(file_view, &info_img, "Status", &PrintPanel::_handle_status_btn, this)
  , print_btn(file_view, &print, "Print", &PrintPanel::_handle_print_callback, this)
  , back_btn(file_view, &back, "Back", &PrintPanel::_handle_back_btn, this)
  , root("", "", 0)
  , cur_dir(&root)
  , cur_file(NULL)
  , file_panel(file_view)
  , print_status(ps)
  , sorted_by(SORTED_BY_MODIFIED)
{
  spdlog::trace("building print panel");
  lv_obj_move_background(files_cont);

  lv_obj_set_size(files_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(files_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(files_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(files_cont, 0, 0);

  // left side cont
  lv_obj_set_size(left_cont, LV_PCT(50), LV_PCT(100));
  lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(left_cont, 0, 0);

  // file view buttons
  lv_obj_t * label = NULL;
  
  label = lv_label_create(refresh_btn);
  lv_label_set_text(label, LV_SYMBOL_REFRESH " Reload");
  lv_obj_center(label);

  label = lv_label_create(modified_sort_btn);
  lv_label_set_text(label, LV_SYMBOL_LIST " Modified");
  lv_obj_center(label);

  label = lv_label_create(az_sort_btn);
  lv_label_set_text(label, LV_SYMBOL_LIST " A-Z");
  lv_obj_center(label);

  lv_obj_add_event_cb(refresh_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(modified_sort_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(az_sort_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  
  lv_obj_set_size(file_table_btns, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(file_table_btns, 2, 0);

  lv_obj_clear_flag(file_table_btns, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(file_table_btns, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(file_table_btns, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);

  lv_obj_set_size(file_table, LV_PCT(100), LV_PCT(100));
  lv_table_set_col_width(file_table, 0, LV_PCT(100));
  lv_table_set_col_cnt(file_table, 1);
  lv_obj_add_event_cb(file_table, &PrintPanel::_handle_callback, LV_EVENT_ALL, this);
  lv_obj_set_scroll_dir(file_table, LV_DIR_TOP | LV_DIR_BOTTOM);

  lv_obj_set_size(file_view, LV_PCT(50), LV_PCT(100));
  lv_obj_clear_flag(file_view, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(file_view, grid_main_col_dsc, grid_main_row_dsc);
  lv_obj_set_grid_cell(file_panel.get_container(), LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 0, 1);

  lv_obj_set_grid_cell(status_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_END, 1, 1);  
  lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_END, 1, 1);
  lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 1, 1);

  lv_obj_move_foreground(back_btn.get_container());
  lv_obj_move_foreground(print_btn.get_container());
  lv_obj_move_foreground(status_btn.get_container());      

  // prompt
  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);  
  lv_obj_set_size(prompt_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(prompt_cont, LV_OPA_70, 0);

  lv_obj_set_size(msgbox, LV_PCT(60), LV_PCT(30));
  lv_obj_set_style_border_width(msgbox, 2, 0);
  lv_obj_set_style_bg_color(msgbox, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
  
  lv_obj_align(msgbox, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(job_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(job_btn, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_add_event_cb(cancel_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_add_event_cb(queue_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(queue_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  
  label = lv_label_create(job_btn);
  lv_label_set_text(label, "View Job");
  lv_obj_center(label);

  label = lv_label_create(cancel_btn);
  lv_label_set_text(label, "Cancel");
  lv_obj_center(label);

  label = lv_label_create(queue_btn);
  lv_label_set_text(label, "Queue Job");
  lv_obj_center(label);

  label = lv_label_create(msgbox);
  lv_label_set_text(label, "Printing in progress...");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

  ws.register_notify_update(this);
}

PrintPanel::~PrintPanel() {
  if (files_cont != NULL) {
    lv_obj_del(files_cont);
    files_cont = NULL;
  }

  if (prompt_cont != NULL) {
    lv_obj_del(prompt_cont);
    prompt_cont = NULL;
  }
}

void PrintPanel::populate_files(json &j) {
  sorted_by = SORTED_BY_MODIFIED;
  show_dir(cur_dir, SORTED_BY_MODIFIED);
}

void PrintPanel::consume(json &j) {  
  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (pstat_state.is_null()) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(lv_lock);
  if(pstat_state.template get<std::string>() != "printing"
     && pstat_state.template get<std::string>() != "paused") {
    status_btn.disable();
  } else {
    status_btn.enable();
  }
}

void PrintPanel::subscribe() {
  ws.send_jsonrpc("server.files.list", R"({"root":"gcodes"})"_json, [this](json &d) {
    std::lock_guard<std::mutex> lock(lv_lock);
    std::string cur_path = cur_dir->full_path;
    root.clear();
    cur_file = NULL;
    cur_dir = NULL;

    if (d.contains("result")) {
      for (auto f : d["result"]) {
        root.add_path(KUtils::split(f["path"], '/'), f["path"], f["modified"].template get<uint32_t>());
      }
    }
    Tree *dir = root.find_path(KUtils::split(cur_path, '/'));
    // need to simply this using the directory endpoint
    cur_dir = dir;
    this->populate_files(d);
  });
}

void PrintPanel::foreground() {
  json &pstat_state = State::get_instance()
    ->get_data("/printer_state/print_stats/state"_json_pointer);
  spdlog::debug("print panel print stats {}",
		pstat_state.is_null() ? "nil" : pstat_state.template get<std::string>());
    
  if (!pstat_state.is_null()
      && pstat_state.template get<std::string>() != "printing"
      && pstat_state.template get<std::string>() != "paused") {
    status_btn.disable();
  } else {
    status_btn.enable();
  }
  
  lv_obj_move_foreground(files_cont);
}

void PrintPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_VALUE_CHANGED) {
    const char * str_fn = NULL;
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(file_table, &row, &col);
    uint16_t row_count = lv_table_get_row_cnt(file_table);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE || row >= row_count) {
      return;
    }

    str_fn = lv_table_get_cell_value(file_table, row, col);
    
    const char *filename = str_fn+5; // +5 skips the LV_SYMBOL and spaces
    if (std::memcmp(LV_SYMBOL_DIRECTORY, str_fn, 3) == 0) {
      if ((strcmp(filename, "..") == 0)) {
	if (cur_dir->parent != cur_dir) {
	  cur_dir = cur_dir->parent;
	  show_dir(cur_dir, sorted_by);
	}
      } else {
	Tree *dir = cur_dir->get_child(filename);
	if (dir != NULL) {
	  cur_dir = dir;
	  show_dir(cur_dir, sorted_by);
	}
      }
    }
    else {
      if (cur_file != cur_dir->get_child(filename)) {
	cur_file = cur_dir->get_child(filename);
	show_file_detail(cur_file);
      }
    }
  }
}

void PrintPanel::show_dir(Tree *dir, uint32_t sort_type) {
  uint32_t index = 0;
  lv_table_set_cell_value_fmt(file_table, index++, 0, LV_SYMBOL_DIRECTORY "  %s", "..");

  bool reversed = sorted_by & sort_type;
  std::vector<Tree> sorted_files;
  if (sort_type == SORTED_BY_MODIFIED) {
    KUtils::sort_map_values<std::string, Tree>(dir->children, sorted_files, [reversed](Tree &x, Tree &y) {
	if (x.is_leaf() && !y.is_leaf()) {
	  return false;
	} else if (!x.is_leaf() && y.is_leaf()) {
	  return true;
	}

	return reversed ? x.date_modified > y.date_modified : y.date_modified > x.date_modified;
      });
  } else {
    KUtils::sort_map_values<std::string, Tree>(dir->children, sorted_files, [reversed](Tree &x, Tree &y) {
	if (x.is_leaf() && !y.is_leaf()) {
	  return false;
	} else if (!x.is_leaf() && y.is_leaf()) {
	  return true;
	}

	return reversed ? x.name > y.name : y.name > x.name;
      });
  }
      
  sorted_by = (sorted_by ^ sort_type) & sort_type;
  for (const auto &c : sorted_files) {
    if (c.is_leaf()) {
      lv_table_set_cell_value_fmt(file_table, index, 0, LV_SYMBOL_FILE "  %s", c.name.c_str());
    } else {
      lv_table_set_cell_value_fmt(file_table, index, 0, LV_SYMBOL_DIRECTORY "  %s", c.name.c_str());
    }
    index++;
  }

  lv_table_set_row_cnt(file_table, index);
  lv_obj_scroll_to_y(file_table, 0, LV_ANIM_OFF);

  // XXX: maybe use the directory instead of file endpoint in moonraker
  for (auto &c : sorted_files) {
    if (c.is_leaf()) {
      const auto &selected = dir->children.find(c.name);
      if (selected != dir->children.cend()) {
	cur_file = &selected->second;
	show_file_detail(cur_file);
      }
      break;
    }
  }

}

void PrintPanel::show_file_detail(Tree *f) {
  if (f->is_leaf()) {
    if (f->contains_metadata()) {
      file_panel.refresh_view(f->metadata, f->full_path);
    } else {
      spdlog::trace("getting metadata for {}", f->name);
      ws.send_jsonrpc("server.files.metadata",
		      json::parse(R"({"filename":")" + f->full_path + R"("})"),
		      [f, this](json &d) { this->handle_metadata(f, d); });
    }
  }
}

void PrintPanel::handle_metadata(Tree *f, json &j) {
  spdlog::trace("handling metadata callback");  
  if (f->is_leaf()) {
    if (j.contains("result")) {
      std::lock_guard<std::mutex> lock(lv_lock);
      f->set_metadata(j);
      file_panel.refresh_view(f->metadata, f->full_path);
    }
  }
}

void PrintPanel::handle_back_btn(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    lv_obj_move_background(files_cont);
    print_status.background();    
  }
}

void PrintPanel::handle_print_callback(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED && cur_file != NULL) {

    json &pstat_state = State::get_instance()
      ->get_data("/printer_state/print_stats/state"_json_pointer);
    spdlog::debug("print panel print stats {}",
		  pstat_state.is_null() ? "nil" : pstat_state.template get<std::string>());
    
    if (!pstat_state.is_null()
	&& pstat_state.template get<std::string>() != "printing"
	&& pstat_state.template get<std::string>() != "paused") {
      spdlog::debug("printer ready to print. print file {}", cur_file->full_path);
	
      // ws.send_jsonrpc("printer.gcode.script",
      // 		    json::parse(R"({"script":"PRINT_PREPARE_CLEAR"})"));

      json fname_input = {{"filename", cur_file->full_path }};
      ws.send_jsonrpc("printer.print.start", fname_input);
      print_status.foreground();

    } else {
      lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(prompt_cont);
    }
  }
}

void PrintPanel::handle_status_btn(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED && cur_file != NULL) {
    spdlog::trace("status button clicked");
    print_status.foreground();
  }
}

void PrintPanel::handle_btns(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(event);
    if (cur_file != NULL) {
      spdlog::trace("status prompt clicked");
      if (btn == queue_btn) {
	spdlog::trace("status prompt queue clicked");
      }

      if (btn == job_btn) {
	spdlog::trace("status prompt job clicked");
      }

      if (btn == cancel_btn) {
	spdlog::trace("status prompt cancel clicked");
	lv_obj_move_background(prompt_cont);
	lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (btn == refresh_btn) {
      subscribe();
      
    } else if (btn == modified_sort_btn) {
      show_dir(cur_dir, SORTED_BY_MODIFIED);

    } else if (btn == az_sort_btn) {
      show_dir(cur_dir, SORTED_BY_NAME);
    }
  }
}
