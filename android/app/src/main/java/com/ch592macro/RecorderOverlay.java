package com.ch592macro;

import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.util.List;

public final class RecorderOverlay {
    private final MacroService service;
    private final WindowManager windows;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Macro macro = new Macro();
    private LinearLayout root;
    private TextView label;
    private boolean attached, closed;
    private long lastFinished;
    private int width, height;
    public RecorderOverlay(MacroService service) {
        this.service = service; windows = service.getSystemService(WindowManager.class);
        macro.name = "录制宏 " + new java.text.SimpleDateFormat("HH:mm",java.util.Locale.ROOT).format(new java.util.Date());
        macro.orientation = service.getResources().getConfiguration().orientation;
        macro.repeats = 1;
    }
    public void show() {
        if (closed) return;
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics(); windows.getDefaultDisplay().getRealMetrics(metrics);
        width = metrics.widthPixels; height = metrics.heightPixels;
        root = new LinearLayout(service); root.setOrientation(LinearLayout.VERTICAL);
        label = new TextView(service); label.setTextColor(0xff164c40); label.setBackgroundColor(0xffe2f3eb);
        label.setPadding(20,20,20,20); label.setText("录制已开启 · 松手后由板卡执行\n目标应用保持可见，单指点按或滑动");
        root.addView(label);
        GesturePad pad = new GesturePad(service,this::gesture); pad.fullScreen(width,height);
        root.addView(pad,new LinearLayout.LayoutParams(-1,0,1));
        Button finish = new Button(service); finish.setText("结束录制并保存"); finish.setMinHeight((int)(56*metrics.density));
        finish.setOnClickListener(v -> {
            close(); service.startActivity(new Intent(service,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
        });
        root.addView(finish); attach();
    }
    private void attach() {
        if (closed || attached) return;
        if (service.getResources().getConfiguration().orientation != macro.orientation) { fail("屏幕方向已改变，已保存当前录制"); return; }
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(-1,-1,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        try { windows.addView(root,params); attached = true; }
        catch (Exception e) { fail("悬浮窗不可用，请在系统设置中允许显示在其他应用上层"); }
    }
    private void detach() { if (attached) { windows.removeView(root); attached = false; } }
    private void gesture(Macro.Step step) {
        if (closed) return;
        if (service.busy) { label.setText("正在读取板卡状态，请稍后重画这一步"); return; }
        int additions = 1;
        int gap = lastFinished == 0 ? 0 : (int)Math.min(3600000,SystemClock.elapsedRealtime()-lastFinished-step.duration);
        if (gap > 100) { macro.steps.add(Macro.Step.waitFor(gap)); additions++; }
        macro.steps.add(step);
        try { MacroCodec.compile(macro); }
        catch (Exception e) {
            for (int i=0;i<additions;i++) macro.steps.remove(macro.steps.size()-1);
            label.setText(e.getMessage() + "，可结束保存已有动作"); return;
        }
        Macro preview = new Macro(); preview.repeats = 1; preview.startDelay = 250;
        preview.orientation = macro.orientation; preview.steps.add(step);
        detach();
        service.play(preview,() -> {
            if (closed) return;
            lastFinished = SystemClock.elapsedRealtime();
            label.setText("已录制 " + macro.steps.size() + " 步 · 继续操作或结束保存"); attach();
        });
    }
    public void fail(String message) { Toast.makeText(service,message,Toast.LENGTH_LONG).show(); close(); }
    public void close() {
        if (closed) return; closed = true; main.removeCallbacksAndMessages(null); detach();
        if (!macro.steps.isEmpty()) {
            try { List<Macro> macros = MacroStore.load(service); macros.add(macro); MacroStore.save(service,macros); }
            catch (Exception e) { Toast.makeText(service,e.getMessage(),Toast.LENGTH_LONG).show(); }
        }
    }
}
