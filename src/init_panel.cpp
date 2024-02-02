#include "init_panel.h"
#include "utils.h"
#include "state.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstdio>

InitPanel::InitPanel(MainPanel &mp, BedMeshPanel &bmp, std::mutex& l)
  : cont(lv_obj_create(lv_scr_act()))
  , label_cont(lv_obj_create(cont))
  , label(lv_label_create(label_cont))
  , main_panel(mp)
  , bedmesh_panel(bmp)
  , lv_lock(l)
{
  lv_obj_set_size(cont, LV_PCT(55), LV_PCT(30));
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);  
  
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  // lv_obj_set_style_bg_opa(cont, LV_OPA_70, 0);

  lv_obj_set_size(label_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_border_width(label_cont, 2, 0);
  lv_obj_set_style_bg_color(label_cont, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
  
  lv_obj_align(label_cont, LV_ALIGN_CENTER, 0, 0);  

  lv_label_set_text(label, LV_SYMBOL_WARNING " Waiting for printer to initialize...");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

InitPanel::~InitPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void InitPanel::connected(KWebSocketClient &ws) {
  spdlog::debug("init panel connected");
  ws.send_jsonrpc("printer.objects.list",
      [this, &ws](json& d) {
	State *state = State::get_instance();
	state->reset();
	state->set_data("printer_objs", d, "/result");

	ws.send_jsonrpc("server.files.roots",
			[](json& j) { State::get_instance()->set_data("roots", j, "/result"); });

	ws.send_jsonrpc("printer.info",
			[](json& j) { State::get_instance()->set_data("printer_info", j, "/result"); });
	
	json h = {
	  { "namespace", "fluidd" },
	  { "key", "console" }
	};
	ws.send_jsonrpc("server.database.get_item", h,
			[](json& j) { State::get_instance()->set_data("console", j, "/result/value"); });

	h = {
	  { "namespace", "guppyscreen" }
	};
	ws.send_jsonrpc("server.database.get_item", h,
			[](json& j) { State::get_instance()->set_data("guppysettings", j, "/result/value"); });	

	// console
	this->main_panel.subscribe();

	// spoolman
	ws.send_jsonrpc("server.info", [this](json &j) {
	  spdlog::debug("server_info {}", j.dump());
	  State::get_instance()->set_data("server_info", j, "/result");
	  
	  auto &components = j["/result/components"_json_pointer];
	  if (!components.is_null()) {
	    const auto &has_spoolman = components.template get<std::vector<std::string>>();
	    if (std::find(has_spoolman.begin(), has_spoolman.end(), "spoolman") != has_spoolman.end()) {
	      this->main_panel.enable_spoolman();
	    }
	  }
	});

	auto display_sensors = state->get_display_sensors();
	this->main_panel.create_sensors(display_sensors);

	auto display_fans = state->get_display_fans();
	this->main_panel.create_fans(display_fans);

	auto display_leds = state->get_display_leds();
	this->main_panel.create_leds(display_leds);

	// subscribe to all objects except gcode_macro
	auto objs = d["/result/objects"_json_pointer];
	if (!objs.is_null()) {
	  json sub_objs;
	  for (auto &obj : objs) {
	    std::string obj_name = obj.template get<std::string>();
	    if (obj_name.rfind("gcode_macro ", 0 ) != 0) {
	      sub_objs[obj_name] = nullptr;
	    }
	  }

	  sub_objs["tmcstatus"] = nullptr;

	  json subs = {{ "objects", sub_objs }};
	  spdlog::debug("subcribing to {}", subs.dump());
	  ws.send_jsonrpc("printer.objects.subscribe", subs,
			  [this](json &data) {
			    State::get_instance()->set_data("printer_state",
							    data, "/result/status");
			    this->main_panel.init(data);
			    this->bedmesh_panel.refresh_views_with_lock(
									data["/result/status/bed_mesh"_json_pointer]);
			    spdlog::debug("done init");
			    std::lock_guard<std::mutex> lock(this->lv_lock);
			    lv_obj_add_flag(this->cont, LV_OBJ_FLAG_HIDDEN);
			    lv_obj_move_background(this->cont);
					
			  });
	}
      });
}

void InitPanel::disconnected(KWebSocketClient &ws) {
  spdlog::debug("init panel disconnected");
  std::lock_guard<std::mutex> lock(lv_lock);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}
