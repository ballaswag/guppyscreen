#include "selector.h"
#include "spdlog/spdlog.h"

#include <limits>

Selector::Selector(lv_obj_t *parent,
		   const char *label_text,
		   std::vector<const char*> m,
		   uint32_t default_idx,
		   int32_t width_pct,
		   int32_t height_pct,
		   lv_event_cb_t cb,
		   void *cb_data)
  : cont(lv_obj_create(parent))
  , label(lv_label_create(cont))
  , btnm(lv_btnmatrix_create(cont))
  , map(m)
  , selector_idx(default_idx)
{
  lv_obj_set_size(cont, LV_PCT(width_pct), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 0, 0);

  auto height = (double)lv_disp_get_physical_ver_res(NULL) * (height_pct / 100.0);
  height = height < 50 ? 50 : height;
  lv_obj_set_size(btnm, LV_PCT(100), height);
  lv_label_set_text(label, label_text);  
  lv_obj_set_width(label, LV_PCT(100));  
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  
  lv_btnmatrix_set_map(btnm, &map[0]);
  lv_obj_set_style_pad_all(btnm, 4, LV_PART_MAIN);

  lv_obj_set_style_outline_width(btnm, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    
  lv_obj_add_event_cb(btnm, cb, LV_EVENT_VALUE_CHANGED, cb_data);
  
  // select one only
  lv_btnmatrix_set_btn_ctrl_all(btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(btnm, true);
  if (selector_idx != std::numeric_limits<uint32_t>::max()) {
    lv_btnmatrix_set_btn_ctrl(btnm, selector_idx, LV_BTNMATRIX_CTRL_CHECKED);
  } 
}

Selector::Selector(lv_obj_t *parent,
		   const char *label_text,
		   std::vector<const char*> m,
		   uint32_t default_idx,
		   lv_event_cb_t cb,
		   void *cb_data)
  : Selector(parent, label_text, m, default_idx, 62, 15, cb, cb_data)
{
}
		   
Selector::~Selector() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *Selector::get_container() {
  return cont;
}

lv_obj_t *Selector::get_selector() {
  return btnm;
}

lv_obj_t *Selector::get_label() {
  return label;
}

uint32_t Selector::get_selected_idx() {
  return selector_idx;
}

void Selector::set_selected_idx(uint32_t idx) {
  selector_idx = idx;
}
