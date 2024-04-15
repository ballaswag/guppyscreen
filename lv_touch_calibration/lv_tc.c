/*********************
 *      INCLUDES
 *********************/

#include "lv_tc.h"

#include "math.h"

#ifdef ESP_PLATFORM
    #include "esp_log.h"
#endif

/**********************
 *      DEFINES
 *********************/
#define TAG "lv_tc"

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_tc_indev_drv_read_cb(lv_indev_drv_t *indevDrv, lv_indev_data_t *data);


/**********************
 *  STATIC VARIABLES
 **********************/

static lv_tc_coeff_t calibResult = {false, 0, 0, 0, 0, 0, 0};

static lv_obj_t *registeredTCScreen = NULL;
static bool (*registeredInputCb)(lv_obj_t *screenObj, lv_indev_data_t *data) = NULL;
static void (*registeredSaveCb)(lv_tc_coeff_t coeff) = NULL;


/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_tc_indev_drv_init(lv_indev_drv_t *indevDrv, void (*readCb)(lv_indev_drv_t *indevDrv, lv_indev_data_t *data)) {
    lv_indev_drv_init(indevDrv);
    indevDrv->type = LV_INDEV_TYPE_POINTER;
    indevDrv->read_cb = lv_tc_indev_drv_read_cb;
    indevDrv->user_data = readCb;
}

void _lv_tc_register_input_cb(lv_obj_t *screenObj, bool (*inputCb)(lv_obj_t *screenObj, lv_indev_data_t *data)) {
    registeredTCScreen = screenObj;
    registeredInputCb = inputCb;
}

void lv_tc_register_coeff_save_cb(void (*saveCb)(lv_tc_coeff_t coeff)) {
    registeredSaveCb = saveCb;
}

lv_tc_coeff_t* lv_tc_get_coeff() {
    return &calibResult;
}

void lv_tc_set_coeff(lv_tc_coeff_t coeff, bool save) {
    calibResult = coeff;

    if(save) {
        lv_tc_save_coeff();
    }
}

#if defined CONFIG_USE_CUSTOM_LV_TC_COEFFICIENTS
void lv_tc_load_coeff_from_config() {
    lv_tc_coeff_t coeff = {
            true,
            atoff(CONFIG_LV_TC_COEFFICIENT_A),
            atoff(CONFIG_LV_TC_COEFFICIENT_B),
            atoff(CONFIG_LV_TC_COEFFICIENT_C),
            atoff(CONFIG_LV_TC_COEFFICIENT_D),
            atoff(CONFIG_LV_TC_COEFFICIENT_E),
            atoff(CONFIG_LV_TC_COEFFICIENT_F)
    };
    lv_tc_set_coeff(coeff, false);
}
#endif

void lv_tc_save_coeff() {
    if(registeredSaveCb) {
        registeredSaveCb(calibResult);
    }
}

