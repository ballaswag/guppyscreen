#include "macros_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

MacrosPanel::MacrosPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(parent))
  , top_controls(lv_obj_create(cont))
  , show_hide_switch(lv_switch_create(top_controls))
  , top_cont(lv_obj_create(cont))
  , kb(lv_keyboard_create(cont))
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 0, 0);

  lv_obj_set_size(top_controls, LV_PCT(100), LV_SIZE_CONTENT);

  lv_obj_align(show_hide_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *label = lv_label_create(top_controls);
  lv_label_set_text(label, "Show Hidden");
  lv_obj_align_to(label, show_hide_switch, LV_ALIGN_OUT_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(show_hide_switch, &MacrosPanel::_handle_hide_show, LV_EVENT_VALUE_CHANGED, this);
  
  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(top_cont, 0, 0);
  
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, LV_STATE_DEFAULT);
}

MacrosPanel::~MacrosPanel()
{
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void MacrosPanel::populate() {
  macro_items.clear();

  auto &config_json = State::get_instance()
    ->get_data("/printer_state/configfile/config"_json_pointer);

  // TODO: this is a race condition
  auto &macro_settings = State::get_instance()->get_data("/guppysettings/macros/settings"_json_pointer);

  if (!config_json.is_null()) {
    auto macros = KUtils::parse_macros(config_json);

    // uint32_t i = 0;
    // lv_color_t cur_bg = lv_obj_get_style_bg_color(cont, 0);
    // lv_color_t mixed = lv_color_mix(lv_palette_main(LV_PALETTE_GREY),
    // 				    cur_bg, LV_OPA_10);

    for (auto const & [k, v] : macros) {
      auto hidden_json = macro_settings[json::json_pointer(fmt::format("/{}/hidden", k))];
      bool hidden = !hidden_json.is_null() ? hidden_json.template get<bool>() : false;
      macro_items.push_back(std::make_shared<MacroItem>(ws, top_cont, k, v, kb, hidden));
      
      // if (i % 2 == 0) {
      // 	macro_items.push_back(std::make_shared<MacroItem>(ws, top_cont, k, v, kb, mixed, hidden));
      // } else {
      // 	macro_items.push_back(std::make_shared<MacroItem>(ws, top_cont, k, v, kb, cur_bg, hidden));
      // }
      // i++;
    }
  }
}

void MacrosPanel::handle_hide_show(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    bool show_hidden = lv_obj_has_state(show_hide_switch, LV_STATE_CHECKED);
    if (show_hidden) {
      for (const auto &m : macro_items) {
	m->show();
      }
    } else {
      for (const auto &m : macro_items) {
	m->hide_if_hidden();
      }
    }
  }
}
