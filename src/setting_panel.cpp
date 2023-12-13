#include "setting_panel.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(network_img);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(spoolman_img);

#ifdef ZBOLT
LV_IMG_DECLARE(info_img);
#else
LV_IMG_DECLARE(sysinfo_img);
#endif


SettingPanel::SettingPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent, SpoolmanPanel &sm)
  : ws(c)
  , cont(lv_obj_create(parent))
  , wifi_panel(l)
  , sysinfo_panel()
  , spoolman_panel(sm)    
  , wifi_btn(cont, &network_img, "WIFI", &SettingPanel::_handle_callback, this)
  , restart_klipper_btn(cont, &refresh_img, "Restart Klipper", &SettingPanel::_handle_callback, this)
  , restart_firmware_btn(cont, &refresh_img, "Restart Firmware", &SettingPanel::_handle_callback, this)
#ifdef ZBOLT
  , sysinfo_btn(cont, &info_img, "System Info", &SettingPanel::_handle_callback, this)
#else
  , sysinfo_btn(cont, &sysinfo_img, "System Info", &SettingPanel::_handle_callback, this)
#endif
  , spoolman_btn(cont, &spoolman_img, "Spoolman", &SettingPanel::_handle_callback, this)
{
  lv_obj_set_style_radius(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

  spoolman_btn.disable();

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(5), LV_GRID_FR(5), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1
  lv_obj_set_grid_cell(wifi_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(restart_klipper_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(restart_firmware_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(sysinfo_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

  // row 2
  lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  // lv_obj_set_grid_cell(finetune_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  // lv_obj_set_grid_cell(restart_klipper_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  // lv_obj_set_grid_cell(restart_firmware_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  
}

SettingPanel::~SettingPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *SettingPanel::get_container() {
  return cont;
}

void SettingPanel::handle_callback(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_target(event);

    if (btn == wifi_btn.get_button()) {
      spdlog::trace("wifi pressed");
      wifi_panel.foreground();
    } else if (btn == sysinfo_btn.get_button()) {
      spdlog::trace("setting system info pressed");
      sysinfo_panel.foreground();
    } else if (btn == restart_klipper_btn.get_button()) {
      spdlog::trace("setting restart klipper pressed");
      ws.send_jsonrpc("printer.restart");
    } else if (btn == restart_firmware_btn.get_button()) {
      spdlog::trace("setting restart klipper pressed");
      ws.send_jsonrpc("printer.firmware_restart");
    } else if (btn == spoolman_btn.get_button()) {
      spdlog::trace("setting spoolman pressed");
      spoolman_panel.foreground();
    }
  }
}

void SettingPanel::enable_spoolman() {
  spoolman_btn.enable();
}
