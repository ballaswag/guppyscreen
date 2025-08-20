#ifndef __PROMPT_PANEL_H__
#define __PROMPT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"

#include <map>
#include <memory>
#include <mutex>

class PromptPanel : public NotifyConsumer {
    public:
        PromptPanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
        ~PromptPanel();
        void consume(json &j);
        void foreground();
        void background();

    private:
        static void on_button_click(lv_event_t *e);

        KWebSocketClient &ws;
        lv_obj_t *prompt_cont;
        lv_obj_t *header;
        lv_obj_t *body_cont;
        lv_obj_t *body;
        lv_obj_t *button_cont;
        lv_obj_t *footer_cont;
};

#endif // __PROMPT_PANEL_H__
