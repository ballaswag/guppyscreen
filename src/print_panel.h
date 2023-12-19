#ifndef __PRINT_PANEL_H__
#define __PRINT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "file_panel.h"
#include "print_status_panel.h"
#include "tree.h"

class PrintPanel : public NotifyConsumer {
 public:
  PrintPanel(KWebSocketClient &ws, std::mutex &lv_lock, PrintStatusPanel &ps);
  ~PrintPanel();

  void consume(json &data);
  void populate_files(json &data);
  void subscribe();
  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_metadata(Tree *, json & data);
  void handle_back_btn(lv_event_t *event);
  void handle_print_callback(lv_event_t *event);
  void handle_status_btn(lv_event_t *event);
  void handle_btns(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_back_btn(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_back_btn(event);
  };
  
  static void _handle_print_callback(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_print_callback(event);
  };

  static void _handle_status_btn(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_status_btn(event);
  };

  static void _handle_btns(lv_event_t *event) {
    PrintPanel *panel = (PrintPanel*)event->user_data;
    panel->handle_btns(event);
  };
  
  
 private:
  void show_dir(const Tree *dir, uint32_t sort_type);
  void show_file_detail(Tree *f);
  
  KWebSocketClient &ws;
  lv_obj_t *files_cont;

  // prompt
  lv_obj_t *prompt_cont;
  lv_obj_t *msgbox;
  lv_obj_t *job_btn;
  lv_obj_t *cancel_btn;
  lv_obj_t *queue_btn;

  lv_obj_t *left_cont;
  lv_obj_t *file_table_btns;
  lv_obj_t *refresh_btn;
  lv_obj_t *modified_sort_btn;
  lv_obj_t *az_sort_btn;
  
  lv_obj_t *file_table;
  lv_obj_t *file_view;
  ButtonContainer status_btn;
  ButtonContainer print_btn;
  ButtonContainer back_btn;
  Tree root;
  Tree *cur_dir;
  Tree *cur_file;
  FilePanel file_panel;
  PrintStatusPanel &print_status;
  uint32_t sorted_by;

};

#endif // __PRINT_PANEL_H__
