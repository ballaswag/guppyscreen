/*********************
 *      INCLUDES
 *********************/

#include "lv_tc_screen.h"

#include "math.h"

#include "lv_tc.h"
#include "lv_tc_config.h"


/*********************
 *      DEFINES
 *********************/

#define MY_CLASS &lv_tc_screen_class

#define STEP_INIT 0
#define STEP_FIRST 1
#define STEP_FINISH 4
#define INDICATOR_SIZE 50

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_tc_screen_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj);

static void lv_tc_screen_auto_set_points(lv_obj_t *screenObj);

static bool lv_tc_screen_input_cb(lv_obj_t *screenObj, lv_indev_data_t *data);
static void lv_tc_screen_process_input(lv_obj_t *screenObj, lv_point_t tchPoint);
static void lv_tc_screen_step(lv_obj_t *screenObj, uint8_t step, lv_point_t tchPoint);
static void lv_tc_screen_set_indicator_pos(lv_obj_t *screenObj, lv_point_t point, bool visible);

static void lv_tc_screen_finish(lv_obj_t *screenObj);
static void lv_tc_screen_ready(lv_obj_t *screenObj);

static void lv_tc_screen_recalibrate_btn_click_cb(lv_event_t *event);
static void lv_tc_screen_accept_btn_click_cb(lv_event_t *event);

static void lv_tc_screen_recalibrate_timer(lv_timer_t *timer);
static void lv_tc_screen_start_delay_timer(lv_timer_t *timer);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_tc_screen_class = {
    .constructor_cb = lv_tc_screen_constructor,
    .instance_size = sizeof(lv_tc_screen_t),
    .base_class = &lv_obj_class
};


/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t* lv_tc_screen_create() {
    lv_obj_t *obj = lv_obj_class_create_obj(MY_CLASS, NULL);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lv_tc_screen_set_points(lv_obj_t* screenObj, lv_point_t *scrPoints) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    memcpy(tCScreenObj->scrPoints, scrPoints, sizeof(lv_point_t) * 3);
}

void lv_tc_screen_start(lv_obj_t *screenObj) {
    lv_tc_screen_start_with_config(screenObj, LV_TC_START_DELAY_ENABLED);
}

void lv_tc_screen_start_with_config(lv_obj_t* screenObj, lv_tc_start_delay_t startDelayEnabled) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    if(tCScreenObj->recalibrateTimer) {
        lv_timer_del(tCScreenObj->recalibrateTimer);
        tCScreenObj->recalibrateTimer = NULL;
    }

    lv_label_set_text_static(tCScreenObj->msgLabelObj, LV_TC_START_MSG);
    lv_obj_add_flag(tCScreenObj->recalibrateBtnObj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tCScreenObj->acceptBtnObj, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_align(tCScreenObj->msgLabelObj, LV_ALIGN_CENTER, 0, -50);
    lv_obj_align_to(tCScreenObj->recalibrateBtnObj, tCScreenObj->msgLabelObj, LV_ALIGN_OUT_BOTTOM_MID, -140, 20);
    lv_obj_align_to(tCScreenObj->acceptBtnObj, tCScreenObj->msgLabelObj, LV_ALIGN_OUT_BOTTOM_MID, 140, 20);


    #if LV_TC_START_DELAY_MS
        lv_point_t point = {0, 0};
        lv_tc_screen_step(screenObj, STEP_INIT, point);
    #endif

    //Register this screen to the calibrated indev driver
    _lv_tc_register_input_cb(screenObj, lv_tc_screen_input_cb);

    //Start the input delay timer (or calibrate immediately)
    #if LV_TC_START_DELAY_MS
        tCScreenObj->startDelayTimer = lv_timer_create(lv_tc_screen_start_delay_timer, LV_TC_START_DELAY_MS, screenObj);
        lv_timer_set_repeat_count(tCScreenObj->startDelayTimer, 1);
    #else
        lv_point_t point = {0, 0};
        lv_tc_screen_step(screenObj, STEP_FIRST, point);
    #endif
}


