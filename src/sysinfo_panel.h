#ifndef __SYSINFO_PANEL_H__
#define __SYSINFO_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"

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
  lv_obj_t *network_label;
  ButtonContainer back_btn;
};

#endif //__SYSINFO_PANEL_H__
