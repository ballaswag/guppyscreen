#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "stdbool.h"

#include "lvgl.h"


/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    LV_TC_START_DELAY_DISABLED = 0,
    LV_TC_START_DELAY_ENABLED = 1
} lv_tc_start_delay_t;

typedef struct {
    lv_obj_t screenObj;             /* The screen object */
    lv_obj_t *indicatorObj;         /* The crosshair image object */
    lv_obj_t *msgLabelObj;          /* The message label object */
    lv_obj_t *recalibrateBtnObj;    /* The recalibration button object */
    lv_obj_t *acceptBtnObj;         /* The confirmation button object */
    bool inputEnabled;              /* A flag which determines whether the screen accepts input from the touch driver.
                                       Makes sure pressing the crosshair is only handled once until releasing */
    lv_point_t scrPoints[3];        /* The points to be pressed (in the coordinate space of the screen) */
    lv_point_t tchPoints[3];        /* The touched points during the current calibration (in the coordinate space of the touch driver) */
    uint8_t currentStep;            /* The current calibration step */
    lv_timer_t *startDelayTimer;    /* The timer for delaying user input after opening the screen */
    lv_timer_t *recalibrateTimer;   /* The timer for automatic recalibration */
} lv_tc_screen_t;

extern const lv_obj_class_t lv_tc_screen_class;


/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a touch calibration screen.
 * @returns a pointer to the newly created screen (lv_obj_t*)
 */
lv_obj_t* lv_tc_screen_create();

/**
 * Set the points on screen to perform the calibration with.
 * @param screenObj a pointer to the calibration screen (lv_obj_t*)
 * @param scrPoints a pointer to the first element of an array holding the three new points
 */
void lv_tc_screen_set_points(lv_obj_t *screenObj, lv_point_t *scrPoints);

/**
 * Start the calibration process.
 * This function can also be used to restart an already ongoing calibration process.
 * @param screenObj a pointer to the calibration screen (lv_obj_t*)
 */
void lv_tc_screen_start(lv_obj_t *screenObj);

/**
 * Start the calibration process.
 * This function can also be used to restart an already ongoing calibration process.
 * @param screenObj a pointer to the calibration screen (lv_obj_t*)
 * @param startDelayEnabled set whether to enable a delay before starting the calibration
 */
void lv_tc_screen_start_with_config(lv_obj_t *screenObj, lv_tc_start_delay_t startDelayEnabled);




#ifdef __cplusplus
}
#endif