/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_tc_screen_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
    LV_UNUSED(class_p);
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)obj;

    tCScreenObj->inputEnabled = true;
    tCScreenObj->startDelayTimer = NULL;
    tCScreenObj->recalibrateTimer = NULL;
    
    #if LV_TC_SCREEN_ENABLE_AUTO_POINTS
        lv_tc_screen_auto_set_points(obj);
    #else
        lv_point_t points[3] = LV_TC_SCREEN_DEFAULT_POINTS;
        lv_tc_screen_set_points(obj, points);
    #endif

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);


    LV_IMG_DECLARE(lv_tc_indicator_img);

    tCScreenObj->indicatorObj = lv_img_create(obj);
    lv_img_set_src(tCScreenObj->indicatorObj, &lv_tc_indicator_img);
    lv_obj_set_style_img_recolor_opa(tCScreenObj->indicatorObj, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(tCScreenObj->indicatorObj, lv_color_white(), 0);    
    lv_obj_clear_flag(tCScreenObj->indicatorObj, LV_OBJ_FLAG_CLICKABLE);


    tCScreenObj->msgLabelObj = lv_label_create(obj);
    lv_obj_set_style_text_align(tCScreenObj->msgLabelObj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(tCScreenObj->msgLabelObj, lv_pct(60), LV_SIZE_CONTENT);


    tCScreenObj->recalibrateBtnObj = lv_btn_create(obj);
    lv_obj_set_size(tCScreenObj->recalibrateBtnObj, lv_pct(35), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(tCScreenObj->recalibrateBtnObj, lv_tc_screen_recalibrate_btn_click_cb, LV_EVENT_CLICKED, obj);
    
    lv_obj_t *recalibrateBtnLabelObj = lv_label_create(tCScreenObj->recalibrateBtnObj);
    #if LV_TC_RECALIB_TIMEOUT_S == 0
        lv_label_set_text_static(recalibrateBtnLabelObj, LV_TC_RECALIBRATE_TXT);
    #endif
    lv_obj_center(recalibrateBtnLabelObj);


    tCScreenObj->acceptBtnObj = lv_btn_create(obj);
    lv_obj_set_size(tCScreenObj->acceptBtnObj, lv_pct(35), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(tCScreenObj->acceptBtnObj, lv_tc_screen_accept_btn_click_cb, LV_EVENT_CLICKED, obj);

    lv_obj_t *acceptBtnLabelObj = lv_label_create(tCScreenObj->acceptBtnObj);
    lv_label_set_text_static(acceptBtnLabelObj, LV_TC_ACCEPT_TXT);
    lv_obj_center(acceptBtnLabelObj);

    lv_obj_move_foreground(tCScreenObj->indicatorObj);
}

static void lv_tc_screen_auto_set_points(lv_obj_t *screenObj) {
    //Choose the on-screen calibration points based on the active display driver's resolution
    lv_coord_t marginH = lv_disp_get_hor_res(NULL) * 0.15;
    lv_coord_t marginV = lv_disp_get_ver_res(NULL) * 0.15;
    lv_coord_t margin = (marginH < marginV) ? marginH: marginV;

    lv_point_t points[3] = {
        {       margin                            ,        (float)lv_disp_get_ver_res(NULL) * 0.3   },
        {(float)lv_disp_get_hor_res(NULL) * 0.4   ,        lv_disp_get_ver_res(NULL) - margin},
        {       lv_disp_get_hor_res(NULL) - margin,        margin                                   }
    };
    lv_tc_screen_set_points(screenObj, points);
}

static bool lv_tc_screen_input_cb(lv_obj_t *screenObj, lv_indev_data_t *data) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    if(tCScreenObj->inputEnabled && data->state == LV_INDEV_STATE_PRESSED) {
        lv_tc_screen_process_input(screenObj, data->point);
    }

    if(data->state == LV_INDEV_STATE_RELEASED) {
        tCScreenObj->inputEnabled = true;
    }

    return tCScreenObj->currentStep >= STEP_FINISH;
}

static void lv_tc_screen_process_input(lv_obj_t* screenObj, lv_point_t tchPoint) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    if(tCScreenObj->currentStep > STEP_INIT) {
        if(tCScreenObj->currentStep < STEP_FINISH) {
            //Block further input until released
            tCScreenObj->inputEnabled = false;
            //Go to the next calibration step
            lv_tc_screen_step(screenObj, tCScreenObj->currentStep + 1, tchPoint);
        } else {
            //When the calibration is completed, show the cursor at touch position
            lv_tc_screen_set_indicator_pos(screenObj, lv_tc_transform_point(tchPoint), true);
        }
    }
}

static void lv_tc_screen_step(lv_obj_t* screenObj, uint8_t step, lv_point_t tchPoint) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    tCScreenObj->currentStep = step;

    if(step > STEP_FIRST) {
        //Store the touch controller output for the current point
        tCScreenObj->tchPoints[step - 2] = tchPoint;
    }
    if(step == STEP_FINISH) {
        //Finish the calibration
        lv_tc_compute_coeff(tCScreenObj->scrPoints, tCScreenObj->tchPoints, false);
        lv_tc_screen_finish(screenObj);
        return;
    }

    lv_tc_screen_set_indicator_pos(screenObj, tCScreenObj->scrPoints[step - 1], step > STEP_INIT);
}

