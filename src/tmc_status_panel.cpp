#include "tmc_status_panel.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(back);

TmcStatusPanel::TmcStatusPanel(KWebSocketClient &c,
			      std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , top(lv_obj_create(cont))
  , toggle(lv_switch_create(top))
  , back_btn(cont, &back, "Back", [](lv_event_t *e) {
    TmcStatusPanel *panel = (TmcStatusPanel*)e->user_data;
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
      panel->background();
    }
  }, this)
{
  lv_obj_move_background(cont);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_t *l = lv_label_create(top);
  lv_label_set_text(l, "TMC Metrics is experimental and disabled by default. "
		    "Turn it on as needed. For in-depth detail, refer to the TMC "
		    "driver datasheet.");
  lv_obj_set_width(l, LV_PCT(70));
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_clear_state(toggle, LV_STATE_CHECKED);

  lv_obj_add_event_cb(toggle, [](lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
      TmcStatusPanel *p = (TmcStatusPanel*)e->user_data;      
      if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
	p->ws.gcode_script("_GUPPY_LOAD_MODULE SECTION=tmcstatus");
      } else {
	p->ws.gcode_script("_GUPPY_UNLOAD_MODULE SECTION=tmcstatus");
      }
    }
  }, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);  
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  ws.register_notify_update(this);    
}

TmcStatusPanel::~TmcStatusPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void TmcStatusPanel::foreground() {
  lv_obj_move_foreground(cont);
}

void TmcStatusPanel::background() {
  lv_obj_move_background(cont);
}

void TmcStatusPanel::init(json &j) {
  auto tmc_status = j["/result/status/tmcstatus"_json_pointer];
  if (!tmc_status.is_null()) {
    if (!tmc_status.empty()) {
      lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    for (auto &el : tmc_status.items()) {
      const auto &s = metrics.find(el.key());
      if (s != metrics.end()) {

	s->second->update(el.value());
      } else {
	spdlog::debug("tmc stepper created {}", el.key());
	auto tmc_status_cont = std::make_shared<TmcStatusContainer>(ws, cont, el.key());
	metrics.insert({el.key(), tmc_status_cont});      
	tmc_status_cont->update(el.value());
      }
    }
  }
  
}

void TmcStatusPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto tmc_status = j["/params/0/tmcstatus"_json_pointer];
  if (!tmc_status.is_null()) {
    for (auto &el : tmc_status.items()) {
      const auto &s = metrics.find(el.key());
      if (s != metrics.end()) {
	// spdlog::debug("tmc stepper found {}", el.key());

	s->second->update(el.value());
      } else {
	spdlog::debug("tmc stepper created {}", el.key());
	auto tmc_status_cont = std::make_shared<TmcStatusContainer>(ws, cont, el.key());
	metrics.insert({el.key(), tmc_status_cont});      
	tmc_status_cont->update(el.value());
      }
    }
  }

  lv_obj_move_foreground(back_btn.get_container());
}
