#include "file_panel.h"
#include "theme.h"
#include "config.h"
#include "state.h"
#include "utils.h"
#include "logger.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

FilePanel::FilePanel(lv_obj_t *parent)
  : file_cont(lv_obj_create(parent))
  , thumbnail(lv_img_create(file_cont))
  , fname_label(lv_label_create(file_cont))
  , detail_label(lv_label_create(file_cont))
{
  // a panel: thumbnail on top, then the name, then the details, all centred
  lv_obj_add_style(file_cont, &Theme::styles().panel, 0);
  lv_obj_set_size(file_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(file_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(file_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(file_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(file_cont, Theme::gap(), 0);

  // REAL: the object is the zoomed bitmap, so the labels below never get
  // overdrawn; refresh_view() picks the zoom that fits the space left over
  lv_img_set_size_mode(thumbnail, LV_IMG_SIZE_MODE_REAL);
  lv_obj_set_width(fname_label, LV_PCT(100));
  lv_label_set_long_mode(fname_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(fname_label, Theme::scale_font(14), 0);
  lv_obj_set_style_max_height(fname_label, 2 * lv_font_get_line_height(Theme::scale_font(14)), 0);
  lv_obj_set_style_text_align(fname_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(fname_label, "");
  lv_obj_add_style(detail_label, &Theme::styles().dim_label, 0);
  lv_obj_set_style_text_font(detail_label, Theme::scale_font(14), 0);
  lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(detail_label, "");
  lv_obj_add_event_cb(file_cont, [](lv_event_t *e) {
    static_cast<FilePanel *>(lv_event_get_user_data(e))->file_cont = NULL;
  }, LV_EVENT_DELETE, this);
}

FilePanel::~FilePanel() {
  if (file_cont != NULL) {
    lv_obj_del(file_cont);
    file_cont = NULL;
  }
}

void FilePanel::clear_view(const std::string &gcode_path, const char *message) {
  lv_label_set_text(fname_label, gcode_path.c_str());
  lv_label_set_text(detail_label, message);
  lv_img_set_src(thumbnail, NULL);
  lv_obj_add_flag(thumbnail, LV_OBJ_FLAG_HIDDEN);
}

void FilePanel::refresh_view(json &j, const std::string &gcode_path, const std::string &thumbnail_path) {
  auto v = j["/result/modified"_json_pointer];
  std::stringstream time_stream;  
  if (!v.is_null()) {
    std::time_t timestamp = v.template get<std::time_t>();
    std::tm lt = *std::localtime(&timestamp);
    time_stream << std::put_time(&lt, "%Y-%m-%d %H:%M");
  } else {
    time_stream << "(unknown)";
  }
  
  v = j["/result/estimated_time"_json_pointer];
  int eta =  v.is_null() ? -1 : v.template get<int>();
  v = j["/result/filament_weight_total"_json_pointer];
  double fweight = v.is_null() ? -1 : v.template get<double>();

  lv_label_set_text(fname_label, gcode_path.c_str());
  
  std::string detail = fmt::format("Filament Weight: {} g\nPrint Time: {}\nSize: {} MB",
				   fweight > 0 ? fmt::format("{:.1f}", fweight) : "(unknown)",
				   eta > 0 ? KUtils::eta_string(eta) : "(unknown)",
				   KUtils::bytes_to_mb(j["result"].value("size", size_t(0))));

  lv_label_set_text(detail_label, detail.c_str());
  if (!thumbnail_path.empty()) {
    lv_img_set_src(thumbnail, ("A:" + thumbnail_path).c_str());
    lv_obj_clear_flag(thumbnail, LV_OBJ_FLAG_HIDDEN);
    // fit the bitmap to the slot the labels leave it, both ways; a small
    // thumbnail may grow to fill it, but no further than 2x or it blurs
    lv_obj_update_layout(file_cont);
    const lv_coord_t slot_w = lv_obj_get_content_width(file_cont);
    const lv_coord_t slot_h = lv_obj_get_content_height(file_cont) - lv_obj_get_height(fname_label)
                              - lv_obj_get_height(detail_label) - 2 * lv_obj_get_style_pad_row(file_cont, 0);
    Theme::fit_img(thumbnail, slot_w, slot_h, 2 * LV_IMG_ZOOM_NONE);
  } else {
    lv_img_set_src(thumbnail, NULL);
    lv_obj_add_flag(thumbnail, LV_OBJ_FLAG_HIDDEN);
  }
}

void FilePanel::foreground() {
  lv_obj_move_foreground(file_cont);
}

lv_obj_t *FilePanel::get_container() {
  return file_cont;
}
