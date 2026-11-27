#ifndef __PRINT_PANEL_H__
#define __PRINT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "file_panel.h"
#include "print_status_panel.h"
#include "mmu_panel.h"

#include <map>

class PrintPanel : public NotifyConsumer {
 public:
  PrintPanel(KWebSocketClient &ws, std::mutex &lv_lock, PrintStatusPanel &ps, MmuPanel *mmu,
             lv_obj_t *parent = NULL);
  ~PrintPanel();

  void consume(json &data);
  void populate_files(json &data, bool usb_available);
  void subscribe();
  void foreground();
  void background();
  lv_obj_t *get_container() { return files_cont; }
  static bool printer_busy();
  std::function<void()> print_started;
  void handle_callback(lv_event_t *event);
  void handle_back_btn(lv_event_t *event);
  void handle_print_callback(lv_event_t *event);
  void handle_file_list_change(json &d);
  void delete_file(uint32_t btn_idx);

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

  static void _delete_file(lv_obj_t *, uint32_t btn_idx, void *user_data) {
    auto *panel = static_cast<PrintPanel *>(user_data);
    panel->delete_dialog = NULL; // the confirmation closes itself after this callback
    panel->delete_file(btn_idx);
  };

 private:
  struct PrintFile {
    std::string name;
    std::string full_path;
    double date_modified;
    uint64_t size;
    json metadata;
    std::string thumbnail_path;

    bool contains_metadata() const { return !metadata.is_null(); }
  };

  struct FileTile {
    std::string path;
    lv_obj_t *container;
    lv_obj_t *preview;
    lv_obj_t *material;
    lv_obj_t *time;
    lv_obj_t *weight;
    bool requested = false;
    bool loading = true;
    lv_obj_t *loading_spinner = NULL;
    std::string filename_material;
    lv_obj_t *name = NULL;
  };

  void show_files(int scroll_y);
  FileTile create_tile(int width, int height);
  void assign_tile(FileTile &tile, const PrintFile &file);
  size_t tile_offset_for_scroll(int scroll_y, size_t tile_count) const;
  void update_visible_tiles();
  void update_source_tabs(bool usb_available);
  void select_source();
  void finish_refresh();
  void clear_files();
  void update_tile(FileTile &tile, const PrintFile &file);
  void update_tile_animations();
  void stop_tile_spinner(FileTile &tile);
  void invalidate_thumbnail(const PrintFile &file);
  void show_browser();
  void show_file_detail(PrintFile *f);
  void load_visible_metadata();
  void handle_metadata(const std::string &path, uint64_t generation, int width, json &data);
  void update_actions();
  void confirm_delete();
  PrintFile *find_file(const std::string &path);
  void start_print(const std::string &path);
  
  KWebSocketClient &ws;
  lv_obj_t *files_cont;
  lv_obj_t *spinner;
  lv_obj_t *source_tabs = NULL;
  lv_obj_t *printer_tab = NULL;
  lv_obj_t *usb_tab = NULL;
  lv_obj_t *browser_view;
  lv_obj_t *file_grid;
  lv_obj_t *file_view;
  lv_obj_t *file_dialog;
  lv_obj_t *delete_dialog = NULL;
  ButtonContainer close_btn;
  ButtonContainer print_btn;
  ButtonContainer delete_btn;
  std::map<std::string, PrintFile> files;
  PrintFile *cur_file;
  FilePanel file_panel;
  PrintStatusPanel &print_status;
  MmuPanel *mmu;
  std::vector<FileTile> tiles;
  std::vector<std::string> file_order;
  lv_obj_t *scroll_extent = NULL;
  size_t tile_offset = 0;
  int tile_columns = 0;
  int tile_width = 0;
  int tile_height = 0;
  bool updating_tiles = false;
  uint64_t view_generation = 0;
  uint64_t refresh_generation = 0;
  bool usb_available = false;
  bool active_usb = false;
  int source_scroll_y[2] = {0, 0};
  bool metadata_pending = false;
  bool detail_requested = false;
  bool refreshing_files;
  bool refresh_pending;
  bool visible;
  std::string pending_delete_path;
};

#endif // __PRINT_PANEL_H__
