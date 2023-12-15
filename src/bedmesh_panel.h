#ifndef __BEDMESH_PANEL_H__
#define __BEDMESH_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <vector>

class BedMeshPanel : public NotifyConsumer {
 public:
  BedMeshPanel(KWebSocketClient &c, std::mutex &l);
  ~BedMeshPanel();

  void consume(json &j);
  void foreground();
  void refresh_views_with_lock(json &);
  void refresh_views(json &);
  void refresh_profile_info(std::string pname);
  void handle_callback(lv_event_t *event);
  void handle_profile_action(lv_event_t *event);
  void handle_prompt_save(lv_event_t *event);
  void handle_prompt_cancel(lv_event_t *event);
  void handle_kb_input(lv_event_t *e);
  void mesh_draw_cb(lv_event_t *e);

  static void _handle_callback(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_profile_action(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_profile_action(event);
  };
  
  static void _handle_prompt_save(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_prompt_save(event);
  };
  
  static void _handle_prompt_cancel(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_prompt_cancel(event);
  };

  static void _handle_kb_input(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _mesh_draw_cb(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->mesh_draw_cb(e);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *prompt;
  lv_obj_t *top_cont;
  lv_obj_t *mesh_table;
  lv_obj_t *profile_cont;
  lv_obj_t *profile_table;
  lv_obj_t *profile_info;
  lv_obj_t *controls_cont;
  ButtonContainer save_btn;
  ButtonContainer clear_btn;
  ButtonContainer calibrate_btn;
  ButtonContainer back_btn;
  lv_obj_t *msgbox;
  lv_obj_t *input;
  lv_obj_t *kb;
  std::string active_profile;
  std::vector<std::vector<double>> mesh;
};

#endif // __BEDMESH_PANEL_H__
