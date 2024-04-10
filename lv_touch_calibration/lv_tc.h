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

typedef float lv_tc_val_t;

typedef struct {
    bool isValid;

    lv_tc_val_t a;
    lv_tc_val_t b;
    lv_tc_val_t c;
    lv_tc_val_t d;
    lv_tc_val_t e;
    lv_tc_val_t f;
} lv_tc_coeff_t;


/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize a calibrated touch input driver.
 * @param indevDrv pointer to an input driver
 * @param readCb function pointer to read input driver data
 */
void lv_tc_indev_drv_init(lv_indev_drv_t *indevDrv, void (*readCb)(struct _lv_indev_drv_t *indevDrv, lv_indev_data_t *data));

/**
 * Register a calibration screen to the modified input driver.
 * NOT TO BE CALLED IN APPLICATION CODE
 * @param screenObj pointer to the screen object
 * @param inputCb function pointer to handle the input
 */
void _lv_tc_register_input_cb(lv_obj_t *screenObj, bool (*inputCb)(lv_obj_t *screenObj, lv_indev_data_t *data));


/**
 * Register a calibration coefficients save callback.
 * It will be called on completion of the calibration steps.
 * @param saveCb function pointer to save the coefficients (a lv_tc_coeff_t structure)
 */
void lv_tc_register_coeff_save_cb(void (*saveCb)(lv_tc_coeff_t coeff));


/**
 * Get the current calibration coefficients.
 * @returns pointer to the coefficients structure (lv_tc_coeff_t*)
 */
lv_tc_coeff_t* lv_tc_get_coeff();

/**
 * Set the current calibration coefficients.
 * @param coeff the new coefficients structure (lv_tc_coeff_t)
 * @param save select whether to save the coefficients or just to keep them in volatile storage
 */
void lv_tc_set_coeff(lv_tc_coeff_t coeff, bool save);

/**
 * Load previously calibrated coefficient data if defined in the config
 */
#if defined CONFIG_USE_CUSTOM_LV_TC_COEFFICIENTS
void lv_tc_load_coeff_from_config();
#endif

/**
 * Save the current calibration coefficients.
 */
void lv_tc_save_coeff();

/**
 * Compute calibration coefficients for a given set of points on the screen and touch panel.
 * @param scrP pointer to the first element of an array containing the screen points
 * @param tchP pointer to the first element of an array containing the touch panel points
 * @param save select whether to save the coefficients or just to keep them in volatile storage
 */
void lv_tc_compute_coeff(lv_point_t *scrP, lv_point_t *tchP, bool save);


/**
 * Transform a point read by an input driver into a point on the screen.
 * NOT TO BE CALLED IN APPLICATION CODE
 * @param data pointer to data from the input driver (lv_indev_data_t*)
 * @returns the point (lv_point_t)
 */
lv_point_t _lv_tc_transform_point_indev(lv_indev_data_t *data);

/**
 * Transform a point on the touch panel into a point on the screen.
 * @param point the point on the touch panel (lv_point_t)
 * @returns the point on the screen (lv_point_t)
 */
lv_point_t lv_tc_transform_point(lv_point_t point);




#ifdef __cplusplus
}
#endif