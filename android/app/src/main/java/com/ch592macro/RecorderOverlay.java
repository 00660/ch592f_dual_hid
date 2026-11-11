package com.ch592macro;

import android.graphics.PixelFormat;
import android.os.SystemClock;
import android.view.*;
import android.widget.Toast;

public final class RecorderOverlay {
    private final MacroService service;
    private final WindowManager windows;
    private final Macro macro = new Macro();
    private GesturePad pad;
    private WindowManager.LayoutParams layout;
    private boolean attached, closed, capturing;
    private int gestures;
    private long lastFinished;
    public RecorderOverlay(MacroService service) {
        this.service = service; windows = service.getSystemService(WindowManager.class);
        macro.name = "动作宏 " + new java.text.SimpleDateFormat("HH:mm",java.util.Locale.ROOT).format(new java.util.Date());
        macro.orientation = service.getResources().getConfiguration().orientation;
        Macro selected=service.selectedMacro;
        macro.repeats=selected.repeats;macro.startDelay=selected.startDelay;macro.loopDelay=selected.loopDelay;
        macro.jitterX=selected.jitterX;macro.jitterY=selected.jitterY;
    }
    public void show() {
        if (closed) return;
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics(); windows.getDefaultDisplay().getRealMetrics(metrics);
        macro.screenWidth=metrics.widthPixels;macro.screenHeight=metrics.heightPixels;
        pad=new GesturePad(service,this::gesture);pad.fullScreen(metrics.widthPixels,metrics.heightPixels);attach();
    }
    private void attach() {
        if (closed || attached) return;
        if (service.getResources().getConfiguration().orientation != macro.orientation) { fail("屏幕方向已改变，已保存当前录制"); return; }
        layout = new WindowManager.LayoutParams(-1,-1,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,PixelFormat.TRANSLUCENT);
        layout.gravity = Gravity.TOP | Gravity.START;
        try { windows.addView(pad,layout); attached = capturing = true; service.raiseToolbar(); }
        catch (Exception e) { fail("悬浮窗不可用，请在系统设置中允许显示在其他应用上层"); }
    }
    private void detach() { capturing = false; if (attached) { windows.removeView(pad); attached = false; } }
    private void capture(boolean enabled) {
        if(closed||!attached)return;
        if(service.getResources().getConfiguration().orientation!=macro.orientation){fail("屏幕方向已改变，已保存当前录制");return;}
        capturing=enabled;pad.clearGesture();
        // A transparent, non-touchable overlay lets the board's HID input reach the target app.
        layout.alpha=enabled?1f:0f;
        if(enabled)layout.flags&=~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        else layout.flags|=WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        windows.updateViewLayout(pad,layout);
    }
    public boolean ready(){return capturing&&!closed;}
    public int gestureCount(){return gestures;}
    private void gesture(Macro.Step step) {
        if (closed) return;
        if (service.busy) { Toast.makeText(service,"上一动作尚未完成，请稍后录入",Toast.LENGTH_SHORT).show(); return; }
        int additions = 1;
        int gap = lastFinished == 0 ? 0 : (int)Math.max(0,Math.min(3600000,SystemClock.elapsedRealtime()-lastFinished-step.duration));
        if (gap > 100) { macro.steps.add(Macro.Step.waitFor(gap)); additions++; }
        macro.steps.add(step);
        try { MacroCodec.compile(macro); }
        catch (Exception e) {
            for (int i=0;i<additions;i++) macro.steps.remove(macro.steps.size()-1);
            Toast.makeText(service,e.getMessage()+"，可完成录制保存已有动作",Toast.LENGTH_LONG).show(); return;
        }
        gestures++;
        Macro preview = new Macro(); preview.repeats = 1; preview.startDelay = 0;
        preview.orientation = macro.orientation; preview.steps.add(step);
        capture(false);
        service.play(preview,() -> {
            if (closed) return;
            lastFinished = SystemClock.elapsedRealtime();
            service.message="已录制 " + gestures + " 步 · 继续操作或完成录制"; capture(true);
        });
    }
    public void fail(String message) { Toast.makeText(service,message,Toast.LENGTH_LONG).show(); close(); }
    public void close() {
        if (closed) return; closed = true; detach();service.recordingClosed(this,macro);
    }
}
