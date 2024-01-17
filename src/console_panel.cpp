#include "console_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>

LV_FONT_DECLARE(dejavusans_mono_14);

ConsolePanel::ConsolePanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent)
  : ws(websocket_client)
  , lv_lock(lock)
  , console_cont(lv_obj_create(parent))
  , top_cont(lv_obj_create(console_cont))
  , output(lv_textarea_create(top_cont))
  , macro_list(lv_table_create(top_cont))
  , input_cont(lv_obj_create(console_cont))
  , input(lv_textarea_create(input_cont))
  , kb(lv_keyboard_create(console_cont))
{
  lv_obj_align(console_cont, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_size(console_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(console_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(console_cont, 0, 0);
  lv_obj_set_style_text_font(console_cont, &dejavusans_mono_14, LV_STATE_DEFAULT);

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));

  lv_obj_set_style_border_width(output, 0, 0);
  lv_obj_set_size(output, LV_PCT(60), LV_PCT(100));
  lv_obj_set_style_border_width(output, 0, LV_STATE_FOCUSED | LV_PART_CURSOR);

  lv_obj_set_flex_grow(input, 1);
  lv_obj_set_width(input, LV_PCT(100));
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_cursor_click_pos(output, false);

  lv_obj_set_flex_flow(input_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(input_cont, 0, 0);
  lv_obj_set_size(input_cont, LV_PCT(100), LV_SIZE_CONTENT);

  lv_obj_t *send_btn = lv_btn_create(input_cont);
  lv_obj_set_style_text_font(send_btn, &lv_font_montserrat_16, LV_STATE_DEFAULT);
  lv_obj_set_width(send_btn, 100);
  lv_obj_t *send_btn_label = lv_label_create(send_btn);
  lv_label_set_text(send_btn_label, LV_SYMBOL_NEW_LINE);
  lv_obj_center(send_btn_label);
  lv_obj_add_event_cb(send_btn , &ConsolePanel::_handle_send_macro, LV_EVENT_CLICKED, this);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(input, &ConsolePanel::_handle_kb_input, LV_EVENT_ALL, this);

  lv_obj_set_size(macro_list, LV_PCT(40), LV_PCT(100));
  lv_obj_align(macro_list, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_table_set_col_width(macro_list, 0, LV_PCT(100));

  lv_obj_add_event_cb(macro_list, &ConsolePanel::_handle_select_macro, LV_EVENT_ALL, this);
  lv_obj_set_scroll_dir(macro_list, LV_DIR_TOP | LV_DIR_BOTTOM);

  lv_obj_t *label = lv_label_create(input);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_STATE_DEFAULT);
  lv_label_set_text(label, "      " LV_SYMBOL_CLOSE "      ");
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(label, &ConsolePanel::_handle_clear_input, LV_EVENT_CLICKED, this);

  // ws.register_gcode_resp([this](json& d) { this->handle_macro_response(d); });
  ws.register_method_callback("notify_gcode_response",
			      "ConsolePanel",
			      [this](json& d) { this->handle_macro_response(d); });
}

ConsolePanel::~ConsolePanel() {
  if (console_cont != NULL) {
    lv_obj_del(console_cont);
    console_cont = NULL;
  }
}

lv_obj_t *ConsolePanel::get_container() {
  return console_cont;
}

void ConsolePanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, input);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  
  if(code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  if (code == LV_EVENT_VALUE_CHANGED) {
    // filter macros with input
    lv_obj_scroll_to_y(macro_list, 0, LV_ANIM_OFF);
    std::string cmd = std::string(lv_textarea_get_text(input));
    if (cmd.find_first_of(' ') == std::string::npos) {
      std::string upper_cmd;
      std::transform(cmd.begin(), cmd.end(), std::back_inserter(upper_cmd),
		     [](unsigned char c){ return std::toupper(c); });

      if (!all_macros.empty() || !history.empty()) {
	uint16_t index = 0;
	for (const auto &m : history) {
	  if (m.rfind(upper_cmd, 0) == 0 || m.rfind(cmd, 0) == 0) {
	    lv_table_set_cell_value(macro_list, index++, 0,  m.c_str());
	  }
	}
	
	for (const auto &m : all_macros) {
	  if (m.rfind(upper_cmd, 0) == 0 || m.rfind(cmd, 0) == 0) {
	    lv_table_set_cell_value(macro_list, index++, 0,  m.c_str());
	  }
	}

	lv_table_set_row_cnt(macro_list, index);
      }
    }
  }
  
  if (code == LV_EVENT_READY) {
    spdlog::debug("keyboard ready");
    const char *cmd = lv_textarea_get_text(input);
    if (cmd == NULL || cmd[0] == 0) {
      return;
    }

    lv_textarea_add_text(output,"> ");
    lv_textarea_add_text(output, cmd);
    lv_textarea_add_text(output,"\n");
    ws.gcode_script(cmd);

    if (!history.empty()) {
      const auto &front = history.front();
      
      if (front != std::string(cmd)) {
	if (history.size() >= 20) {
	  history.pop_back();
	}
	history.push_front(cmd);

	json h = {
	  {"namespace", "fluidd"}, // leverage history from fluidd
	  {"key", "console.commandHistory"},
	  {"value", history}
	};

	ws.send_jsonrpc("server.database.post_item", h);
      }
    }
		    
    lv_textarea_set_text(input, "");

    uint32_t index = 0;
    for (const auto &m : history) {
      lv_table_set_cell_value(macro_list, index++, 0,  m.c_str());
    }

    for (const auto &m : all_macros) {
      lv_table_set_cell_value(macro_list, index++, 0,  m.c_str());
    }
    
  }
}

void ConsolePanel::handle_select_macro(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(macro_list, &row, &col);
    const char * macro = lv_table_get_cell_value(macro_list, row, col);
    lv_textarea_set_text(input, macro);
  }
  
}

void ConsolePanel::handle_macros(json &j) {
  uint32_t index = 0;

  // TODO: this is a race condition
  auto &db_history = State::get_instance()->get_data("/console/commandHistory"_json_pointer);

  if (!db_history.is_null()) {
    history = db_history.template get<std::list<std::string>>();
  }

  if (j.contains("result")) {
    const auto &m = j["result"];
    for (const auto &el : m.items()) {
      all_macros.push_back(el.key());
    }
  }

  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &m : history) {
    lv_table_set_cell_value(macro_list, index++, 0,  m.c_str());
  }
  
  for (const auto &m : all_macros) {
    lv_table_set_cell_value(macro_list, index++, 0,  m.c_str());
  }
}

void ConsolePanel::handle_macro_response(json &j) {
  if (j.contains("params")) {
    std::lock_guard<std::mutex> lock(lv_lock);
    for (auto &l : j["params"]) {
      lv_textarea_add_text(output, l.template get<std::string>().c_str());
      lv_textarea_add_text(output, "\n");
    }
  }
}

void ConsolePanel::handle_send_macro(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    lv_event_send(input, LV_EVENT_READY, this);
  }
}

void ConsolePanel::handle_clear_input(lv_event_t *e) {
  lv_textarea_set_text(input, "");
}
