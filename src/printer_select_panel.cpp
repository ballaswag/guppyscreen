#include "printer_select_panel.h"
#include "guppyscreen.h"
#include "config.h"
#include "hv/json.hpp"
#include "subprocess.hpp"

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;
using json = nlohmann::json;

LV_IMG_DECLARE(back);

PrinterSelectContainer::PrinterSelectContainer(PrinterSelectPanel &ps,
					       lv_obj_t *parent,
					       const std::string &pname,
					       const std::string &ip,
					       uint32_t port)
  : printer_select_panel(ps)
  , cont(lv_obj_create(parent))
  , name(pname)
{
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);  
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);  
  lv_obj_set_style_border_width(cont, 2, 0);
  
  lv_obj_t *l = lv_label_create(cont);
  lv_label_set_text(l, fmt::format("Name: {}\nIP: {}\nPort: {}", pname, ip, port).c_str());
  lv_obj_set_width(l, LV_PCT(55));

  lv_obj_t *btn = lv_btn_create(cont);
  l = lv_label_create(btn);
  lv_label_set_text(l, "Switch");
  lv_obj_center(l);

  lv_obj_add_event_cb(btn, [](lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
      PrinterSelectContainer *p = (PrinterSelectContainer*)e->user_data;
      lv_obj_t *msgbox = p->prompt(fmt::format("Guppy Screen will restart. Do you want to switch to {}?",
					       p->name));
      lv_obj_add_event_cb(msgbox, [](lv_event_t *e) {
	lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(e));
	uint32_t clicked_btn = lv_msgbox_get_active_btn(obj);
	if(clicked_btn == 0) {
	  
	  
	  Config *conf = Config::get_instance();
	  conf->set<std::string>("/default_printer", ((PrinterSelectContainer*)e->user_data)->name);
	  conf->save();
		  
	  auto init_script = conf->get<std::string>("/guppy_init_script");
	  const fs::path script(init_script);
	  if (fs::exists(script)) {
	    sp::call({init_script, "restart"});
	  }

	}
	
	lv_msgbox_close(obj);

      }, LV_EVENT_VALUE_CHANGED, p);

    }
  }, LV_EVENT_CLICKED, this);
  

  btn = lv_btn_create(cont);
  l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_CLOSE);
  lv_obj_center(l);

  lv_obj_add_event_cb(btn, [](lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
      PrinterSelectContainer *p = (PrinterSelectContainer*)e->user_data;
      lv_obj_t *msgbox = p->prompt(fmt::format("Are you sure you want to delete {}?", p->name));
      lv_obj_add_event_cb(msgbox, [](lv_event_t *e) {
	lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(e));
	uint32_t clicked_btn = lv_msgbox_get_active_btn(obj);
	if(clicked_btn == 0) {
	  auto *p = (PrinterSelectContainer*)e->user_data;
	  p->printer_select_panel.remove_printer(p->name);
	}
	
	lv_msgbox_close(obj);

      }, LV_EVENT_VALUE_CHANGED, p);
    }
  }, LV_EVENT_CLICKED, this);
}

PrinterSelectContainer::~PrinterSelectContainer() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *PrinterSelectContainer::prompt(const std::string &prompt_text) {
  static const char * btns[] = {"Confirm", "Cancel", ""};

  lv_obj_t *mbox1 = lv_msgbox_create(NULL, NULL, prompt_text.c_str(), btns, false);
  lv_obj_t *msg = ((lv_msgbox_t*)mbox1)->text;
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);  
  lv_obj_set_width(msg, LV_PCT(100));
  lv_obj_center(msg);
  
  lv_obj_t *btnm = lv_msgbox_get_btns(mbox1);
  lv_btnmatrix_set_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_btnmatrix_set_btn_ctrl(btnm, 1, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);
  
  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

  lv_obj_set_size(btnm, LV_PCT(90), 50 *hscale);
  lv_obj_set_size(mbox1, LV_PCT(50), LV_PCT(35));
  lv_obj_center(mbox1);
  return mbox1;
}

