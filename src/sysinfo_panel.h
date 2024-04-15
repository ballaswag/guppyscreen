#ifndef __SYSINFO_PANEL_H__
#define __SYSINFO_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <string>

const std::vector<std::string> z_plus_types = {
  "UP arrow",
  "DOWN arrow"
};

class SysInfoPanel {
 public:
  SysInfoPanel();
  ~SysInfoPanel();

  void foreground();
  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    SysInfoPanel *panel = (SysInfoPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  lv_obj_t *cont;
  lv_obj_t *left_cont;
  lv_obj_t *right_cont;
  lv_obj_t *network_label;

  lv_obj_t *disp_sleep_cont;
  lv_obj_t *display_sleep_dd;

  lv_obj_t *ll_cont;
  lv_obj_t *loglevel_dd;
  uint32_t loglevel;

  lv_obj_t *estop_toggle_cont;
  lv_obj_t *prompt_estop_toggle;

  lv_obj_t *z_icon_toggle_cont;
  lv_obj_t *z_icon_toggle;
  
  ButtonContainer back_btn;

  static std::vector<std::string> log_levels;

  static std::vector<std::string> z_plus_types;
  
};

#endif //__SYSINFO_PANEL_H__
