#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "print_status_panel.h"

#include <mutex>
#include <cstdint>

class InitPanel {
 public:
  InitPanel(MainPanel &mp, std::mutex &l);
  ~InitPanel();

  // A Moonraker websocket can open before Klipper is ready. Keep that event
  // separate from notify_klippy_ready so startup has one owner.
  void websocket_connected(KWebSocketClient &ws);
  void klippy_ready(KWebSocketClient &ws);
  void klippy_disconnected(KWebSocketClient &ws);
  void klippy_shutdown(KWebSocketClient &ws);
  void websocket_disconnected(KWebSocketClient &ws);
  void waiting_for_moonraker();

 private:
  enum class Lifecycle {
    WaitingForKlipper,
    Initializing,
    Ready,
  };

  void start_initialization(KWebSocketClient &ws);
  bool initialization_active(uint64_t generation);
  bool initialization_current(uint64_t generation);
  void wait_for_klipper(uint64_t generation);
  void show_klippy_error(json &printer_info);
  void initialization_complete(uint64_t generation);
  void begin_waiting(const char *message, bool firmware_restart_available);
  void set_message(const char *message);
  void update_visibility();
  static void _tabview_event_cb(lv_event_t *event);
  lv_obj_t *cont;
  lv_obj_t *label;
  MainPanel &main_panel;
  std::mutex &lv_lock;
  bool waiting = true;
  Lifecycle lifecycle = Lifecycle::WaitingForKlipper;
  uint64_t lifecycle_generation = 0;
};

#endif // __INIT_PANEL_H__