PrinterSelectPanel::PrinterSelectPanel()
  : cont(lv_obj_create(lv_scr_act()))
  , top(lv_obj_create(cont))
  , left(lv_obj_create(top))
  , printer_name(lv_textarea_create(left))
  , moonraker_ip(lv_textarea_create(left))
  , moonraker_port(lv_textarea_create(left))
  , right(lv_obj_create(top))
  , back_btn(cont, &back, "Back", [](lv_event_t *e) {
    PrinterSelectPanel *panel = (PrinterSelectPanel*)e->user_data;
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
      lv_obj_move_background(panel->cont);
    }
  }, this)
  , kb(lv_keyboard_create(cont))
{
  lv_obj_move_background(cont);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);  
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_grow(top, 1);
  lv_obj_set_width(top, LV_PCT(100));
  lv_obj_set_style_pad_all(top, 0, 0);

  // lv_obj_set_style_border_width(left, 2, 0);
  lv_obj_set_style_border_width(right, 2, 0);

  lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(left, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_textarea_set_placeholder_text(printer_name, "Printer Name");
  lv_textarea_set_max_length(printer_name, 128);
  lv_textarea_set_one_line(printer_name, true);
  lv_textarea_set_accepted_chars(printer_name,
				 "abcdefghijklmnopqrstuvwxyz"
				 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				 "0123456789"
				 " -_");
  lv_obj_add_event_cb(printer_name, [](lv_event_t *e) {
    PrinterSelectPanel *p = (PrinterSelectPanel*)e->user_data;
    p->handle_input(e);
  }, LV_EVENT_ALL, this);

  lv_textarea_set_placeholder_text(moonraker_ip, "Moonraker IP Address");
  lv_textarea_set_max_length(moonraker_ip, 45); // ipv6??
  lv_textarea_set_one_line(moonraker_ip, true);
  lv_textarea_set_accepted_chars(moonraker_ip, "0123456789abcdef.:");
  lv_obj_add_event_cb(moonraker_ip, [](lv_event_t *e) {
    PrinterSelectPanel *p = (PrinterSelectPanel*)e->user_data;
    p->handle_input(e);
  }, LV_EVENT_ALL, this);

  lv_textarea_set_text(moonraker_port, "7125");
  lv_textarea_set_placeholder_text(moonraker_port, "Moonraker Port");
  lv_textarea_set_one_line(moonraker_port, true);
  lv_textarea_set_max_length(moonraker_port, 5);
  lv_textarea_set_accepted_chars(moonraker_port, "0123456789");
  lv_obj_add_event_cb(moonraker_port, [](lv_event_t *e) {
    PrinterSelectPanel *p = (PrinterSelectPanel*)e->user_data;
    p->handle_input(e);
  }, LV_EVENT_ALL, this);

  lv_obj_t *btn = lv_btn_create(left);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_PLUS " Printer");
  lv_obj_center(l);

  // lv_obj_set_style_bg_img_src(btn, LV_SYMBOL_PLUS, 0);
  lv_obj_add_event_cb(btn, [](lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
      PrinterSelectPanel *p = (PrinterSelectPanel*)e->user_data;
      Config *conf = Config::get_instance();

      auto pname = std::string(lv_textarea_get_text(p->printer_name));
      auto ip = std::string(lv_textarea_get_text(p->moonraker_ip));

      const char *mp = lv_textarea_get_text(p->moonraker_port);
      auto port = mp != NULL ? std::stoi(mp) : 7125;

      json new_printer = {
	      {"moonraker_api_key", false},
	      {"moonraker_host", ip},
	      {"moonraker_port", port},
	      {"monitored_sensors", {} },
	      {"fans", {} },
	      {"default_macros", {
		  {"load_filament", "LOAD_MATERIAL"},
		  {"unload_filament", "UNLOAD_MATERIAL"},
		  { "cooldown", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0\nSET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0"}
		}
	      }
      };

      auto printers = conf->get<json>("/printers");
      printers[pname] = new_printer;
      conf->set<json>("/printers", printers);
      conf->save();

      p->add_printer(pname, ip, port);

      lv_obj_add_flag(p->kb, LV_OBJ_FLAG_HIDDEN);

      if (conf->get_json("/default_printer").is_null()) {
        // connect to the one and only added printer
        conf->set<std::string>("/default_printer", pname);
        conf->save();
        conf->init(conf->get<std::string>("/config_path"), conf->get<std::string>("/thumbnail_path"));
        std::string ws_url = fmt::format("ws://{}:{}/websocket", ip, port);
        GuppyScreen::get()->connect_ws(ws_url);
      }
    }
  }, LV_EVENT_CLICKED, this);

  lv_obj_set_flex_grow(right, 1);
  lv_obj_set_height(right, LV_PCT(100));
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW_WRAP);

  Config *conf = Config::get_instance();    
  const auto &configured_printers = conf->get<json>("/printers");

  for (auto &el : configured_printers.items()) {
    auto &v = el.value();
    printers.insert({el.key(), std::make_shared<PrinterSelectContainer>(
			*this,
			right,
			el.key(),
			v["moonraker_host"].template get<std::string>(),
			v["moonraker_port"].template get<uint32_t>())});
  }

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);  
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);
}

PrinterSelectPanel::~PrinterSelectPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void PrinterSelectPanel::handle_input(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * ta = lv_event_get_target(e);

  if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
    if (ta == moonraker_ip || ta == moonraker_port) {
      lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    } else {
      lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    }

    lv_keyboard_set_textarea(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  if (code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

}

void PrinterSelectPanel::remove_printer(const std::string &n) {
  const auto &el = printers.find(n);
  if (el != printers.end()) {
    printers.erase(el);

    Config *conf = Config::get_instance();    
    auto printers = conf->get<json>("/printers");
    printers.erase(n);
    conf->set<json>("/printers", printers);
    conf->save();
  }
}

void PrinterSelectPanel::add_printer(const std::string &n,
				     const std::string &ip,
				     uint32_t port) {
  printers.insert({n, std::make_shared<PrinterSelectContainer>(*this, right, n, ip, port)});
}
  
void PrinterSelectPanel::foreground() {
  lv_obj_move_foreground(cont);
}
