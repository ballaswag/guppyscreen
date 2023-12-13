#include "sysinfo_panel.h"
#include "utils.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(back);

#ifdef GUPPYSCREEN_VERSION
#define GS_VERSION GUPPYSCREEN_VERSION
#else
#define GS_VERSION "dev-snapshot"
#endif

SysInfoPanel::SysInfoPanel()
  : cont(lv_obj_create(lv_scr_act()))
  , network_label(lv_label_create(cont))
  , back_btn(cont, &back, "Back", &SysInfoPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);
  lv_obj_set_style_radius(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

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
    lv_obj_t *btn = lv_event_get_target(e);

    if (btn == back_btn.get_button()) {
      lv_obj_move_background(cont);
    }
  }
}
