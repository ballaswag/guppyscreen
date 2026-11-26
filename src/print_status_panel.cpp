#include "print_status_panel.h"
#include "config.h"
#include "exclude_object_panel.h"
#include "finetune_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "icons.h"
#include "theme.h"

#include <algorithm>
#include <vector>

using namespace Theme;

double pi() { return std::atan(1)*4; }

PrintStatusPanel::PrintStatusPanel(KWebSocketClient &websocket_client,
				   std::mutex &lock,
				   lv_obj_t *mini_parent)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , finetune_panel(websocket_client, lock)
  , exclude_object_panel(websocket_client, lock)
  , mini_print_status(mini_parent, &PrintStatusPanel::_handle_callback, this)
  , status_cont(lv_obj_create(lv_scr_act()))
  , buttons_cont(lv_obj_create(status_cont))
  , finetune_btn(buttons_cont, Icons::FINE_TUNE_IMG, "Fine Tune", &PrintStatusPanel::_handle_callback, this)
  , objects_btn(buttons_cont, Icons::DELETE_IMG, "Objects", &PrintStatusPanel::_handle_callback, this)
  , pause_btn(buttons_cont, Icons::PAUSE_IMG, "Pause", &PrintStatusPanel::_handle_callback, this)
  , resume_btn(buttons_cont, Icons::RESUME, "Resume", &PrintStatusPanel::_handle_callback, this)
  , cancel_btn(buttons_cont, Icons::CANCEL, "Cancel", &PrintStatusPanel::_handle_callback, this,
	       "Cancel Print", "Do you want to cancel the print?", {"Back", "Cancel Print"})
  , back_btn(buttons_cont, Icons::BACK, "Back", &PrintStatusPanel::_handle_callback, this)
  , thumbnail_cont(lv_obj_create(status_cont))
  , thumbnail(lv_img_create(thumbnail_cont))
  , pbar_cont(lv_obj_create(thumbnail_cont))
  , progress_bar(lv_bar_create(pbar_cont))
  , progress_label(lv_label_create(pbar_cont))
  , detail_cont(lv_obj_create(status_cont))
  , extruder_temp(detail_cont, Icons::EXTRUDER, "20")
  , bed_temp(detail_cont, Icons::BED, "21")
  , chamber_temp(detail_cont, Icons::HEATER, "")
  , print_speed(detail_cont, Icons::SPEED_UP_IMG, "0 mm/s")
  , z_offset(detail_cont, Icons::HOME_Z, "0.0 mm")
  , flow_rate(detail_cont, Icons::EXTRUDE, "0.0 mm3/s")
  , layers(detail_cont, Icons::LAYERS_IMG, "...")
  , fans(detail_cont, Icons::FAN, "0%")
  , elapsed(detail_cont, Icons::CLOCK_IMG, "0s")
  , time_left(detail_cont, Icons::HOURGLASS, "...")
  , estimated_time_s(0)
  , filament_diameter(1.75) // XXX: check config
  , extruder_target(-1)
  , active_extruder_("extruder")
  , heater_bed_target(-1)
  , chamber_sensor_key_(Config::get_instance()->get<std::string>("/ui/chamber_temp_sensor"))
{
  // a full-screen overlay paints its own background; a plain
  // container is transparent scaffolding
  lv_obj_add_style(status_cont, &styles().screen, 0);
  lv_obj_move_background(status_cont);
  lv_obj_clear_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(status_cont, LV_PCT(100), LV_PCT(100));

  // the readouts fill the detail cell as a two-column grid. Only chips that
  // apply to this printer take a cell, so a missing chamber sensor never
  // leaves a hole; an odd one out spans the last row.
  std::vector<ImageLabel *> chips = {&extruder_temp, &bed_temp, &chamber_temp, &fans, &print_speed,
                                     &z_offset, &flow_rate, &layers, &elapsed, &time_left};
  if (chamber_sensor_key_.empty()) {
    lv_obj_add_flag(chamber_temp.get_container(), LV_OBJ_FLAG_HIDDEN);
    chips.erase(std::find(chips.begin(), chips.end(), &chamber_temp));
  }
  static lv_coord_t grid_main_col_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  // five rows at most; the terminator moves up when the chamber chip is absent
  static lv_coord_t grid_main_row_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                                  LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  grid_main_row_dsc_detail[(chips.size() + 1) / 2] = LV_GRID_TEMPLATE_LAST;
  lv_obj_set_grid_dsc_array(detail_cont, grid_main_col_dsc_detail, grid_main_row_dsc_detail);

  lv_obj_clear_flag(detail_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(detail_cont, &styles().row, 0);
  for (size_t i = 0; i < chips.size(); i++) {
    const bool last_alone = (i + 1 == chips.size()) && (chips.size() % 2 == 1);
    lv_obj_set_grid_cell(chips[i]->get_container(), LV_GRID_ALIGN_STRETCH, i % 2, last_alone ? 2 : 1,
                         LV_GRID_ALIGN_STRETCH, i / 2, 1);
  }

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(5), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(status_cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_add_style(buttons_cont, &styles().row, 0);
  lv_obj_clear_flag(buttons_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(buttons_cont, LV_FLEX_FLOW_ROW);

  // All actions share one row, with Back on the far right.
  for (ButtonContainer *b : {&finetune_btn, &objects_btn, &pause_btn, &resume_btn,
                             &cancel_btn, &back_btn}) {
    b->use_plain();
    lv_obj_set_height(b->get_container(), LV_PCT(100));
    lv_obj_set_flex_grow(b->get_container(), 1);
  }

  // the progress bar spans the preview column with its percentage drawn on
  // it; the box has the bar's height so the centred label measures as nothing
  lv_obj_add_style(pbar_cont, &styles().row, 0);
  lv_obj_set_size(pbar_cont, LV_PCT(100), scale_r(18));
  lv_obj_set_style_pad_hor(pbar_cont, gap(), 0);  // the bar is not flush with the screen edges
  lv_obj_set_size(progress_bar, LV_PCT(100), LV_PCT(100));
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

  lv_label_set_text(progress_label, "0%");
  lv_obj_set_style_text_font(progress_label, scale_font(12), 0);
  lv_obj_center(progress_label);

  // the preview is a REAL-size image: its box is its drawn size, so the flex
  // column centres it and stacks the bar under it without any pivot or box
  // arithmetic. handle_metadata zooms it to what the cell leaves for it, so a
  // tall thumbnail can never push the bar onto the action tiles.
  lv_obj_add_flag(thumbnail, LV_OBJ_FLAG_HIDDEN);  // until a print has a preview
  lv_obj_add_style(thumbnail_cont, &styles().row, 0);
  lv_obj_clear_flag(thumbnail_cont, LV_OBJ_FLAG_SCROLLABLE);  // never a scrollbar here
  lv_obj_set_scrollbar_mode(thumbnail_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(thumbnail_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(thumbnail_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // row 1
  lv_obj_set_grid_cell(thumbnail_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(detail_cont, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  //row 2
  lv_obj_set_grid_cell(buttons_cont, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
  
  ws.register_notify_update(this);
}

PrintStatusPanel::~PrintStatusPanel() {
  if (status_cont != NULL) {
    lv_obj_del(status_cont);
    status_cont = NULL;
  }

  ws.unregister_notify_update(this);
}

void PrintStatusPanel::foreground() {
  // populate();

  // if there was a filament runout on resume we need to re-enable resume button
  State* s = State::get_instance();
  auto &pstate = s->get_data("/printer_state/print_stats/state"_json_pointer);
  if (!pstate.is_null() && pstate.template get<std::string>() == "paused") {
    resume_btn.enable();
  }
  is_foreground_ = true;
  lv_obj_move_foreground(status_cont);
}

void PrintStatusPanel::background() {
  is_foreground_ = false;
  lv_obj_move_background(status_cont);
}

bool PrintStatusPanel::is_foreground() const {
  return is_foreground_;
}

void PrintStatusPanel::reset() {
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
  lv_label_set_text(progress_label, "0%");
  print_speed.update_label("0 mm/s");
  flow_rate.update_label("0.0 mm3/s");
  elapsed.update_label("0s");
  time_left.update_label("...");
  estimated_time_s = 0;

  auto v = State::get_instance()->get_data("/printer_state/configfile/settings/extruder/filament_diameter"_json_pointer);
  filament_diameter = v.is_number() ? v.template get<double>() : 1.750;
  extruder_target = -1;
  active_extruder_.clear();
  heater_bed_target = -1;

  // no preview until the next print's metadata arrives
  lv_obj_add_flag(thumbnail, LV_OBJ_FLAG_HIDDEN);
  lv_img_set_src(thumbnail, NULL);

  mini_print_status.reset();
  mini_print_status.hide();
}

void PrintStatusPanel::init(json &fan_cfgs) {
  fan_speeds.clear();
  std::vector<std::string> values;
  for (auto &f : fan_cfgs.items()) {
    std::string fan_name = f.key();

    auto fan_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", fan_name)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      fan_speeds.insert({fan_name, v});
      values.push_back(fmt::format("{}%", v));
    }

    fan_value = State::get_instance()->get_data(json::json_pointer(fmt::format("/printer_state/{}/speed", fan_name)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      fan_speeds.insert({fan_name, v});
      values.push_back(fmt::format("{}%", v));
    }
  }

  fans.update_label(fmt::format("{}", join(values, ", ")).c_str());

  reset();
  update_active_extruder(json::object());
  populate();
  json &pstat_state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  print_state_ = pstat_state.is_null() ? "" : pstat_state.template get<std::string>();
  if (!pstat_state.is_null()) {
    auto pstatus = pstat_state.template get<std::string>();
    if (pstatus != "printing" && pstatus != "paused") {
      mini_print_status.hide();
    } else {
      mini_print_status.show();
    }
    mini_print_status.update_status(pstatus);
  } else {
    mini_print_status.hide();
  }
}

void PrintStatusPanel::populate() {
  State* s = State::get_instance();
  auto &pstate = s->get_data("/printer_state/print_stats/state"_json_pointer);
  json& printfile = s->get_data("/printer_state/print_stats/filename"_json_pointer);
  if (!printfile.is_null()) {
    const std::string fname = printfile.template get<std::string>();
    if (fname.length() > 0) {
      json fname_input = {{"filename", fname }};
      ws.send_jsonrpc("server.files.metadata", fname_input,
		      [fname, this](json &d) { this->handle_metadata(fname, d); });

      mini_print_status.show();
    }
  }

  if (!pstate.is_null() && pstate.template get<std::string>() == "paused") {
    lv_obj_clear_flag(resume_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pause_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(resume_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pause_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  }

  // progress percentage
  auto v = s->get_data("/printer_state/virtual_sdcard/progress"_json_pointer);
  if (!v.is_null()) {
    int new_value = static_cast<int>(v.template get<double>() * 100);
    lv_bar_set_value(progress_bar, new_value, LV_ANIM_ON);
    lv_label_set_text(progress_label, fmt::format("{}%", new_value).c_str());
    mini_print_status.update_progress(new_value);
  }

  v = s->get_data("/printer_state/gcode_move/homing_origin/2"_json_pointer);
  if (!v.is_null()) {
    std::string z_offset_str = fmt::format("{:.5} mm", v.template get<double>());
    // this is some dodgy shit not even sure why it happens
    if (z_offset_str.find("e-") == std::string::npos && z_offset_str.find("E-") == std::string::npos) {
      z_offset.update_label(z_offset_str.c_str());
    } else {
      z_offset.update_label("0.0 mm");
    }
  }
}

void PrintStatusPanel::handle_metadata(const std::string &gcode_file, json &j) {
  auto eta = j["/result/estimated_time"_json_pointer];
  if (!eta.is_null()) {
    estimated_time_s = static_cast<uint32_t>(eta.template get<float>());
    LOG_TRACE("updated eta {}", estimated_time_s);

    json &v = State::get_instance()->get_data("/printer_state/print_stats/print_duration"_json_pointer);
    if (!v.is_null()) {
      uint32_t passed = static_cast<uint32_t>(v.template get<float>());
      LOG_TRACE("updated time progress in handle metadata, passed {}", passed);

      std::lock_guard<std::mutex> lock(lv_lock);
      update_time_progress(passed);
    }
  }

  current_file = j["/result"_json_pointer];

  auto thumb_detail = KUtils::get_thumbnail(gcode_file, j, scale_w(180));
  std::string fullpath = thumb_detail.first;
  if (fullpath.length() > 0) {
    LOG_TRACE("thumb path: {}", fullpath);
    std::lock_guard<std::mutex> lock(lv_lock);
    const std::string img_path = "A:" + fullpath;

    lv_img_set_src(thumbnail, img_path.c_str());
    // as large as the cell leaves beside the progress bar, whole and in
    // proportion; a small bitmap is enlarged at most 2x so it stays crisp
    lv_obj_clear_flag(thumbnail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(thumbnail_cont);
    const int avail_w = lv_obj_get_content_width(thumbnail_cont);
    const int avail_h = lv_obj_get_content_height(thumbnail_cont) - lv_obj_get_height(pbar_cont)
                        - lv_obj_get_style_pad_row(thumbnail_cont, 0);
    fit_img(thumbnail, avail_w, avail_h, 2 * LV_IMG_ZOOM_NONE);
    mini_print_status.update_img(img_path, thumb_detail.second);
  }
}

void PrintStatusPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  auto& pstate = j["/params/0/print_stats/state"_json_pointer];
  if (!pstate.is_null()) {
    auto print_status = pstate.template get<std::string>();
	  
    // a print starts on any move into printing except a resume. Not on a
    // filename change: clearing a finished print sends an empty filename, and
    // reprinting the same file sends none
    if (print_status == "printing" && print_state_ != "printing" && print_state_ != "paused") {
      reset();
      populate(); // State already holds this update's filename
      foreground(); // auto move to front when print is detected
    }
    print_state_ = print_status;
	  
    if (print_status != "printing" && print_status != "paused") {
      mini_print_status.hide();
      if (print_status != "standby") {
        background();
      }
    } else {
      mini_print_status.show();
    }
    mini_print_status.update_status(print_status);
  }

  update_active_extruder(j);

  json v;
  if (!active_extruder_.empty()) {
    v = j[json::json_pointer("/params/0/" + active_extruder_ + "/target")];
    if (v.is_number()) {
      extruder_target = v.get<int>();
    }
  }

  v = j["/params/0/heater_bed/target"_json_pointer];
  if (!v.is_null()) {
    heater_bed_target = v.template get<int>();
  }

  v = active_extruder_.empty() ? json()
      : j[json::json_pointer("/params/0/" + active_extruder_ + "/temperature")];
  if (v.is_number()) {
    if (extruder_target > 0) {
      extruder_temp.update_label(fmt::format("{} / {}", v.template get<int>(), extruder_target).c_str());
    } else {
      extruder_temp.update_label(fmt::format("{}", v.template get<int>()).c_str());
    }
  }

  v = j["/params/0/heater_bed/temperature"_json_pointer];
  if (!v.is_null()) {
    if (heater_bed_target > 0) {
      bed_temp.update_label(fmt::format("{} / {}", v.template get<int>(), heater_bed_target).c_str());
    } else {
      bed_temp.update_label(fmt::format("{}", v.template get<int>()).c_str());
    }
  }

  v = j[json::json_pointer(fmt::format("/params/0/{}/temperature", chamber_sensor_key_))];
  if (!v.is_null()) {
    chamber_temp.update_label(fmt::format("{}", v.template get<int>()).c_str());
  }

  // speed
  auto speed = j["/params/0/motion_report/live_velocity"_json_pointer];
  if (!speed.is_null()) {
    int s = static_cast<int>(speed.template get<double>());
    print_speed.update_label((std::to_string(s) + " mm/s").c_str());
  }
  
  // zoffset
  v = j["/params/0/gcode_move/homing_origin/2"_json_pointer];
  if (!v.is_null()) {
    std::string z_offset_str = fmt::format("{:.5} mm", v.template get<double>());
    // this is some dodgy shit not even sure why it happens
    if (z_offset_str.find("e-") == std::string::npos && z_offset_str.find("E-") == std::string::npos) {
      z_offset.update_label(z_offset_str.c_str());
    } else {
      z_offset.update_label("0.0 mm");
    }
  }

  std::vector<std::string> values;
  for (auto &f : fan_speeds) {
    std::string fan_name = f.first;

    int fv = f.second;
    auto fan_value = j[json::json_pointer(fmt::format("/params/0/{}/value", fan_name))];
    if (!fan_value.is_null()) {
      fv = static_cast<int>(fan_value.template get<double>() * 100);
      f.second = fv;
    }

    fan_value = j[json::json_pointer(fmt::format("/params/0/{}/speed", fan_name))];
    if (!fan_value.is_null()) {
      fv = static_cast<int>(fan_value.template get<double>() * 100);
      f.second = fv;
    }
    values.push_back(fmt::format("{}%", fv));
  }

  fans.update_label(fmt::format("{}", join(values, ", ")).c_str());

  // progress
  v = j["/params/0/print_stats/print_duration"_json_pointer];
  if (!v.is_null()) {
    uint32_t passed = static_cast<uint32_t>(v.template get<float>());
    update_time_progress(passed);
  }

  // progress percentage
  v = j["/params/0/virtual_sdcard/progress"_json_pointer];
  if (!v.is_null()) {
    int new_value = static_cast<int>(v.template get<double>() * 100);
    lv_bar_set_value(progress_bar, new_value, LV_ANIM_ON);
    lv_label_set_text(progress_label, fmt::format("{}%", new_value).c_str());
    mini_print_status.update_progress(new_value);
  }

  v = j["/params/0/motion_report/live_extruder_velocity"_json_pointer];
  if (!v.is_null()) {
    double flow = pi() / 4 * std::pow(filament_diameter, 2) * v.template get<double>();
    flow_rate.update_label(fmt::format("{:.1f} mm3/s", flow > 0.0 ? flow : 0.0).c_str());
  }

  v = j["/params/0/pause_resume/is_paused"_json_pointer];
  if (!v.is_null()) {
    bool is_paused = v.template get<bool>();
    if (is_paused) {
      resume_btn.enable();
      lv_obj_clear_flag(resume_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
      
      pause_btn.disable();
      lv_obj_add_flag(pause_btn.get_container(), LV_OBJ_FLAG_HIDDEN);

    } else {
      pause_btn.enable();
      lv_obj_clear_flag(pause_btn.get_container(), LV_OBJ_FLAG_HIDDEN);      

      resume_btn.disable();
      lv_obj_add_flag(resume_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
    }
  }

  // layers
  v = j["/params/0/print_stats/info"_json_pointer];
  update_layers(v);
}

void PrintStatusPanel::update_active_extruder(const json &update) {
  // toolhead.extruder is Klipper's active heater (also after ACTIVATE_EXTRUDER).
  // Notifications are patches, so use the saved state when this field is absent.
  json reported = update.value("/params/0/toolhead/extruder"_json_pointer, json());
  if (reported.is_null()) {
    reported = State::get_instance()->get_data(
        "/printer_state/toolhead/extruder"_json_pointer);
  }
  const std::string name = reported.is_string()
      ? reported.get<std::string>() : "extruder";
  if (name == active_extruder_) {
    return;
  }
  active_extruder_ = name;
  extruder_target = -1;
  if (name.empty()) {
    extruder_temp.update_label("--");
    return;
  }
  const json state = State::get_instance()->get_data(
      json::json_pointer("/printer_state/" + name));
  if (!state.is_object()) {
    extruder_temp.update_label("--");
    return;
  }
  const auto target = state.value("target", json());
  const auto temperature = state.value("temperature", json());
  if (target.is_number()) {
    extruder_target = target.get<int>();
  }
  if (temperature.is_number()) {
    const int value = temperature.get<int>();
    const std::string label = extruder_target > 0
        ? fmt::format("{} / {}", value, extruder_target)
        : fmt::format("{}", value);
    extruder_temp.update_label(label.c_str());
  } else {
    extruder_temp.update_label("--");
  }
}

void PrintStatusPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    background();
  } else if (btn == pause_btn.get_container()) {
    if (!pause_btn.start_pressed_transition(2000)) {
      return;
    }
    ws.send_jsonrpc("printer.print.pause");
  } else if (btn == resume_btn.get_container()) {
    if (!resume_btn.start_pressed_transition(2000)) {
      return;
    }
    ws.send_jsonrpc("printer.print.resume");
  } else if (btn == cancel_btn.get_container()) {
    ws.send_jsonrpc("printer.print.cancel");
  } else if (btn == finetune_btn.get_container()) {
    finetune_panel.foreground();
  } else if (btn == objects_btn.get_container()) {
    exclude_object_panel.foreground();
  } else if (btn == mini_print_status.get_container()) {
    foreground();
  }
}

void PrintStatusPanel::update_time_progress(uint32_t time_passed) {
    int32_t remaining = estimated_time_s - time_passed;
    if (remaining < 0) {
      // XXX: better estimate
      time_left.update_label("...");
    } else {
      auto eta_str = KUtils::eta_string(remaining);
      time_left.update_label(eta_str.c_str());
      mini_print_status.update_eta(eta_str);
    }

    elapsed.update_label(KUtils::eta_string(time_passed).c_str());
}

void PrintStatusPanel::update_layers(json &info) {
  layers.update_label(fmt::format("{} / {}", current_layer(info), max_layer(info)).c_str());
}

int PrintStatusPanel::max_layer(json &info) {
  if (!info.is_null()) {
    auto v = info["/total_layer"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    }
  }

  // Status notifications are patches, so print_stats.info is normally absent
  // from updates unrelated to a layer change.  State retains the last value
  // reported by SET_PRINT_STATS_INFO.
  auto &stats_info = State::get_instance()->get_data("/printer_state/print_stats/info"_json_pointer);
  if (!stats_info.is_null()) {
    auto v = stats_info["/total_layer"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    }
  }

  if (!current_file.is_null()) {
    auto v = current_file["/layer_count"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    } else {
      auto first_layer_height = current_file["/first_layer_height"_json_pointer];
      auto layer_height = current_file["/layer_height"_json_pointer];
      auto object_height = current_file["/object_height"_json_pointer];

      if (!first_layer_height.is_null() && !layer_height.is_null() && !object_height.is_null()) {
        auto layer = static_cast<int>(std::ceil((object_height.template get<double>() - first_layer_height.template get<double>()) / layer_height.template get<double>() + 1));
        return layer > 0 ? layer : 0;
      }
    }
  }
  return 0;
}

int PrintStatusPanel::current_layer(json &info) {
  if (!info.is_null()) {
    auto v = info["/current_layer"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    }
  }

  // See max_layer(): use the persisted slicer-reported layer before falling
  // back to an estimate derived from the current Z position.
  auto &stats_info = State::get_instance()->get_data("/printer_state/print_stats/info"_json_pointer);
  if (!stats_info.is_null()) {
    auto v = stats_info["/current_layer"_json_pointer];
    if (!v.is_null()) {
      return v.template get<int>();
    }
  }

  if (!current_file.is_null()) {
    State *s = State::get_instance();
    auto pd = s->get_data("/printer_state/print_stats/print_duration"_json_pointer);
    auto zpos = s->get_data("/printer_state/gcode_move/gcode_position/2"_json_pointer);

    auto first_layer_height = current_file["/first_layer_height"_json_pointer];
    auto layer_height = current_file["/layer_height"_json_pointer];

    if (!pd.is_null()
        && pd.template get<int>() > 0
        && !zpos.is_null()
        && !first_layer_height.is_null()
        && !layer_height.is_null()) {
      auto layer = static_cast<int>(std::ceil((zpos.template get<double>() - first_layer_height.template get<double>()) / layer_height.template get<double>() + 1));
      auto total = max_layer(info);
      if (layer > total) {
        return total;
      }

      if (layer > 0) {
        return layer;
      }
    }
  }

  return 0;
}

FineTunePanel &PrintStatusPanel::get_finetune_panel() {
  return finetune_panel;
}
