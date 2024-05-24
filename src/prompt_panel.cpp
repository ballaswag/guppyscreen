#include "prompt_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

// uncomment for helper boxes
// #define DEBUG_LINES

static lv_style_t style_btn_grey;
static lv_style_t style_btn_blue;
static lv_style_t style_btn_red;
static lv_style_t style_btn_orange;
static lv_style_t style_btn_dark_grey;
static lv_style_t button_group_flex_style;

PromptPanel::PromptPanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent)
    : NotifyConsumer(lock)
    , ws(websocket_client)
    , prompt_cont(lv_obj_create(lv_scr_act()))
    , flex(lv_obj_create(prompt_cont))
    , header(lv_label_create(prompt_cont))
    , footer_cont(lv_obj_create(prompt_cont))
//  , back_btn(promptpanel_cont, &back, "Back", &PromptPanel::_handle_callback, this)
{
    lv_obj_set_style_pad_all(prompt_cont, 0, 0);
    
    // lv_obj_clear_flag(promptpanel_cont, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t grid_main_row_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    // header, flex, buttons
    static lv_coord_t grid_main_col_dsc_detail[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST}; 
    // single column

    lv_obj_center(prompt_cont);
    lv_obj_set_style_pad_all(prompt_cont, 5, 0);
    lv_obj_set_style_radius(prompt_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(prompt_cont, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(prompt_cont, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_max_height(prompt_cont, lv_pct(80), 0);
    lv_obj_set_style_max_width(prompt_cont, lv_pct(75), 0);
    lv_obj_set_style_min_height(prompt_cont, lv_pct(50), 0);
    lv_obj_set_style_min_width(prompt_cont, lv_pct(60), 0);
    lv_obj_set_size(prompt_cont, lv_pct(60), lv_pct(50));
    lv_obj_set_grid_dsc_array(prompt_cont, grid_main_col_dsc_detail, grid_main_row_dsc_detail);

    lv_obj_set_style_pad_all(flex, 0, 0);

    lv_obj_set_grid_cell(header,                LV_GRID_ALIGN_START,    0, 1, LV_GRID_ALIGN_START,  0, 1);
    lv_obj_set_grid_cell(flex,                  LV_GRID_ALIGN_START,    0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_set_grid_cell(footer_cont,          LV_GRID_ALIGN_CENTER,   0, 1, LV_GRID_ALIGN_END,    2, 1);

    lv_obj_set_size(header, lv_pct(100), lv_pct(10));
    lv_obj_set_size(flex, lv_pct(100), lv_pct(60));
    lv_obj_set_size(footer_cont, lv_pct(100), lv_pct(15));

    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(flex, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(footer_cont, LV_OBJ_FLAG_SCROLLABLE);


    // set buttons horizontal
    //
    lv_style_init(&button_group_flex_style);
    lv_style_set_flex_flow(&button_group_flex_style, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_main_place(&button_group_flex_style, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&button_group_flex_style, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&button_group_flex_style, LV_FLEX_ALIGN_CENTER);
    lv_style_set_layout(&button_group_flex_style, LV_LAYOUT_FLEX);
    lv_style_set_pad_all(&button_group_flex_style, 0);
    lv_style_set_height(&button_group_flex_style, LV_SIZE_CONTENT);
    lv_style_set_width(&button_group_flex_style, lv_pct(100));
    lv_style_set_outline_pad(&button_group_flex_style, 0);
    lv_style_set_outline_width(&button_group_flex_style, 0);
    lv_obj_add_style(footer_cont, &button_group_flex_style, 0);

    static lv_style_t flex_style;
    lv_style_init(&flex_style);
    lv_style_set_flex_flow(&flex_style, LV_FLEX_FLOW_COLUMN_WRAP);
    lv_style_set_flex_main_place(&flex_style, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&flex_style, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&flex_style, LV_FLEX_ALIGN_CENTER);
    lv_style_set_layout(&flex_style, LV_LAYOUT_FLEX);
    lv_style_set_pad_all(&flex_style, 0);
    lv_style_set_outline_pad(&flex_style, 0);
    lv_style_set_outline_width(&flex_style, 0);
    lv_obj_add_style(flex, &flex_style, 0);

#ifdef DEBUG_LINES
    // for debugging
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(header, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(flex, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(flex, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(footer_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(footer_cont, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif

    ws.register_notify_update(this);
    ws.register_method_callback("notify_gcode_response", "MainPanel",[this](json& d) { this->handle_macro_response(d); });

    // create header
    lv_label_set_text(header, "HEADER");

    // button styles
    lv_style_init(&style_btn_grey);
    lv_style_set_bg_color(&style_btn_grey, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_btn_grey, LV_OPA_COVER);
    lv_style_set_pad_all(&style_btn_grey, 0);

    lv_style_init(&style_btn_blue);
    lv_style_set_bg_color(&style_btn_blue, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_opa(&style_btn_blue, LV_OPA_COVER);

    lv_style_init(&style_btn_red);
    lv_style_set_bg_color(&style_btn_red, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_bg_opa(&style_btn_red, LV_OPA_COVER);

    lv_style_init(&style_btn_orange);
    lv_style_set_bg_color(&style_btn_orange, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_bg_opa(&style_btn_orange, LV_OPA_COVER);

    lv_style_init(&style_btn_dark_grey);
    lv_style_set_bg_color(&style_btn_dark_grey, lv_palette_darken(LV_PALETTE_GREY, 1));
    lv_style_set_bg_opa(&style_btn_dark_grey, LV_OPA_COVER);
    
    background(); // hide ourselves
}


void PromptPanel::consume(json &j) {
}

PromptPanel::~PromptPanel() {
    if (prompt_cont != NULL) {
        lv_obj_del(prompt_cont);
        prompt_cont = NULL;
    }

    ws.unregister_notify_update(this);
}

void PromptPanel::foreground() {
    // shrink wrap
    lv_obj_move_foreground(prompt_cont);
}

void PromptPanel::background() {
  lv_obj_move_background(prompt_cont);
}

void PromptPanel::handle_callback(lv_event_t *event) {
    lv_obj_t *btn = lv_event_get_current_target(event);

    PromptPanel *panel = (PromptPanel*)event->user_data;

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_obj_t *command = lv_obj_get_child(btn, 1);
    // check if btn in command map
    spdlog::debug("handle event");

    if (btn == NULL) {
        spdlog::debug("no button found");
    }

    if (label != NULL) {
        spdlog::debug("button: {}", lv_label_get_text(label));
    }

    if (command != NULL) {
        std::string cmd = lv_label_get_text(command);
        spdlog::debug("button: {}", cmd);
        ws.gcode_script(cmd);
    }


}

void PromptPanel::check_height() {
    // check if we need to increase size of the parent container
    lv_obj_t *last_child = lv_obj_get_child(flex, -1);
    // iterate max 5 times to enlarge, could probably be done nicer but since it's 
    // based on css auto sizing is terrible.
    if (NULL != last_child) {
        int count = 0;
        int y = lv_obj_get_y(last_child);
        int height = lv_obj_get_height(last_child);
        while(((y + height > lv_obj_get_height(flex)) || height == 0) && count < 5) {
            spdlog::debug("y: {}, h: {}", y, height);
            if ((y + height > lv_obj_get_height(flex)) || height == 0) {
                int newheight = (int) (((double)lv_obj_get_height(prompt_cont)) * 1.1);
                int newwidth = (int) (((double)lv_obj_get_width(prompt_cont)) * 1.1);
                spdlog::debug("Increase size of panel: {}, {}", newheight, newwidth);
                lv_obj_set_size(prompt_cont, newheight, newwidth);
            }
            lv_obj_update_layout(prompt_cont);
            count++;
            y = lv_obj_get_y(last_child);
            height = lv_obj_get_height(last_child);
        }
    }
}

void PromptPanel::handle_macro_response(json &j) {
    spdlog::trace("macro response: {}", j.dump());
    auto &v = j["/params/0"_json_pointer];

    if (!v.is_null()) {
        spdlog::debug("data found");
        std::string resp = v.template get<std::string>();
        std::lock_guard<std::mutex> lock(lv_lock);
        spdlog::debug("data: {}", resp);

        if (resp.find("// action:", 0) == 0) {
            // it is an action
            std::string command = resp.substr(10);
            spdlog::debug("action: {}", command);

            
            if (command.find("prompt_begin") == 0) {
                std::string prompt_header = command.substr(13);
                spdlog::debug("PROMPT_BEGIN: {}", prompt_header);

                // remove buttons
                lv_obj_clean(footer_cont);
                lv_obj_clean(flex);
                // remove button commands

                lv_obj_set_size(prompt_cont, lv_pct(60), lv_pct(50));
                lv_obj_set_height(flex, lv_pct(70));

                // set header here
                lv_label_set_text(header, prompt_header.c_str());
            } else if (command.find("prompt_text") == 0) {
                std::string prompt_text = command.substr(12);
                spdlog::debug("PROMPT_TEXT: {}", prompt_text);
                // create label and add to flex field
                lv_obj_t *textfield = lv_label_create(flex);
                lv_obj_set_width(textfield, lv_pct(96));
                lv_obj_set_height(textfield, 40);
                // lv_obj_set_style_min_height(textfield, 32, 0);
                lv_label_set_long_mode(textfield, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(textfield, 1);
                lv_obj_set_style_outline_pad(textfield, 0, 0);
                lv_label_set_text(textfield, prompt_text.c_str());
                lv_obj_center(textfield);
#ifdef DEBUG_LINES
                lv_obj_set_style_border_width(textfield, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(textfield, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(textfield, lv_palette_lighten(LV_PALETTE_GREEN, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
            // due to using find, order IS important!  
            } else if (command.find("prompt_button_group_start") == 0) {
                spdlog::debug("Button group created");
                // create new button group in flex window and mark active
                button_group_cont = lv_obj_create(flex);
                lv_obj_add_style(button_group_cont, &button_group_flex_style, 0);
                lv_obj_set_flex_grow(button_group_cont, 1);
                lv_obj_set_width(button_group_cont, lv_pct(96));
                lv_obj_center(button_group_cont);
                lv_obj_set_style_pad_all(button_group_cont, 0, 0);
                lv_obj_set_style_outline_pad(button_group_cont, 0, 0);
                lv_obj_set_style_max_height(button_group_cont, lv_pct(62), 0);
                lv_obj_set_style_min_height(button_group_cont, 48, 0);
                lv_obj_set_height(button_group_cont, LV_SIZE_CONTENT);
                lv_obj_clear_flag(button_group_cont, LV_OBJ_FLAG_SCROLLABLE);

#ifdef DEBUG_LINES
                lv_obj_set_style_border_width(button_group_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(button_group_cont, lv_palette_main(LV_PALETTE_PINK), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(button_group_cont, lv_palette_lighten(LV_PALETTE_PINK, 2), LV_PART_MAIN | LV_STATE_DEFAULT);
#endif
                // lv_obj_set_style_min_height(button_group_cont, lv_pct(5), 0);
            } else if (command.find("prompt_button_group_end") == 0) {
                // does nothing since start creates a new one
                spdlog::debug("Button group ended");
                button_group_cont = NULL;
            } else if (command.find("prompt_footer_button") == 0 || command.find("prompt_button") == 0) {
                int index_label = command.find("button", 0) + strlen("button");
                int index_first = command.find("|", index_label);
                int index_second = command.find("|", index_first + 1);
                spdlog::debug("indexes: {} {} {}", index_label, index_first, index_second);
                std::string prompt_footer_button = command.substr(index_label, index_first - index_label);
                std::string prompt_button_command;
                std::string prompt_button_type = "none";
                spdlog::debug("button: {} |  {} | {}", prompt_footer_button, prompt_button_command, prompt_button_type);
                if (index_second > 0) {
                    prompt_button_command = command.substr(index_first + 1, index_second - index_first - 1);
                    prompt_button_type = command.substr(index_second + 1, command.length() - index_second - 1);
                } else {
                    prompt_button_command = command.substr(index_first + 1);
                }
                spdlog::debug("PROMPT_FOOTER_BUTTON: {} CMD: {}, type {}", prompt_footer_button, prompt_button_command, prompt_button_type);
                lv_obj_t *btn = NULL;
                if (command.find("prompt_footer_button") == 0) {
                    btn = lv_btn_create(footer_cont);
                } else {
                    if (button_group_cont == NULL) {
                        btn = lv_btn_create(flex);
                    } else {
                        btn = lv_btn_create(button_group_cont);
                    }
                }
                if (btn) {
                    lv_obj_set_size(btn, lv_pct(45), 32);
                    lv_obj_set_style_max_width(btn, lv_pct(45), 0);
                    lv_obj_set_style_min_width(btn, 32, 0);
                    lv_obj_set_style_max_height(btn, 54, 0);
                    lv_obj_set_style_min_height(btn, 42, 0);
                    lv_obj_set_style_outline_pad(btn, 0, 0);
                    lv_obj_center(btn);
                    lv_obj_set_flex_grow(btn, 1);
                    lv_obj_t *label = lv_label_create(btn);
                    // a hidden label is abused to transfer the command and auto-clean it
                    lv_obj_t *command = lv_label_create(btn);
                    lv_obj_set_size(command, 1, 1);
                    lv_obj_add_flag(command, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(label, prompt_footer_button.c_str());
                    lv_label_set_text(command, prompt_button_command.c_str());
                    lv_obj_set_style_pad_all(btn, 2, 0);
                    // lv_obj_set_style_max_width(label, lv_pct(45), 0);
                    lv_obj_center(label);

                    if (!prompt_button_type.compare("secondary")) {
                        spdlog::debug("type secondary");
                        lv_obj_add_style(btn, &style_btn_grey, 0);
                    } else if (!prompt_button_type.compare("warning")) {
                        spdlog::debug("type warning");
                        lv_obj_add_style(btn, &style_btn_orange, 0);
                    } else if (!prompt_button_type.compare("error")) {
                        spdlog::debug("type error");
                        lv_obj_add_style(btn, &style_btn_red, 0);
                    } else if (!prompt_button_type.compare("info")) {
                        spdlog::debug("type info");
                        lv_obj_add_style(btn, &style_btn_blue, 0);
                    } else if (!prompt_button_type.compare("primary")) {
                        spdlog::debug("type primary");
                        lv_obj_add_style(btn, &style_btn_blue, 0);
                    } else { // info and primary as well
                        spdlog::debug("type default");
                        lv_obj_add_style(btn, &style_btn_dark_grey, 0);
                    }
                    lv_obj_add_event_cb(btn, _handle_callback, LV_EVENT_PRESSED, this);

                }
            } else if (command.find("prompt_show") == 0) {
                spdlog::debug("PROMPT_SHOW");
                check_height();
                foreground();
            } else if (command.find("prompt_end") == 0) {
                spdlog::debug("PROMPT_END");
                background();

                // remove buttons
                lv_obj_clean(footer_cont);
                lv_obj_clean(flex);
                lv_obj_set_size(prompt_cont, lv_pct(60), lv_pct(50));
                lv_obj_set_height(flex, LV_SIZE_CONTENT);
            } else {
                spdlog::debug("action {} --- not supported", command);
            }
            
        }
        
    }
}
