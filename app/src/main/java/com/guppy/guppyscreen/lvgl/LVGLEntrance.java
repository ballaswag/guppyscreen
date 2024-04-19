package com.guppy.guppyscreen.lvgl;

import android.view.Surface;

public class LVGLEntrance {

    static {
        System.loadLibrary("guppyscreen");
    }

    public static native void nativeCreate(Surface surface, String cacheDir);

    public static native void nativeChanged(Surface surface, int width, int height, String cacheDir, String fileDir);

    public static native void nativeDestroy(Surface surface);

    public static native void nativeTouch(int x, int y, boolean touch);
}
