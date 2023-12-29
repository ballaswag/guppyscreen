#ifndef __FILE_PANEL_H__
#define __FILE_PANEL_H__

#include "lvgl/lvgl.h"
#include "button_container.h"
#include "hv/json.hpp"

#include <string>

using json = nlohmann::json;

class FilePanel {
 public:
  FilePanel(lv_obj_t *parent);
  ~FilePanel();

  void foreground();
  void refresh_view(json &j, const std::string &gcode_path);  
  lv_obj_t *get_container();
  const char* get_thumbnail_path();

 private:
  lv_obj_t *file_cont;
  lv_obj_t *thumbnail;
  lv_obj_t *fname_label;
  lv_obj_t *detail_label;
};

#endif // __FILE_PANEL_H__
