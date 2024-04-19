package com.guppy.guppyscreen.lvgl;

import android.util.Log;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;

public class LVGLHolderCallback implements SurfaceHolder.Callback {

    private static final String TAG = "LVGLHolderCallback";
    private final String cacheDir;
    private final String fileDir;

    public LVGLHolderCallback(final String cacheDir, final String fileDir) {
        this.cacheDir = cacheDir;
        this.fileDir = fileDir;
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder surfaceHolder) {
        LVGLEntrance.nativeCreate(surfaceHolder.getSurface(), cacheDir);
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder surfaceHolder, int format, int width, int height) {
        Log.e(TAG, "surfaceChanged: width=" + width + ";height=" + height + ";format=" + format);
        LVGLEntrance.nativeChanged(surfaceHolder.getSurface(), width, height, cacheDir, fileDir);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder surfaceHolder) {
        LVGLEntrance.nativeDestroy(surfaceHolder.getSurface());
    }
}