void lv_tc_compute_coeff(lv_point_t *scrP, lv_point_t *tchP, bool save) {   //The computation is explained here: https://www.maximintegrated.com/en/design/technical-documents/app-notes/5/5296.html
    const lv_tc_val_t divisor = (
          (lv_tc_val_t)tchP[0].x * ((lv_tc_val_t)tchP[2].y - (lv_tc_val_t)tchP[1].y)
        - (lv_tc_val_t)tchP[1].x * (lv_tc_val_t)tchP[2].y
        + (lv_tc_val_t)tchP[1].y * (lv_tc_val_t)tchP[2].x
        + (lv_tc_val_t)tchP[0].y * ((lv_tc_val_t)tchP[1].x - (lv_tc_val_t)tchP[2].x)
    );

    lv_tc_coeff_t result = {
        true,
        (
              (lv_tc_val_t)scrP[0].x * ((lv_tc_val_t)tchP[2].y - (lv_tc_val_t)tchP[1].y)
            - (lv_tc_val_t)scrP[1].x * (lv_tc_val_t)tchP[2].y
            + (lv_tc_val_t)scrP[2].x * (lv_tc_val_t)tchP[1].y
            + ((lv_tc_val_t)scrP[1].x - (lv_tc_val_t)scrP[2].x) * (lv_tc_val_t)tchP[0].y
        ) / divisor,
        - (
              (lv_tc_val_t)scrP[0].x * ((lv_tc_val_t)tchP[2].x - (lv_tc_val_t)tchP[1].x)
            - (lv_tc_val_t)scrP[1].x * (lv_tc_val_t)tchP[2].x
            + (lv_tc_val_t)scrP[2].x * (lv_tc_val_t)tchP[1].x
            + ((lv_tc_val_t)scrP[1].x - (lv_tc_val_t)scrP[2].x) * (lv_tc_val_t)tchP[0].x
        ) / divisor,
        (
              (lv_tc_val_t)scrP[0].x * ((lv_tc_val_t)tchP[1].y * (lv_tc_val_t)tchP[2].x - (lv_tc_val_t)tchP[1].x * (lv_tc_val_t)tchP[2].y)
            + (lv_tc_val_t)tchP[0].x * ((lv_tc_val_t)scrP[1].x * (lv_tc_val_t)tchP[2].y - (lv_tc_val_t)scrP[2].x * (lv_tc_val_t)tchP[1].y)
            + (lv_tc_val_t)tchP[0].y * ((lv_tc_val_t)scrP[2].x * (lv_tc_val_t)tchP[1].x - (lv_tc_val_t)scrP[1].x * (lv_tc_val_t)tchP[2].x)
        ) / divisor,
        (
              (lv_tc_val_t)scrP[0].y * ((lv_tc_val_t)tchP[2].y - (lv_tc_val_t)tchP[1].y)
            - (lv_tc_val_t)scrP[1].y * (lv_tc_val_t)tchP[2].y
            + (lv_tc_val_t)scrP[2].y * (lv_tc_val_t)tchP[1].y
            + ((lv_tc_val_t)scrP[1].y - (lv_tc_val_t)scrP[2].y) * (lv_tc_val_t)tchP[0].y
        ) / divisor,
        - (
              (lv_tc_val_t)scrP[0].y * ((lv_tc_val_t)tchP[2].x - (lv_tc_val_t)tchP[1].x)
            - (lv_tc_val_t)scrP[1].y * (lv_tc_val_t)tchP[2].x
            + (lv_tc_val_t)scrP[2].y * (lv_tc_val_t)tchP[1].x
            + ((lv_tc_val_t)scrP[1].y - (lv_tc_val_t)scrP[2].y) * (lv_tc_val_t)tchP[0].x
        ) / divisor,
        (
              (lv_tc_val_t)scrP[0].y * ((lv_tc_val_t)tchP[1].y * (lv_tc_val_t)tchP[2].x - (lv_tc_val_t)tchP[1].x * (lv_tc_val_t)tchP[2].y)
            + (lv_tc_val_t)tchP[0].x * ((lv_tc_val_t)scrP[1].y * (lv_tc_val_t)tchP[2].y - (lv_tc_val_t)scrP[2].y * (lv_tc_val_t)tchP[1].y)
            + (lv_tc_val_t)tchP[0].y * ((lv_tc_val_t)scrP[2].y * (lv_tc_val_t)tchP[1].x - (lv_tc_val_t)scrP[1].y * (lv_tc_val_t)tchP[2].x)
        ) / divisor
    };

    lv_tc_set_coeff(result, save);

    #ifdef ESP_PLATFORM
        ESP_LOGI(TAG, "touch calibration coefficients -> [a: %f, b: %f, c: %f, d: %f, e: %f, f: %f]", result.a, result.b,
                result.c, result.d, result.e, result.f);
    #endif
}

static lv_point_t last_pressed_point = {0,0};
static lv_indev_state_t last_state = 0;
lv_point_t _lv_tc_transform_point_indev(lv_indev_data_t *data) {
    if(data->state == LV_INDEV_STATE_PRESSED) {
        // Pressed - just return coordinates
        last_pressed_point = data->point;
        last_state = data->state;
        return lv_tc_transform_point(data->point);
    } else if(data->state == LV_INDEV_STATE_RELEASED && last_state == LV_INDEV_STATE_PRESSED) {
        // Released - ensure reporting of coordinates where the touch was last seen
        last_state = data->state;
        return lv_tc_transform_point(last_pressed_point);
    }
    // Invalid state
    lv_point_t point = {0, 0};
    return point;
}

lv_point_t lv_tc_transform_point(lv_point_t point) {
    lv_point_t transformedPoint = point;
    if (calibResult.isValid) {
        transformedPoint.x = roundf((lv_tc_val_t)point.x * calibResult.a + (lv_tc_val_t)point.y * calibResult.b + calibResult.c);
        transformedPoint.y = roundf((lv_tc_val_t)point.x * calibResult.d + (lv_tc_val_t)point.y * calibResult.e + calibResult.f);

        lv_disp_t *disp = lv_disp_get_default();
        if (disp->driver->rotated == LV_DISP_ROT_90 || disp->driver->rotated == LV_DISP_ROT_270) {
            lv_coord_t tmp = transformedPoint.y;
            transformedPoint.y = transformedPoint.x;
            transformedPoint.x = lv_disp_get_ver_res(NULL) - tmp - 1;
        }

        if (disp->driver->rotated == LV_DISP_ROT_180) {
            transformedPoint.y = lv_disp_get_ver_res(NULL) - transformedPoint.y;
            transformedPoint.x = lv_disp_get_hor_res(NULL) - transformedPoint.x;
        }
    }

    return transformedPoint;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_tc_indev_drv_read_cb(lv_indev_drv_t *indevDrv, lv_indev_data_t *data) {
    if(!indevDrv->user_data) return;

    //Call the actual indev read callback
    ((void (*)(lv_indev_drv_t*, lv_indev_data_t*))indevDrv->user_data)(indevDrv, data);

    //Pass the results to an ongoing calibration if there is one
    if(registeredTCScreen && registeredInputCb && registeredTCScreen == lv_scr_act()) {
        if(!registeredInputCb(registeredTCScreen, data)) {
            //Override state and point if the input has been handled by the registered calibration screen
            data->state = LV_INDEV_STATE_RELEASED;
            lv_point_t point = {0, 0};
            data->point = point;

            return;
        }
    }

    data->point = _lv_tc_transform_point_indev(data);
}
