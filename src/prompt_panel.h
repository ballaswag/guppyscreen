#ifndef __PROMPT_PANEL_H__
#define __PROMPT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"

#include <map>
#include <memory>
#include <mutex>

struct SharedButton {
    SharedButton(lv_obj_t *bbtn) : btn(bbtn) {};
    lv_obj_t *btn;
};

class PromptPanel : public NotifyConsumer {
    public:
        PromptPanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
        ~PromptPanel();

        void handle_macro_response(json &j);
        void consume(json &j);

        lv_obj_t *get_container();
        void handle_callback(lv_event_t *event);

        static void _handle_callback(lv_event_t *event) {
            PromptPanel *panel = (PromptPanel*)event->user_data;
            panel->handle_callback(event);
        };

        void foreground();
        void background();

    private:

        void check_height();

        KWebSocketClient &ws;
        lv_obj_t *promptpanel_cont;
        lv_obj_t *prompt_cont;
        lv_obj_t *flex;
        lv_obj_t *header;
        lv_obj_t *button_group_cont;
        lv_obj_t *footer_cont;

};

#endif // __PROMPT_PANEL_H__
