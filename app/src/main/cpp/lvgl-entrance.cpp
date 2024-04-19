#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <android/native_window.h>

#include "lvgl.h"
#include "lvgl/lvgl.h"
#include "config.h"
#include "guppyscreen.h"

#ifdef __cplusplus
extern "C" {
#endif

struct TouchState {
    int32_t x;
    int32_t y;
    bool is_touched;
};
static TouchState state;

static int WIDTH = 0;
static int HEIGHT = 0;

#define DISP_BUF_SIZE 1024 * 1024

static ANativeWindow *window;
static lv_color_t lv_buf_1[DISP_BUF_SIZE];
static lv_color_t lv_buf_2[DISP_BUF_SIZE];
static pthread_t thread;
static bool run = false;

static lv_disp_draw_buf_t lv_disp_buf;

static void window_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static void lv_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
static void hal_init();

static void hal_init() {
    /*Initialize a descriptor for the buffer*/
    lv_disp_draw_buf_init(&lv_disp_buf, lv_buf_1, lv_buf_2, DISP_BUF_SIZE);

    /*Initialize and register a display driver*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &lv_disp_buf;
    disp_drv.flush_cb   = window_flush;

    disp_drv.hor_res    = WIDTH;
    disp_drv.ver_res    = HEIGHT;

    spdlog::debug("resolution {} x {}", WIDTH, HEIGHT);
    lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
    lv_theme_t * th = HEIGHT <= 480
                      ? lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_12)
                      : lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, &lv_font_montserrat_16);
    lv_disp_set_theme(disp, th);

    static lv_indev_drv_t indev_drv_1;
    indev_drv_1.read_cb = lv_touch_read;
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv_1);
}

static void clearScreen() {
    ANativeWindow_Buffer buffer;
    ANativeWindow_lock(window, &buffer, 0);
    memset(buffer.bits, 0xff, buffer.stride * buffer.height * 4);
    ANativeWindow_unlockAndPost(window);
}

static void copy_px(uint8_t *data, lv_color_t *color_p, int w) {
    for (int i = 0; i < w; i++) {
        data[0] = color_p->ch.red;
        data[1] = color_p->ch.green;
        data[2] = color_p->ch.blue;
        data[3] = color_p->ch.alpha;
        color_p++;
        data += 4;
    }
}

static uint32_t *buf;

static void window_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    int left = area->x1;
    if (left < 0)
        left = 0;
    int right = area->x2 + 1;
    if (right > WIDTH)
        right = WIDTH;
    int top = area->y1;
    if (top < 0)
        top = 0;
    int bottom = area->y2 + 1;
    if (bottom > HEIGHT)
        bottom = HEIGHT;
    int32_t y;

    ANativeWindow_Buffer buffer;
    ANativeWindow_lock(window, &buffer, 0);
    uint32_t *data = (uint32_t *) buffer.bits;
    uint32_t *dest = buf + top * WIDTH + left;
    int w = right - left;
    for (y = top; y < bottom; y++) {
        copy_px((uint8_t *) dest, color_p, w);
        dest += WIDTH;
        color_p += w;
    }
    uint32_t *src = buf;
    for (int i = 0; i < buffer.height; i++) {
        memcpy(data, src, WIDTH * 4);
        src += WIDTH;
        data += buffer.stride;
    }
    ANativeWindow_unlockAndPost(window);
    lv_disp_flush_ready(disp_drv);
}

static void *refresh_task(void *data) {
    GuppyScreen::loop();
    return 0;
}

static void lv_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    if (state.is_touched) {
        data->point.x = state.x;
        data->point.y = state.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void guppyscreen_init(std::string cache_dir, std::string file_dir)
{
    Config *conf = Config::get_instance();
    conf->init(file_dir + "/guppyconfig.json", cache_dir);
    GuppyScreen::init(hal_init);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_guppy_guppyscreen_lvgl_LVGLEntrance_nativeCreate(JNIEnv *env, jclass clazz,
                                                          jobject surface, jstring cacheDir) {
        window = ANativeWindow_fromSurface(env, surface);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_guppy_guppyscreen_lvgl_LVGLEntrance_nativeChanged(JNIEnv *env, jclass clazz,
                                                           jobject surface, jint width,
                                                           jint height,
                                                           jstring cacheDir,
                                                           jstring fileDir) {
    if (run) {
        return;
    }

    WIDTH = width;
    HEIGHT = height;
    buf = new uint32_t[WIDTH * HEIGHT];

    ANativeWindow_setBuffersGeometry(window, WIDTH, HEIGHT, WINDOW_FORMAT_RGBA_8888);

    clearScreen();

    std::string cache_dir(env->GetStringUTFChars(cacheDir, NULL));
    std::string file_dir(env->GetStringUTFChars(fileDir, NULL));
    guppyscreen_init(cache_dir, file_dir);

    run = true;
    pthread_create(&thread, 0, refresh_task, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_guppy_guppyscreen_lvgl_LVGLEntrance_nativeDestroy(JNIEnv *env, jclass clazz,
                                                           jobject surface) {
    run = false;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_guppy_guppyscreen_lvgl_LVGLEntrance_nativeTouch(JNIEnv *env, jclass clazz, jint x,
                                                         jint y, jboolean touch) {
    state.x = x;
    state.y = y;
    state.is_touched = touch;
}

#ifdef __cplusplus
} /*extern "C"*/
#endif
