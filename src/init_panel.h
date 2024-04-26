#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "bedmesh_panel.h"
#include "print_status_panel.h"

#include <mutex>

class InitPanel {
 public:
  InitPanel(MainPanel &mp, BedMeshPanel &bmp, std::mutex &l);
  ~InitPanel();

  void connected(KWebSocketClient &ws);
  void disconnected(KWebSocketClient &ws);
  void set_message(const char *message);

 private:
  lv_obj_t *cont;
  lv_obj_t *label;
  MainPanel &main_panel;
  BedMeshPanel &bedmesh_panel;
  std::mutex &lv_lock;
};

#endif // __INIT_PANEL_H__
