package com.ch592macro;

import android.content.Context;
import android.graphics.*;
import android.view.*;
import java.util.ArrayList;
import java.util.List;

public final class GesturePad extends View {
    public interface Recorded { void accept(Macro.Step step); }
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final List<Macro.Point> path = new ArrayList<>();
    private final Recorded recorded;
    private long started;
    private boolean active;
    private boolean fullScreen;
    private int screenWidth, screenHeight;
    public GesturePad(Context context, Recorded recorded) {
        super(context); this.recorded = recorded;
        setContentDescription("手势录制区域，在此点按或滑动"); setFocusable(true);
    }
    public void fullScreen(int width, int height) { fullScreen = true; screenWidth = width; screenHeight = height; }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!fullScreen) {
            canvas.drawColor(Color.rgb(235,243,239));
            paint.setColor(Color.rgb(204,221,214)); paint.setStrokeWidth(1);
            for (int i = 1; i < 4; i++) { canvas.drawLine(getWidth()*i/4f,0,getWidth()*i/4f,getHeight(),paint); canvas.drawLine(0,getHeight()*i/4f,getWidth(),getHeight()*i/4f,paint); }
            if (path.isEmpty()) {
                paint.setColor(Color.rgb(71,85,79)); paint.setTextSize(15 * getResources().getDisplayMetrics().scaledDensity); paint.setTextAlign(Paint.Align.CENTER);
                canvas.drawText("在这里画出手势",getWidth()/2f,getHeight()/2f,paint);
            }
        }
        paint.setColor(Color.rgb(17,124,105)); paint.setStrokeWidth(5 * getResources().getDisplayMetrics().density);
        int[] location = new int[2]; getLocationOnScreen(location);
        float width = fullScreen ? screenWidth : getWidth(), height = fullScreen ? screenHeight : getHeight();
        float dx = fullScreen ? location[0] : 0, dy = fullScreen ? location[1] : 0;
        for (int i = 1; i < path.size(); i++) {
            Macro.Point a = path.get(i-1), b = path.get(i);
            canvas.drawLine(a.x*width-dx,a.y*height-dy,b.x*width-dx,b.y*height-dy,paint);
        }
        if (!path.isEmpty()) { Macro.Point p = path.get(path.size()-1); canvas.drawCircle(p.x*width-dx,p.y*height-dy,8 * getResources().getDisplayMetrics().density,paint); }
    }
    @Override public boolean onTouchEvent(android.view.MotionEvent event) {
        if (event.getPointerCount() > 1 || event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
            active = false; path.clear(); getParent().requestDisallowInterceptTouchEvent(false); invalidate(); return true;
        }
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            started = event.getEventTime(); path.clear(); active = true; getParent().requestDisallowInterceptTouchEvent(true);
        }
        if (!active) return true;
        float x = fullScreen ? event.getRawX()/screenWidth : event.getX()/Math.max(1,getWidth());
        float y = fullScreen ? event.getRawY()/screenHeight : event.getY()/Math.max(1,getHeight());
        int time = (int)Math.min(10000,event.getEventTime()-started);
        if (path.isEmpty() || action == MotionEvent.ACTION_UP || time - path.get(path.size()-1).time >= 15) {
            path.add(new Macro.Point(Math.max(0,Math.min(1,x)),Math.max(0,Math.min(1,y)),time));
        }
        invalidate();
        if (action == MotionEvent.ACTION_UP) {
            active = false; getParent().requestDisallowInterceptTouchEvent(false);
            Macro.Step step = new Macro.Step(); step.duration = Math.max(40,time);
            step.points.addAll(path);
            if (step.points.size() < 2 || time < 40) {
                Macro.Point end = path.get(path.size()-1); step.points.add(new Macro.Point(end.x,end.y,step.duration));
            }
            performClick(); recorded.accept(step);
        }
        return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }
}
