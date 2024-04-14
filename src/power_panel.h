#ifndef __POWER_PANEL_H__
#define __POWER_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"
#include "websocket_client.h"

#include <string>
#include <mutex>

class PowerPanel {
  public:
    PowerPanel(KWebSocketClient &ws, std::mutex &l);
    ~PowerPanel();

    void create_devices(json &j);
    
    void foreground();
    void handle_callback(lv_event_t *event);

    static void _handle_callback(lv_event_t *event) {
      PowerPanel *panel = (PowerPanel*)event->user_data;
      panel->handle_callback(event);
    };

  private:
    void create_device(json &j);
    void handle_device_callback(json &j);

    KWebSocketClient &ws;
    std::mutex &lv_lock;

    lv_obj_t *cont;
    ButtonContainer back_btn;

    std::map<std::string, lv_obj_t*> devices;
  
};

#endif //__POWER_PANEL_H__
