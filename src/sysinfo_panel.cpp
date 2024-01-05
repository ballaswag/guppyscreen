#include "sysinfo_panel.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <iterator>

LV_IMG_DECLARE(back);

#ifdef GUPPYSCREEN_VERSION
#define GS_VERSION GUPPYSCREEN_VERSION
#else
#define GS_VERSION "dev-snapshot"
#endif

std::vector<std::string> SysInfoPanel::log_levels = {
  "trace",
  "debug",
  "info"
};

SysInfoPanel::SysInfoPanel()
  : cont(lv_obj_create(lv_scr_act()))
  , network_label(lv_label_create(cont))
  , loglevel_dd(lv_dropdown_create(cont))
  , loglevel(1)
  , back_btn(cont, &back, "Back", &SysInfoPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_dropdown_set_options(loglevel_dd, fmt::format("{}", fmt::join(log_levels, "\n")).c_str());

  Config *conf = Config::get_instance();
  auto v = conf->get_json(conf->df() + "log_level");
  if (!v.is_null()) {
    auto it = std::find(log_levels.begin(), log_levels.end(), v.template get<std::string>());
    if (it != std::end(log_levels)) {
      loglevel = std::distance(log_levels.begin(), it);
      lv_dropdown_set_selected(loglevel_dd, loglevel);
    }
  } else {
    lv_dropdown_set_selected(loglevel_dd, loglevel);
  }

  lv_obj_add_event_cb(loglevel_dd, &SysInfoPanel::_handle_callback,
		      LV_EVENT_VALUE_CHANGED, this);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);	
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

SysInfoPanel::~SysInfoPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SysInfoPanel::foreground() {
  lv_obj_move_foreground(cont);

  auto ifaces = KUtils::get_interfaces();
  std::vector<std::string> network_detail;
  network_detail.push_back("Network");
  for (auto &iface : ifaces) {
    auto ip = KUtils::interface_ip(iface);
    network_detail.push_back(fmt::format("\t{}: {}", iface, ip));
  }
  lv_label_set_text(network_label, fmt::format("{}\n\nGuppyScreen\n\tVersion: " GS_VERSION,
					       fmt::join(network_detail, "\n")).c_str());
}

void SysInfoPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(cont);
    }
  } else if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    auto idx = lv_dropdown_get_selected(loglevel_dd);
    if (idx != loglevel) {
      if (loglevel < log_levels.size()) {
	loglevel = idx;	
	auto ll = spdlog::level::from_str(log_levels[loglevel]);

	spdlog::set_level(ll);
	spdlog::flush_on(ll);
	Config *conf = Config::get_instance();
	spdlog::debug("setting log_level to {}", log_levels[loglevel]);
	conf->set<std::string>(conf->df() + "log_level", log_levels[loglevel]);
	conf->save();
      }
    }
  }
}

  
