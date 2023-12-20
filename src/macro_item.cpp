#include "macro_item.h"
#include "spdlog/spdlog.h"

MacroItem::MacroItem(KWebSocketClient &c,
		     lv_obj_t *parent,
		     std::string macro_name,
		     const std::map<std::string, std::string> &m_params,
		     lv_obj_t *keyboard,
		     lv_color_t bg_color) 
  : ws(c)
  , cont(lv_obj_create(parent))
  , top_cont(lv_obj_create(cont))
  , macro_label(lv_label_create(top_cont))
  , kb(keyboard)
{
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(cont, bg_color, 0);  
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);

  lv_obj_set_size(top_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(top_cont, bg_color, 0);  
  lv_obj_set_style_bg_opa(top_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP,
			       LV_PART_MAIN);  

  lv_obj_set_style_border_width(cont, 2, 0);

  lv_label_set_text(macro_label, macro_name.c_str());
  lv_obj_align(macro_label, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *run_btn = lv_btn_create(top_cont);
  lv_obj_align(run_btn, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_set_style_text_font(run_btn, &lv_font_montserrat_16, LV_STATE_DEFAULT);
  lv_obj_set_width(run_btn, 80);
  lv_obj_t *run_btn_label = lv_label_create(run_btn);
  lv_label_set_text(run_btn_label, LV_SYMBOL_PLAY);
  lv_obj_center(run_btn_label);
  lv_obj_add_event_cb(run_btn , &MacroItem::_handle_send_macro, LV_EVENT_CLICKED, this);


  if (!m_params.empty()) {
    lv_obj_t *params_cont = lv_obj_create(cont);
    lv_obj_set_style_bg_color(params_cont, bg_color, 0);  
    lv_obj_set_style_bg_opa(params_cont, LV_OPA_COVER, 0);
    
    lv_obj_set_size(params_cont, LV_PCT(70), LV_SIZE_CONTENT);

    lv_obj_set_flex_flow(params_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(params_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(params_cont, 0, 0);

    lv_obj_t *param_name;
    lv_obj_t *param_value;

    for (auto const & [k, v] : m_params) {
      param_name = lv_label_create(params_cont);
      lv_label_set_text(param_name, k.c_str());

      param_value = lv_textarea_create(params_cont);
      lv_textarea_set_one_line(param_value, true);
      lv_textarea_set_text(param_value, v.c_str());
      lv_obj_clear_flag(param_value, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_event_cb(param_value, &MacroItem::_handle_kb_input, LV_EVENT_ALL, this);

      lv_obj_set_width(param_name, LV_PCT(30));
      lv_obj_set_width(param_value, LV_PCT(45));

      params.push_back({param_name, param_value});
    }

  }
}

MacroItem::~MacroItem() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void MacroItem::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if(code == LV_EVENT_FOCUSED) {
    spdlog::trace("macro item focused");    
    lv_keyboard_set_textarea(kb, obj);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  
  if(code == LV_EVENT_DEFOCUSED) {
    spdlog::trace("macro item defocused");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  if (code == LV_EVENT_READY) {
    spdlog::trace("macro item keyboard ready");
  }

  if (code == LV_EVENT_CANCEL) {
    spdlog::trace("macro item keyboard close");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  
}

void MacroItem::handle_send_macro(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    const char *macro_name = lv_label_get_text(macro_label);
    std::vector<std::string> kv;
    kv.push_back(macro_name);
    for (const auto &p: params) {
      const char *v = lv_textarea_get_text(p.second);
      if (v != NULL && strlen(v) > 0) {
	const char *k = lv_label_get_text(p.first);
	kv.push_back(fmt::format("{}={}", k, v));
      }
    }

    spdlog::trace("sending macro: {}", fmt::format("{}", fmt::join(kv, " "))); 
    ws.gcode_script(fmt::format("{}", fmt::join(kv, " ")));
  }
}
