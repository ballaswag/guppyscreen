#include "file_panel.h"
#include "requests.h"
#include "hurl.h"
#include "config.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

FilePanel::FilePanel(lv_obj_t *parent, json &j, const std::string &gcode_path)
  : file_cont(lv_obj_create(parent))
  , thumbnail(lv_img_create(file_cont))
  , detail_label(lv_label_create(file_cont))
{
  lv_obj_set_size(file_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(file_cont, LV_OBJ_FLAG_SCROLLABLE);  
  lv_obj_align(file_cont, LV_ALIGN_CENTER, 0, 0);

  std::time_t timestamp = j["result"]["modified"].template get<std::time_t>();
  std::tm lt = *std::localtime(&timestamp);
  std::stringstream time_stream;
  time_stream << std::put_time(&lt, "%Y-%m-%d %H:%M");

  auto v = j["/result/estimated_time"_json_pointer];
  int eta =  v.is_null() ? -1 : v.template get<int>();
  v = j["/result/filament_weight_total"_json_pointer];
  int fweight = v.is_null() ? -1 : v.template get<int>();

  std::string detail = fmt::format("Filament Weight: {} g\nPrint Time: {}\nSize: {} MB\nModified: {}",
				   fweight > 0 ? std::to_string(fweight) : "(unknown)",
				   eta > 0 ? KUtils::eta_string(eta) : "(unknown)",
				   KUtils::bytes_to_mb(j["result"]["size"].template get<size_t>()),
				   time_stream.str());
  
  // std::string detail = "Modified: " + time_stream.str() + "\n" +
  //   "Size: " + std::to_string(j["result"]["size"].template get<size_t>()) + " Bytes";

  std::string fullpath = KUtils::get_thumbnail(gcode_path, j);
  lv_label_set_text(detail_label, detail.c_str());
  
  if (fullpath.length() > 0) {
    lv_img_set_src(thumbnail, ("A:" + fullpath).c_str());
    lv_img_set_zoom(thumbnail, 150);
  }

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(file_cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_grid_cell(thumbnail, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(detail_label, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 1, 1);
}

FilePanel::~FilePanel() {
  if (file_cont != NULL) {
    lv_obj_del(file_cont);
    file_cont = NULL;
  }
}

void FilePanel::foreground() {
  lv_obj_move_foreground(file_cont);
}

lv_obj_t *FilePanel::get_container() {
  return file_cont;
}

const char* FilePanel::get_thumbnail_path() {
  return (const char*)lv_img_get_src(thumbnail);
}
