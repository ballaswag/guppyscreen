package com.guppy.guppyscreen.lvgl;

import android.content.Context;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.SurfaceView;

public class LVGLSurfaceView extends SurfaceView {

    public LVGLSurfaceView(Context context) {
        super(context);
        getHolder().addCallback(new LVGLHolderCallback(
                context.getCacheDir().getAbsolutePath(),
                context.getFilesDir().getAbsolutePath()
        ));
    }

    public LVGLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        getHolder().addCallback(new LVGLHolderCallback(
                context.getCacheDir().getAbsolutePath(),
                context.getFilesDir().getAbsolutePath()
        ));
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int x = (int) event.getX();
        int y = (int) event.getY();
        if (event.getAction() == MotionEvent.ACTION_UP)
            LVGLEntrance.nativeTouch(x, y, false);
        else
            LVGLEntrance.nativeTouch(x, y, true);
        return true;
    }
}
