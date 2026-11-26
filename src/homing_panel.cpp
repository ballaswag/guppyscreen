#include "homing_panel.h"
#include "state.h"
#include "logger.h"
#include "config.h"
#include "icons.h"
#include "theme.h"

#include <cstdlib>

HomingPanel::HomingPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , homing_cont(Theme::create_screen(NULL))
  , home_all_btn(homing_cont, Icons::HOME, "Home All", &HomingPanel::_handle_callback, this)
  , home_xy_btn(homing_cont, Icons::HOME, "Home XY", &HomingPanel::_handle_callback, this)
  , y_up_btn(homing_cont, Icons::ARROW_UP, "Y+", &HomingPanel::_handle_callback, this)
  , y_down_btn(homing_cont, Icons::ARROW_DOWN, "Y-", &HomingPanel::_handle_callback, this)
  , x_up_btn(homing_cont, Icons::ARROW_RIGHT, "X+", &HomingPanel::_handle_callback, this)
  , x_down_btn(homing_cont, Icons::ARROW_LEFT, "X-", &HomingPanel::_handle_callback, this)
  , z_up_btn(homing_cont, Icons::Z_CLOSER, "Z+", &HomingPanel::_handle_callback, this)
  , z_down_btn(homing_cont, Icons::Z_FARTHER, "Z-", &HomingPanel::_handle_callback, this)
  , motoroff_btn(homing_cont, Icons::MOTOR_OFF_IMG, "Motors Off", &HomingPanel::_handle_callback, this)
  , back_btn(homing_cont, Icons::BACK, "Back", &HomingPanel::_handle_callback, this)
  // the keys fill whatever the row leaves below the title; the trailing "" is
  // the btnmatrix map terminator
  , distance_selector(homing_cont, "Move Distance (mm)",
		     {".1", ".5", "1", "5", "10", "25", "50", ""}, 2, &HomingPanel::_handle_selector_cb, this)
{
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(homing_cont, grid_main_col_dsc, grid_main_row_dsc);

  // Keep the XY movement cross and Z controls in two rows of four tiles.
  ButtonContainer *tiles[] = {&home_all_btn, &y_up_btn, &home_xy_btn, &z_up_btn,
                              &x_down_btn, &y_down_btn, &x_up_btn, &z_down_btn};
  for (int i = 0; i < 8; i++) {
    tiles[i]->use_plain();
    lv_obj_set_grid_cell(tiles[i]->get_container(), LV_GRID_ALIGN_STRETCH, i % 4, 1, LV_GRID_ALIGN_STRETCH, i / 4, 1);
  }

  // The selector fills the four movement columns. Motors Off and Back occupy
  // the right lane, with Back aligned to the bottom of the selector.
  distance_selector.seat_at_row_bottom();
  lv_obj_set_grid_cell(distance_selector.get_container(), LV_GRID_ALIGN_STRETCH, 0, 4, LV_GRID_ALIGN_END, 2, 1);
  motoroff_btn.use_plain();
  lv_obj_set_grid_cell(motoroff_btn.get_container(), LV_GRID_ALIGN_STRETCH, 4, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  back_btn.use_plain();
  lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_STRETCH, 4, 1, LV_GRID_ALIGN_END, 2, 1);
  back_btn.match_height(homing_cont, distance_selector.get_container());

  ws.register_notify_update(this);
}

HomingPanel::~HomingPanel() {
}

void HomingPanel::update_homing_controls(const std::string &homed_axes) {
  const bool x_axis_homed = homed_axes.find("x") != std::string::npos;
  const bool y_axis_homed = homed_axes.find("y") != std::string::npos;
  const bool z_axis_homed = homed_axes.find("z") != std::string::npos;

  if (x_axis_homed && y_axis_homed && z_axis_homed) {
    home_all_btn.disable();
    home_xy_btn.disable();
  } else {
    home_all_btn.enable();
    home_xy_btn.enable();
  }

  if (x_axis_homed) {
    x_up_btn.enable();
    x_down_btn.enable();
  } else {
    x_up_btn.disable();
    x_down_btn.disable();
  }

  if (y_axis_homed) {
    y_up_btn.enable();
    y_down_btn.enable();
  } else {
    y_up_btn.disable();
    y_down_btn.disable();
  }

  if (z_axis_homed) {
    z_up_btn.enable();
    z_down_btn.enable();
  } else {
    z_up_btn.disable();
    z_down_btn.disable();
  }
}

void HomingPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto v = j["/params/0/toolhead/homed_axes"_json_pointer];
  if (!v.is_null()) {
    std::string homed_axes = v.template get<std::string>();

    LOG_DEBUG("homed_axes is {}", homed_axes);

    update_homing_controls(homed_axes);
  }

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    if (pstat_state.template get<std::string>() == "printing") {
      lv_obj_move_background(homing_cont);
    } else if (pstat_state.template get<std::string>() == "paused") {
      home_all_btn.disable();
      home_xy_btn.disable();
      motoroff_btn.disable();
    } else {
      home_all_btn.enable();
      home_xy_btn.enable();
      motoroff_btn.enable();
    }
  }
}

lv_obj_t *HomingPanel::get_container() {
  return homing_cont;
}

void HomingPanel::foreground() {
  auto v = State::get_instance()->get_data("/printer_state/toolhead/homed_axes"_json_pointer);
  if (!v.is_null()) {
    std::string homed_axes = v.template get<std::string>();

    update_homing_controls(homed_axes);
  }

  const char *invert_env = std::getenv("INVERT_Z_ICON");
  const bool inverted = invert_env != nullptr
      ? std::string(invert_env) == "true"
      : Config::get_instance()->get<bool>("/ui/invert_z_icon");

  if (inverted) {
    // UP arrow
    z_up_btn.set_image(Icons::Z_FARTHER);
    z_down_btn.set_image(Icons::Z_CLOSER);
  } else {
    // DOWN arrow
    z_up_btn.set_image(Icons::Z_CLOSER);
    z_down_btn.set_image(Icons::Z_FARTHER);
  }

  lv_obj_move_foreground(homing_cont);
}

void HomingPanel::background() {
  lv_obj_move_background(homing_cont);
}

void HomingPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);  
  if (btn == back_btn.get_container()) {
    background();
    return;
  }
  const char * distance = lv_btnmatrix_get_btn_text(distance_selector.get_selector(),
						    distance_selector.get_selected_idx());
  if (btn == home_all_btn.get_container()) {
    if (!home_all_btn.start_pressed_transition(2000)) {
      return;
    }
    LOG_DEBUG("home all pressed");
    ws.gcode_script("G28");
  } else if (btn == home_xy_btn.get_container()) {
    if (!home_xy_btn.start_pressed_transition(2000)) {
      return;
    }
    LOG_DEBUG("home xy pressed");
    ws.gcode_script("G28 X Y");
  } else if (btn == y_up_btn.get_container()) {
    LOG_DEBUG("y up pressed");
    ws.gcode_script(fmt::format("_CLIENT_LINEAR_MOVE Y={} F=7800", distance));
  } else if (btn == y_down_btn.get_container()) {
    LOG_DEBUG("y down pressed");
    ws.gcode_script(fmt::format("_CLIENT_LINEAR_MOVE Y=-{} F=7800", distance));
  } else if (btn == x_up_btn.get_container()) {
    LOG_DEBUG("x up pressed");
    ws.gcode_script(fmt::format("_CLIENT_LINEAR_MOVE X={} F=7800", distance));
  } else if (btn == x_down_btn.get_container()) {
    LOG_DEBUG("x down pressed");
    ws.gcode_script(fmt::format("_CLIENT_LINEAR_MOVE X=-{} F=7800", distance));
  } else if (btn == z_up_btn.get_container()) {
    LOG_DEBUG("z up pressed");
    ws.gcode_script(fmt::format("_CLIENT_LINEAR_MOVE Z={} F=600", distance));
  } else if (btn == z_down_btn.get_container()) {
    LOG_DEBUG("z down pressed");
    ws.gcode_script(fmt::format("_CLIENT_LINEAR_MOVE Z=-{} F=600", distance));
  } else if (btn == motoroff_btn.get_container()) {
    if (!motoroff_btn.start_pressed_transition(1000)) {
      return;
    }
    LOG_DEBUG("motor off pressed");
    ws.gcode_script("M84");
  } else {
    LOG_DEBUG("Unknown action button pressed");
  }
}

void HomingPanel::handle_selector_cb(lv_event_t *event) {
  lv_obj_t * obj = lv_event_get_target(event);
  uint32_t idx = lv_btnmatrix_get_selected_btn(obj);
  distance_selector.set_selected_idx(idx);
  LOG_DEBUG("selector move distance index {}", idx);
}
