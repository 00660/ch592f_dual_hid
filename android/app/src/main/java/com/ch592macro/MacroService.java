package com.ch592macro;

import android.app.*;
import android.bluetooth.BluetoothManager;
import android.content.*;
import android.os.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.CancellationException;

public final class MacroService extends Service {
    public static final String STOP = "com.ch592macro.STOP";
    private static final String CHANNEL = "board";
    public interface Listener { void changed(); }
    public final class LocalBinder extends Binder { public MacroService service() { return MacroService.this; } }
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private BleLink link;
    public volatile String message = "先在系统蓝牙设置中配对 CH592F-3IN1";
    public volatile boolean busy, connected;
    public volatile byte[] state, macroState;
    public Listener listener;
    private Runnable previewFinished;
    private boolean previewSeen;
    private boolean polling;
    private volatile boolean stopping;
    private volatile int generation;
    private RecorderOverlay recorder;

    @Override public void onCreate() {
        super.onCreate();
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"板卡回放",NotificationManager.IMPORTANCE_LOW));
        link = new BleLink(() -> main.post(() -> {
            connected = false; state = macroState = null;
            message = "蓝牙已断开，请重新连接检查板卡状态";
            if (recorder != null) recorder.close();
            finishPreview(); changed();
        }));
        main.postDelayed(poll,1000);
    }
    @Override public IBinder onBind(Intent intent) { return new LocalBinder(); }
    @Override public int onStartCommand(Intent intent, int flags, int id) {
        startForeground(1, notification());
        if (intent != null && STOP.equals(intent.getAction())) stopMacro();
        return START_NOT_STICKY;
    }
    private Notification notification() {
        PendingIntent open = PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop = PendingIntent.getService(this,1,new Intent(this,MacroService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this,CHANNEL).setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle("HID 宏工作台").setContentText(message).setContentIntent(open).setOngoing(true)
                .addAction(new Notification.Action.Builder(null,"停止回放",stop).build()).build();
    }
    private void changed() {
        if (listener != null) listener.changed();
        if (connected) getSystemService(NotificationManager.class).notify(1,notification());
    }
    private interface Job { void run() throws Exception; }
    private boolean job(String text, Job task) {
        if (busy || stopping) { message = "正在处理上一请求，请稍后重试"; changed(); return false; }
        busy = true; message = text; changed();
        worker.execute(() -> {
            try { task.run(); }
            catch (CancellationException ignored) { }
            catch (Exception error) {
                Throwable cause = error; while (cause.getCause() != null) cause = cause.getCause();
                message = cause.getMessage() == null ? "蓝牙操作超时，请重新连接" : cause.getMessage();
                main.post(() -> { if (recorder != null) recorder.fail(message); finishPreview(); });
            } finally { busy = stopping; main.post(this::changed); }
        });
        return true;
    }
    public void connect(String address) {
        job("正在连接本机板卡…",() -> {
            connected = false;
            link.connect(this,getSystemService(BluetoothManager.class).getAdapter().getRemoteDevice(address));
            state = link.status(); macroState = link.macroStatus(); connected = true;
            message = (state[4] & 4) != 0 ? "本机触摸已就绪" : "已连接，等待系统启用 HID 触摸";
        });
    }
    private void checkReady() throws Exception {
        state = link.status();
        if (state[1] != 1 || state[2] != 1 || (state[4] & 4) == 0 || state[17] != 0)
            throw new IllegalStateException("需要本机连接 Android 触摸固件，并在系统中启用输入设备");
        if (state[16] != 0 && state[16] != 5) throw new IllegalStateException("板卡正在升级，请等待完成");
    }
    public void play(Macro macro, Runnable finished) {
        if (busy || stopping) { message = "正在处理上一请求，请稍后重试"; changed(); if (finished != null) finished.run(); return; }
        final byte[] records;
        try { records = MacroCodec.compile(macro); }
        catch (Exception e) { message = e.getMessage(); changed(); if (finished != null) finished.run(); return; }
        if (getResources().getConfiguration().orientation != macro.orientation) {
            message = "屏幕方向与录制时不同，请旋转后播放"; changed(); if (finished != null) finished.run(); return;
        }
        final int request = generation, repeats = macro.repeats, delay = macro.startDelay;
        previewFinished = finished; previewSeen = false;
        job("正在上传动作宏…",() -> {
            checkReady();
            macroState = link.macroStatus();
            if ((macroState[1] & 255) >= 3) throw new IllegalStateException("请先停止当前回放");
            for (byte[] packet : MacroCodec.upload(records)) {
                if (request != generation) throw new CancellationException();
                link.write(packet);
            }
            macroState = link.macroStatus();
            if (macroState[1] != 2 || macroState[2] != 0) throw new IllegalStateException("板卡未确认完整宏，请重新上传");
            if (request != generation) throw new CancellationException();
            link.write(MacroCodec.start(repeats,delay));
            previewSeen = true; macroState = link.macroStatus();
            message = "板卡正在回放，可切换到目标应用";
        });
    }
    public void stopMacro() {
        ++generation; stopping = true; busy = true;
        if (recorder != null) recorder.close();
        message = "正在停止并释放输入…"; changed();
        // Cancel the upload between ATT transactions; release any report already sent.
        worker.execute(() -> {
            try { if (link.ready()) { link.write(new byte[]{0x14}); link.write(new byte[]{4}); macroState = link.macroStatus(); } message = "已停止并释放输入"; }
            catch (Exception e) { message = "停止请求失败：请断开板卡电源或本机蓝牙连接"; }
            main.post(() -> { stopping = false; busy = false; previewSeen = false; finishPreview(); changed(); });
        });
    }
    public void pauseResume() {
        if (!connected || macroState == null) return;
        final boolean resume = macroState[1] == 4;
        job(resume ? "继续回放…" : "当前手势完成后暂停…",() -> {
            link.write(new byte[]{(byte)(resume ? 0x16 : 0x15)}); macroState = link.macroStatus();
            message = resume ? "板卡正在回放" : "已请求暂停";
        });
    }
    private void finishPreview() { Runnable action = previewFinished; previewFinished = null; if (action != null) action.run(); }
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (connected && !busy && !polling) {
                polling = true;
                worker.execute(() -> {
                    try {
                        state = link.status(); macroState = link.macroStatus();
                        if (macroState[1] == 2 && state[5] == 0 && previewSeen) {
                            previewSeen = false; message = macroState[2] == 0 ? "回放完成" : "回放已中止，错误码 " + macroState[2]; main.post(MacroService.this::finishPreview);
                        }
                    } catch (Exception e) { connected = false; message = "状态读取失败，请重新连接"; main.post(() -> { if (recorder != null) recorder.close(); finishPreview(); }); }
                    finally { main.post(() -> { polling = false; changed(); }); }
                });
            }
            main.postDelayed(this,1000);
        }
    };
    public void record() {
        if (!connected || busy || macroState == null || macroState[1] >= 3) { message = "请先连接板卡并停止回放"; changed(); return; }
        if (recorder != null) recorder.close();
        recorder = new RecorderOverlay(this); recorder.show();
    }
    @Override public void onDestroy() {
        if (recorder != null) recorder.close();
        main.removeCallbacksAndMessages(null); link.close(); worker.shutdownNow(); super.onDestroy();
    }
}
