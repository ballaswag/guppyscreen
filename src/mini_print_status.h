#ifndef __MINI_PRINT_STATUS__
#define __MINI_PRINT_STATUS__

#include "lvgl/lvgl.h"
#include <string>

class MiniPrintStatus {
 public:
  MiniPrintStatus(lv_obj_t *parent,
		  lv_event_cb_t cb,
		  void* user_data);

  ~MiniPrintStatus();

  void show();
  void hide();
  lv_obj_t *get_container();

  void update_eta(std::string &eta_str);
  void update_status(std::string &status_str);
  void update_progress(int p);
  void update_img(const std::string &img_path, size_t twidth);
  void reset();

 private:
  lv_obj_t *cont;
  lv_obj_t *progress_bar;  
  lv_obj_t *thumb;
  lv_obj_t *status_label;
  std::string status;
  std::string eta;
};

#endif //__MINI_PRINT_STATUS__
