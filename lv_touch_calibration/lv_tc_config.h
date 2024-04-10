#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
    This is the configuration file for the touch calibration system.
    Change the defines below to match your application
*/


#if defined CONFIG_USE_CUSTOM_LV_TC_START_MSG
#define LV_TC_START_MSG CONFIG_LV_TC_START_MSG
#else
#define LV_TC_START_MSG                         "Precisely touch the cursors (using a touch pen) to perform the calibration."
#endif

#if defined CONFIG_USE_CUSTOM_LV_TC_READY_MSG
#define LV_TC_READY_MSG CONFIG_LV_TC_READY_MSG
#else
#define LV_TC_READY_MSG                         "Calibration completed. You can check it by moving the cursor around on the screen."
#endif

#if defined CONFIG_USE_CUSTOM_LV_TC_RECALIBRATE_TXT
#define LV_TC_RECALIBRATE_TXT CONFIG_LV_TC_RECALIBRATE_TXT
#else
#define LV_TC_RECALIBRATE_TXT                   "Recalibrate"
#endif

#if defined CONFIG_USE_CUSTOM_LV_TC_ACCEPT_TXT
#define LV_TC_ACCEPT_TXT CONFIG_LV_TC_ACCEPT_TXT
#else
#define LV_TC_ACCEPT_TXT                        "Accept"
#endif

//The format of the timeout string on the recalibration button.
//Appends to LV_TC_RECALIBRATE_TXT if LV_TC_RECALIB_TIMEOUT_S is set greater than 0
#define LV_TC_RECALIBRATE_TIMEOUT_FORMAT        " (%d)"


#if defined CONFIG_USE_CUSTOM_LV_TC_SCREEN_POINTS
#define LV_TC_SCREEN_POINT_1_X CONFIG_LV_TC_SCREEN_POINT_1_X
#define LV_TC_SCREEN_POINT_1_Y CONFIG_LV_TC_SCREEN_POINT_1_Y
#define LV_TC_SCREEN_POINT_2_X CONFIG_LV_TC_SCREEN_POINT_2_X
#define LV_TC_SCREEN_POINT_2_Y CONFIG_LV_TC_SCREEN_POINT_2_Y
#define LV_TC_SCREEN_POINT_3_X CONFIG_LV_TC_SCREEN_POINT_3_X
#define LV_TC_SCREEN_POINT_3_Y CONFIG_LV_TC_SCREEN_POINT_3_Y
#define LV_TC_SCREEN_ENABLE_AUTO_POINTS         0
#else
#define LV_TC_SCREEN_POINT_1_X 80
#define LV_TC_SCREEN_POINT_1_Y 150
#define LV_TC_SCREEN_POINT_2_X 192
#define LV_TC_SCREEN_POINT_2_Y 656
#define LV_TC_SCREEN_POINT_3_X 720
#define LV_TC_SCREEN_POINT_3_Y 80
//Set to 1 to make the system choose the points for the calibration automatically
//based on your screen resolution
#define LV_TC_SCREEN_ENABLE_AUTO_POINTS         1
#endif

//The default points (will be overridden if LV_TC_SCREEN_ENABLE_AUTO_POINTS is enabled)
#define LV_TC_SCREEN_DEFAULT_POINTS             {{LV_TC_SCREEN_POINT_1_X, LV_TC_SCREEN_POINT_1_Y}, {LV_TC_SCREEN_POINT_2_X, LV_TC_SCREEN_POINT_2_Y}, {LV_TC_SCREEN_POINT_3_X, LV_TC_SCREEN_POINT_3_Y}}


//Prevent user input immediately after the calibration is started by adding a delay (in milliseconds)
//When the process was started by pressing the screen, this makes sure that
//this press is not falsely registered as the first calibration point
//Set to 0 to disable
#if defined CONFIG_LV_TC_START_DELAY_MS
#define LV_TC_START_DELAY_MS CONFIG_LV_TC_START_DELAY_MS
#else
#define LV_TC_START_DELAY_MS                    1000
#endif

//Make the system restart the calibration automatically after a given timeout (in seconds)
//if it is not accepted by the user. This makes sure that a faulty calibration can
//always be restarted - even when it is impossible to press the 'recalibrate' button
//Set to 0 to disable
#if defined CONFIG_LV_TC_RECALIB_TIMEOUT_S
#define LV_TC_RECALIB_TIMEOUT_S CONFIG_LV_TC_RECALIB_TIMEOUT_S
#else
#define LV_TC_RECALIB_TIMEOUT_S                 30
#endif

#ifdef __cplusplus
}
#endif
