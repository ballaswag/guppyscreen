#ifndef __PRINTER_SELECT_PANEL_H__
#define __PRINTER_SELECT_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"

#include <string>
#include <map>
#include <memory>

class PrinterSelectPanel;

class PrinterSelectContainer {
 public:
  PrinterSelectContainer(PrinterSelectPanel &ps,
			 lv_obj_t *parent,
			 const std::string &name,
			 const std::string &ip,
			 uint32_t port);

  ~PrinterSelectContainer();

  lv_obj_t *prompt(const std::string &prompt_text);

 private:
  PrinterSelectPanel &printer_select_panel;
  lv_obj_t *cont;
  std::string name;
};


class PrinterSelectPanel {
 public:
  PrinterSelectPanel();
  ~PrinterSelectPanel();

  void handle_input(lv_event_t *);
  void remove_printer(const std::string &n);
  void add_printer(const std::string &n,
		   const std::string &ip,
		   uint32_t port);
  
  void foreground();

 private:
  lv_obj_t *cont;
  lv_obj_t *top;
  lv_obj_t *left;
  lv_obj_t *printer_name;
  lv_obj_t *moonraker_ip;
  lv_obj_t *moonraker_port;
  /* lv_obj_t *moonraker_apikey; */
  lv_obj_t *right;
  ButtonContainer back_btn;
  lv_obj_t *kb;

  std::map<std::string, std::shared_ptr<PrinterSelectContainer>> printers;
};

#endif // __PRINTER_SELECT_PANEL_H__
