#include "extruder_panel.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "icons.h"
#include "theme.h"

using namespace Theme;

#include <algorithm>
#include <cctype>
#include <limits>

namespace {

std::string trim_copy(std::string s) {
  auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_ws(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_ws(c); }).base(), s.end());
  return s;
}

std::vector<std::string> parse_selector_options(const std::string &csv) {
  std::vector<std::string> options;
  size_t start = 0;
  while (start <= csv.size()) {
    size_t end = csv.find(',', start);
    std::string token = trim_copy(csv.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (!token.empty()) {
      options.push_back(token);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return options;
}

std::vector<std::string> load_selector_options(const std::string &config_key, size_t max_options) {
  Config *conf = Config::get_instance();
  auto options = parse_selector_options(conf->get<std::string>(config_key));
  if (options.size() > max_options) {
    LOG_INFO("{} has {} values; truncating to {}", config_key, options.size(), max_options);
    options.resize(max_options);
  }
  return options;
}

std::vector<const char*> build_selector_map(std::vector<std::string> &options) {
  std::vector<const char*> map;
  map.reserve(options.size() + 1);
  for (auto &option : options) {
    map.push_back(option.c_str());
  }
  map.push_back("");
  return map;
}

std::string read_config_value_as_string(const std::string &config_key) {
  Config *conf = Config::get_instance();
  std::string configured_value = trim_copy(conf->get<std::string>(config_key));
  if (!configured_value.empty()) {
    return configured_value;
  }

  int configured_int = conf->get<int>(config_key, std::numeric_limits<int>::min());
  if (configured_int != std::numeric_limits<int>::min()) {
    return std::to_string(configured_int);
  }

  return "";
}

uint32_t resolve_default_idx(const std::string &config_key,
                             const std::vector<std::string> &options) {
  std::string configured_value = read_config_value_as_string(config_key);
  if (!configured_value.empty()) {
    auto it = std::find(options.begin(), options.end(), configured_value);
    if (it != options.end()) {
      return static_cast<uint32_t>(std::distance(options.begin(), it));
    }
  }

  if (options.empty()) {
    return std::numeric_limits<uint32_t>::max();
  }

  return 0;
}

} // namespace

ExtruderPanel::ExtruderPanel(KWebSocketClient &websocket_client,
			     std::mutex &lock,
			     Numpad &numpad)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(create_screen(NULL))
  , temp_options(load_selector_options("/ui/extruder_temp_presets", 8))
  , temp_option_map(build_selector_map(temp_options))
  , temp_default_idx(resolve_default_idx("/ui/extruder_temp_default", temp_options))
  , length_options(load_selector_options("/ui/extruder_length_presets", 8))
  , length_option_map(build_selector_map(length_options))
  , length_default_idx(resolve_default_idx("/ui/extruder_length_default", length_options))
  , speed_options(load_selector_options("/ui/extruder_speed_presets", 8))
  , speed_option_map(build_selector_map(speed_options))
  , speed_default_idx(resolve_default_idx("/ui/extruder_speed_default", speed_options))
  , tabview(lv_tabview_create(panel_cont, LV_DIR_TOP, scale_r(36)))
  , load_tab(lv_tabview_add_tab(tabview, "Load / Unload"))
  , extrude_tab(lv_tabview_add_tab(tabview, "Extrude / Retract"))
#ifndef COSMOS
  , load_header(create_row(load_tab))
  , extrude_header(create_row(extrude_tab))
  , load_extruder_temp(ws, load_header, Icons::EXTRUDER, "Extruder", theme_secondary(), false, true, numpad,
                       "extruder", NULL, NULL)
  , extrude_extruder_temp(ws, extrude_header, Icons::EXTRUDER, "Extruder", theme_secondary(), false, true, numpad,
                          "extruder", NULL, NULL)
  , load_extruder_dd(lv_dropdown_create(load_header))
  , extrude_extruder_dd(lv_dropdown_create(extrude_header))
#else
  , load_extruder_temp(ws, load_tab, Icons::EXTRUDER, "Extruder", theme_secondary(), false, true, numpad,
                       "extruder", NULL, NULL)
  , extrude_extruder_temp(ws, extrude_tab, Icons::EXTRUDER, "Extruder", theme_secondary(), false, true, numpad,
                          "extruder", NULL, NULL)
#endif
  , load_cont(create_row(load_tab))
  , extrude_cont(create_row(extrude_tab))
  , extrude_col(create_row(extrude_cont))
  , load_temp_selector(load_cont, "Extruder Temperature (C)",
		       temp_option_map, temp_default_idx, &ExtruderPanel::_handle_callback, this)
  , load_btn(load_cont, Icons::LOAD_FILAMENT_IMG, "Load", &ExtruderPanel::_handle_callback, this)
  , unload_btn(load_cont, Icons::UNLOAD_FILAMENT_IMG, "Unload", &ExtruderPanel::_handle_callback, this)
  , speed_selector(extrude_cont, "Extrude Speed (mm/s)",
		   speed_option_map, speed_default_idx, &ExtruderPanel::_handle_callback, this)
  , length_selector(extrude_cont, "Extrude Length (mm)",
		    length_option_map, length_default_idx, &ExtruderPanel::_handle_callback, this)
  , extrude_temp_selector(extrude_cont, "Extruder Temperature (C)",
			  temp_option_map, temp_default_idx, &ExtruderPanel::_handle_callback, this)
  , extrude_btn(extrude_col, Icons::EXTRUDE_IMG, "Extrude", &ExtruderPanel::_handle_callback, this)
  , retract_btn(extrude_col, Icons::RETRACT_IMG, "Retract", &ExtruderPanel::_handle_callback, this)
  , load_right_col(create_row(load_tab))
  , extrude_right_col(create_row(extrude_tab))
  , load_cooldown_btn(load_right_col, Icons::COOLDOWN_IMG, "Cooldown", &ExtruderPanel::_handle_callback, this)
  , extrude_cooldown_btn(extrude_right_col, Icons::COOLDOWN_IMG, "Cooldown", &ExtruderPanel::_handle_callback, this)
{
  lv_obj_move_background(panel_cont);

  // The tab bar spans the panel; each page owns all of its controls.
  lv_obj_set_style_pad_all(panel_cont, 0, 0);
  lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
  static lv_coord_t grid_panel_col_dsc[] = {LV_GRID_FR(1), (lv_coord_t)scale_w(90), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_panel_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  for (lv_obj_t *tab : {load_tab, extrude_tab}) {
    lv_obj_add_style(tab, &styles().screen, 0);
    lv_obj_set_grid_dsc_array(tab, grid_panel_col_dsc, grid_panel_row_dsc);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  }
#ifndef COSMOS
  for (lv_obj_t *header : {load_header, extrude_header}) {
    lv_obj_set_grid_cell(header, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, gap(), 0);
  }
  for (SensorContainer *sensor : {&load_extruder_temp, &extrude_extruder_temp}) {
    lv_obj_set_width(sensor->get_sensor(), 0);
    lv_obj_set_flex_grow(sensor->get_sensor(), 1);
  }
  for (lv_obj_t *dd : {load_extruder_dd, extrude_extruder_dd}) {
    lv_obj_set_width(dd, scale_w(150));
    lv_obj_add_event_cb(dd, &ExtruderPanel::_handle_extruder_select, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_flag(dd, LV_OBJ_FLAG_HIDDEN);
  }
#else
  for (SensorContainer *sensor : {&load_extruder_temp, &extrude_extruder_temp}) {
    lv_obj_set_grid_cell(sensor->get_sensor(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  }
#endif
  for (lv_obj_t *content : {load_cont, extrude_cont}) {
    lv_obj_set_grid_cell(content, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  }
  style_embedded_tabview(tabview);

  ButtonContainer *all_tiles[] = {&load_btn, &unload_btn, &extrude_btn, &retract_btn,
                                  &load_cooldown_btn, &extrude_cooldown_btn};
  for (ButtonContainer *b : all_tiles) {
    b->use_plain();
  }

  // Cooldown stays at the bottom of each page's right-hand column.
  for (lv_obj_t *right_col : {load_right_col, extrude_right_col}) {
    lv_obj_set_grid_cell(right_col, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(right_col, 0, 0);
  }
  for (ButtonContainer *b : {&load_cooldown_btn, &extrude_cooldown_btn}) {
    lv_obj_set_width(b->get_container(), LV_PCT(100));
    lv_obj_set_height(b->get_container(), action_tile_h());
  }

  // Load / Unload: the pair side by side, the temperature presets beneath.
  static lv_coord_t grid_load_row_dsc[] = {LV_GRID_FR(6), LV_GRID_FR(6), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_load_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(load_cont, grid_load_col_dsc, grid_load_row_dsc);
  lv_obj_set_grid_cell(load_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(unload_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(load_temp_selector.get_container(), LV_GRID_ALIGN_STRETCH, 0, 2,
                       LV_GRID_ALIGN_STRETCH, 1, 1);

  // Extrude / Retract: the pair stacked on the left, three selectors beside them.
  static lv_coord_t grid_extrude_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_extrude_col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(7), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(extrude_cont, grid_extrude_col_dsc, grid_extrude_row_dsc);
  lv_obj_set_grid_cell(extrude_col, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 3);
  lv_obj_set_flex_flow(extrude_col, LV_FLEX_FLOW_COLUMN);
  for (ButtonContainer *b : {&extrude_btn, &retract_btn}) {
    lv_obj_set_width(b->get_container(), LV_PCT(100));
    lv_obj_set_height(b->get_container(), 0);
    lv_obj_set_flex_grow(b->get_container(), 1);
  }
  Selector *mid[] = {&speed_selector, &length_selector, &extrude_temp_selector};
  for (int r = 0; r < 3; r++) {
    lv_obj_set_grid_cell(mid[r]->get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, r, 1);
    // Three selectors share the column, so a cell is shorter than a full key
    // row plus the panel's padding: let the keys shrink into what it leaves.
    lv_obj_set_style_min_height(mid[r]->get_selector(), 0, 0);
    lv_obj_set_style_pad_bottom(mid[r]->get_container(), 0, 0);
  }

  ws.register_notify_update(this);
}

ExtruderPanel::~ExtruderPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
#ifndef COSMOS
  refresh_extruders();
#endif
  lv_tabview_set_act(tabview, lv_obj_get_index(load_tab), LV_ANIM_OFF);
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::background() {
  lv_obj_move_background(panel_cont);
}

#ifndef COSMOS
// "extruder" is T0, "extruderN" is TN
static int extruder_tool(const std::string &name) {
  const std::string digits = name.substr(std::string("extruder").size());
  if (digits.empty()) {
    return 0;
  }
  if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return -1;
  }
  return std::stoi(digits);
}

void ExtruderPanel::refresh_extruders() {
  std::vector<std::string> found = State::get_instance()->get_extruders();
  std::sort(found.begin(), found.end(), [](const std::string &a, const std::string &b) {
    return extruder_tool(a) < extruder_tool(b);
  });
  if (found != extruders) {
    extruders = found;
    std::string options;
    for (const std::string &name : extruders) {
      if (!options.empty()) {
        options += '\n';
      }
      options += fmt::format("T{}", extruder_tool(name));
    }
    for (lv_obj_t *dd : {load_extruder_dd, extrude_extruder_dd}) {
      lv_dropdown_set_options(dd, options.c_str());
    }
    active_extruder.clear();  // relabel the readouts for the new count
  }
  for (lv_obj_t *dd : {load_extruder_dd, extrude_extruder_dd}) {
    if (extruders.size() > 1) {
      lv_obj_clear_flag(dd, LV_OBJ_FLAG_HIDDEN);
    }
    else {
      lv_obj_add_flag(dd, LV_OBJ_FLAG_HIDDEN);
    }
  }

  json &active = State::get_instance()->get_data("/printer_state/toolhead/extruder"_json_pointer);
  set_active_extruder(active.is_string() ? active.template get<std::string>() : "extruder");
}

// toolhead.extruder decides, so the pickers and readouts follow a tool change
// made anywhere, not only from here
void ExtruderPanel::set_active_extruder(const std::string &name) {
  auto it = std::find(extruders.begin(), extruders.end(), name);
  if (it != extruders.end()) {
    for (lv_obj_t *dd : {load_extruder_dd, extrude_extruder_dd}) {
      lv_dropdown_set_selected(dd, std::distance(extruders.begin(), it));
    }
  }
  if (name.empty() || name == active_extruder) {
    return;
  }
  active_extruder = name;
  const std::string label = extruders.size() > 1
      ? fmt::format("Extruder T{}", extruder_tool(name)) : "Extruder";
  json &state = State::get_instance()->get_data(json::json_pointer("/printer_state/" + name));
  for (SensorContainer *sensor : {&load_extruder_temp, &extrude_extruder_temp}) {
    sensor->set_id(name, label.c_str());
    if (state.is_object()) {
      if (state["target"].is_number()) {
        sensor->update_target(state["target"].template get<int>());
      }
      if (state["temperature"].is_number()) {
        sensor->update_value(state["temperature"].template get<int>());
      }
    }
  }
}

void ExtruderPanel::handle_extruder_select(lv_event_t *e) {
  uint32_t idx = lv_dropdown_get_selected(lv_event_get_target(e));
  if (idx >= extruders.size()) {
    return;
  }
  const std::string &name = extruders[idx];
  const int tool = extruder_tool(name);
  // a multi toolhead picks the head up with its T macro; otherwise only the
  // extruder Klipper drives changes
  if (Config::get_instance()->get<bool>("/ui/extruder_select_tool_change") && tool >= 0) {
    ws.gcode_script(fmt::format("T{}", tool));
  } else {
    ws.gcode_script(fmt::format("ACTIVATE_EXTRUDER EXTRUDER={}", name));
  }
}
#endif  // COSMOS

void ExtruderPanel::consume(json& j) {
  std::lock_guard<std::mutex> lock(lv_lock);
#ifndef COSMOS
  json &toolhead_extruder = j["/params/0/toolhead/extruder"_json_pointer];
  if (toolhead_extruder.is_string()) {
    set_active_extruder(toolhead_extruder.template get<std::string>());
  }
#endif

  auto target_value = j[json::json_pointer("/params/0/" + active_extruder + "/target")];
  if (!target_value.is_null()) {
    int target = target_value.template get<int>();
    load_extruder_temp.update_target(target);
    extrude_extruder_temp.update_target(target);
  }

  auto temp_value = j[json::json_pointer("/params/0/" + active_extruder + "/temperature")];
  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    load_extruder_temp.update_value(value);
    extrude_extruder_temp.update_value(value);
  }

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    if (pstat_state.template get<std::string>() == "printing") {
      lv_obj_move_background(panel_cont);
    }
  }
}

void ExtruderPanel::handle_callback(lv_event_t *e) {
  LOG_TRACE("handling extruder panel callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    const char * v = lv_btnmatrix_get_btn_text(selector, idx);

    for (Selector *sel : {&load_temp_selector, &speed_selector, &length_selector,
                          &extrude_temp_selector}) {
      if (selector == sel->get_selector()) {
        sel->set_selected_idx(idx);
      }
    }

    LOG_TRACE("selector {} {} {}", fmt::ptr(selector), idx, v);
    
  } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    Config *conf = Config::get_instance();
    auto selected = [](Selector &sel) {
      return lv_btnmatrix_get_btn_text(sel.get_selector(), sel.get_selected_idx());
    };

    if (btn == extrude_btn.get_container()) {
      if (!extrude_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string extrude_macro = conf->get<std::string>("/default_macros/extrude");
      ws.gcode_script(fmt::format(extrude_macro, selected(extrude_temp_selector),
                                  selected(length_selector), std::stoi(selected(speed_selector)) * 60));
    }

    if (btn == retract_btn.get_container()) {
      if (!retract_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string retract_macro = conf->get<std::string>("/default_macros/retract");
      ws.gcode_script(fmt::format(retract_macro, selected(extrude_temp_selector),
                                  selected(length_selector), std::stoi(selected(speed_selector)) * 60));
    }

    if (btn == unload_btn.get_container()) {
      if (!unload_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string unload_filament_macro = conf->get<std::string>("/default_macros/unload_filament");

      ws.gcode_script(fmt::format(unload_filament_macro, selected(load_temp_selector)));
    }

    if (btn == load_btn.get_container()) {
      if (!load_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string load_filament_macro = conf->get<std::string>("/default_macros/load_filament");

      ws.gcode_script(fmt::format(load_filament_macro, selected(load_temp_selector)));
    }

    ButtonContainer *cooldown_btn = btn == load_cooldown_btn.get_container() ? &load_cooldown_btn
                                    : btn == extrude_cooldown_btn.get_container() ? &extrude_cooldown_btn
                                    : nullptr;
    if (cooldown_btn != nullptr) {
      if (!cooldown_btn->start_pressed_transition(2000)) {
        return;
      }
      const std::string cooldown_macro = conf->get<std::string>("/default_macros/cooldown");
      ws.gcode_script(cooldown_macro);
    }
  }
}
