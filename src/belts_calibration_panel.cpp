#include "belts_calibration_panel.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(back);

#define BELTS_PNG "belts_calibration.png"


BeltsCalibrationPanel::BeltsCalibrationPanel(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , graph_cont(lv_img_create(cont))
  , graph(lv_img_create(graph_cont))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , button_cont(lv_img_create(cont))
  , calibrate_btn(button_cont, &resume, "Calibrate", &BeltsCalibrationPanel::_handle_callback, this)
  , emergency_btn(button_cont, &emergency, "Stop", &BeltsCalibrationPanel::_handle_callback, this)
  , back_btn(button_cont, &back, "Back", &BeltsCalibrationPanel::_handle_callback, this)
  , image_fullsized(false)
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);

  lv_obj_set_style_pad_all(graph_cont, 0, 0);
  lv_obj_add_flag(graph_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(graph_cont, &BeltsCalibrationPanel::_handle_image_clicked,
		      LV_EVENT_CLICKED, this);
  lv_obj_set_size(graph_cont, LV_PCT(100), LV_PCT(80));

  lv_img_set_zoom(graph, 180);
  // lv_img_set_src(graph, "A:/usr/data/printer_data/thumbnails/belts_calibration.png");
  lv_obj_center(graph);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(spinner, 100, 100);

  lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_size(button_cont, LV_PCT(100), LV_PCT(20));

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  
  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_grid_cell(graph_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(spinner, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(button_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);

  ws.register_method_callback("notify_gcode_response",
			      "BeltsCalibrationPanel",
			      [this](json& d) { this->handle_macro_response(d); });
  
}

BeltsCalibrationPanel::~BeltsCalibrationPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void BeltsCalibrationPanel::foreground() {
  lv_obj_move_foreground(cont);
}

void BeltsCalibrationPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_target(event);
  if (btn == calibrate_btn.get_button()) {
    ws.gcode_script("G28");
    
    ws.gcode_script(fmt::format("BELTS_SHAPER_CALIBRATION FREQ_START={} FREQ_END={}", 5, 10));

    lv_obj_add_flag(graph, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(graph, NULL);    
    lv_obj_invalidate(graph);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(spinner);

  } else if (btn == back_btn.get_button()) {
    lv_obj_move_background(cont);
  } else if (btn == emergency_btn.get_button()) {
    ws.send_jsonrpc("printer.emergency_stop");
  }
}

void BeltsCalibrationPanel::handle_image_clicked(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t *clicked = lv_event_get_target(e);

    if (clicked == graph_cont) {
      if (image_fullsized) {
	lv_img_set_zoom(graph, 180);
	lv_obj_set_size(graph_cont, LV_PCT(100), LV_PCT(70));
	lv_obj_clear_flag(graph_cont, LV_OBJ_FLAG_FLOATING);	
	
      } else {
	lv_img_set_zoom(graph, LV_IMG_ZOOM_NONE);
	lv_obj_set_size(graph_cont, LV_PCT(100), LV_PCT(100));
	lv_obj_add_flag(graph_cont, LV_OBJ_FLAG_FLOATING);	
      }
      lv_obj_move_foreground(graph_cont);      
      image_fullsized = !image_fullsized;
    } 
  }
}

void BeltsCalibrationPanel::handle_macro_response(json &j) {
  spdlog::debug("belts calbiration macro response: {}", j.dump());
  auto &v = j["/params/0"_json_pointer];
  if (!v.is_null()) {
    std::string resp = v.template get<std::string>();
    std::lock_guard<std::mutex> lock(lv_lock);
    if (resp.rfind("// Command {guppy_belts_calibration} finished", 0) == 0) {
      spdlog::debug("belts calbiration finished");
      auto config_root = KUtils::get_root_path("config");
      auto png_path = fmt::format("{}/{}", config_root.length() > 0 ? config_root : "/tmp" , BELTS_PNG);

      png_path = 
	fmt::format("A:{}", KUtils::is_running_local()
		    ? png_path
		    : KUtils::download_file("config", BELTS_PNG, Config::get_instance()->get_thumbnail_path()));

      lv_img_set_src(graph, png_path.c_str());
      lv_obj_clear_flag(graph, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_background(spinner);
    }
  }
}