static void lv_tc_screen_set_indicator_pos(lv_obj_t* screenObj, lv_point_t point, bool visible) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    lv_obj_set_pos(tCScreenObj->indicatorObj, point.x - INDICATOR_SIZE / 2, point.y - INDICATOR_SIZE / 2);
    
    if(visible) {
        lv_obj_clear_flag(tCScreenObj->indicatorObj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(tCScreenObj->indicatorObj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void lv_tc_screen_finish(lv_obj_t *screenObj) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    //Update the UI
    lv_label_set_text_static(tCScreenObj->msgLabelObj, LV_TC_READY_MSG);
    lv_obj_clear_flag(tCScreenObj->recalibrateBtnObj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(tCScreenObj->acceptBtnObj, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_align(tCScreenObj->msgLabelObj, LV_ALIGN_CENTER, 0, -50);
    lv_obj_align_to(tCScreenObj->recalibrateBtnObj, tCScreenObj->msgLabelObj, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_x(tCScreenObj->recalibrateBtnObj, lv_pct(12));
    lv_obj_align_to(tCScreenObj->acceptBtnObj, tCScreenObj->msgLabelObj, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_x(tCScreenObj->acceptBtnObj, lv_pct(53));


    //Start the recalibration timeout
    #if LV_TC_RECALIB_TIMEOUT_S
        lv_label_set_text_fmt(lv_obj_get_child(tCScreenObj->recalibrateBtnObj, 0), LV_TC_RECALIBRATE_TXT LV_TC_RECALIBRATE_TIMEOUT_FORMAT, (int)LV_TC_RECALIB_TIMEOUT_S);

        tCScreenObj->recalibrateTimer = lv_timer_create(lv_tc_screen_recalibrate_timer, 1000, screenObj);
        lv_timer_set_repeat_count(tCScreenObj->recalibrateTimer, LV_TC_RECALIB_TIMEOUT_S);
    #endif
}

static void lv_tc_screen_ready(lv_obj_t *screenObj) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)screenObj;

    //Store the calibration coefficients in NVM
    lv_tc_save_coeff();

    if(tCScreenObj->recalibrateTimer) {
        lv_timer_del(tCScreenObj->recalibrateTimer);
        tCScreenObj->recalibrateTimer = NULL;
    }

    //Indicate that the calibration is complete and the screen can be closed
    lv_event_send(screenObj, LV_EVENT_READY, lv_tc_get_coeff());
}


static void lv_tc_screen_recalibrate_btn_click_cb(lv_event_t *event) {
    lv_tc_screen_start(event->user_data);
}

static void lv_tc_screen_accept_btn_click_cb(lv_event_t *event) {
    lv_tc_screen_ready(event->user_data);
}


static void lv_tc_screen_recalibrate_timer(lv_timer_t *timer) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)timer->user_data;

    if(timer->repeat_count == 0) {
        //Restart when timed out
        lv_tc_screen_start(tCScreenObj);
        return;
    }
    lv_label_set_text_fmt(lv_obj_get_child(tCScreenObj->recalibrateBtnObj, 0), LV_TC_RECALIBRATE_TXT LV_TC_RECALIBRATE_TIMEOUT_FORMAT, (int)timer->repeat_count);
}

static void lv_tc_screen_start_delay_timer(lv_timer_t *timer) {
    lv_tc_screen_t *tCScreenObj = (lv_tc_screen_t*)timer->user_data;

    lv_point_t point = {0, 0};
    lv_tc_screen_step(tCScreenObj, STEP_FIRST, point);
}
