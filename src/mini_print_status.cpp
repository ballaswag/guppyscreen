#include "mini_print_status.h"
#include "spdlog/spdlog.h"

MiniPrintStatus::MiniPrintStatus(lv_obj_t *parent,
				 lv_event_cb_t cb,
				 void* user_data)
  : cont(lv_obj_create(parent))
  , progress_bar(lv_arc_create(cont))
  , thumb(lv_img_create(cont))
  , status_label(lv_label_create(cont))
  , status("n/a")
  , eta("...")
{
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_color_t cur_bg = lv_obj_get_style_bg_color(cont, 0);
  lv_color_t mixed = lv_color_mix(lv_palette_main(LV_PALETTE_GREY),
				  cur_bg, LV_OPA_10);
  
  lv_obj_set_style_bg_color(cont, mixed, 0);  
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);

  lv_obj_set_size(cont, LV_PCT(40), 50);
  lv_obj_set_style_pad_top(cont, 0, 0);
  lv_obj_set_style_pad_bottom(cont, 0, 0);
  
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_radius(cont, 4, 0);
  
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, -14);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user_data);

  lv_label_set_text(status_label, fmt::format("ETA: {}\nStatus: {}", eta, status).c_str());

  lv_arc_set_rotation(progress_bar, 270);
  lv_obj_set_size(progress_bar, 40, 40);
  lv_arc_set_bg_angles(progress_bar, 0, 360);
  lv_obj_remove_style(progress_bar, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(progress_bar);

  lv_img_set_size_mode(thumb, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_zoom(thumb, 30);
  
}

MiniPrintStatus::~MiniPrintStatus() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}


void MiniPrintStatus::show() {
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

void MiniPrintStatus::hide() {
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
}

lv_obj_t *MiniPrintStatus::get_container() {
  return cont;
}

void MiniPrintStatus::update_eta(std::string &eta_str) {
  eta = eta_str;
  lv_label_set_text(status_label, fmt::format("ETA: {}\nStatus: {}", eta, status).c_str());
}

void MiniPrintStatus::update_status(std::string &status_str) {
  status = status_str;
  lv_label_set_text(status_label, fmt::format("ETA: {}\nStatus: {}", eta, status).c_str());
}

void MiniPrintStatus::update_progress(int p) {
  lv_arc_set_value(progress_bar, p);
}

void MiniPrintStatus::update_img(const std::string &img_path) {
  lv_img_set_src(thumb, img_path.c_str());
}

void MiniPrintStatus::reset() {
  lv_arc_set_value(progress_bar, 0);
  eta = "...";
  status = "n/a";  
}

