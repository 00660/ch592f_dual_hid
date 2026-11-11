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
    public volatile String message = "请选择要使用的板卡";
    public volatile boolean busy, connected;
    public volatile byte[] state, macroState;
    public Listener listener;
    public volatile String boardName = "", boardAddress = "", connectionState = "未连接";
    public Macro selectedMacro = new Macro();
    public int selectedIndex;
    private Runnable previewFinished;
    private boolean previewSeen;
    private boolean polling;
    private volatile boolean stopping;
    private volatile int generation;
    private RecorderOverlay recorder;
    private FloatingToolbar toolbar;
    private boolean destroyed;

    @Override public void onCreate() {
        super.onCreate();
        SharedPreferences preferences=getSharedPreferences("device",MODE_PRIVATE);
        boardName=preferences.getString("name","");boardAddress=preferences.getString("address","");
        try { java.util.List<Macro> macros=MacroStore.load(this);selectedIndex=Math.min(getSharedPreferences("macros",0).getInt("selected",0),macros.size()-1);if(selectedIndex>=0)selectedMacro=macros.get(selectedIndex); }
        catch(Exception error){message=error.getMessage();}
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"板卡回放",NotificationManager.IMPORTANCE_LOW));
        link = new BleLink(() -> main.post(() -> {
            connected = false; state = macroState = null;
            connectionState = "已断开";
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
        if(destroyed)return;
        if(toolbar!=null)toolbar.refresh();
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
        connect(null,address);
    }
    @android.annotation.SuppressLint("MissingPermission")
    public void restoreConnection() {
        if(destroyed||connected||busy||stopping||running()||isRecording()||boardAddress.isEmpty())return;
        if(Build.VERSION.SDK_INT>=31 && checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)!=android.content.pm.PackageManager.PERMISSION_GRANTED){
            connectionState="需要授权";message="请允许附近设备权限后连接板卡";changed();return;
        }
        BluetoothManager manager=getSystemService(BluetoothManager.class);
        android.bluetooth.BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
        if(adapter==null||!adapter.isEnabled()){
            connectionState="蓝牙未开启";message="请开启本机蓝牙后连接板卡";changed();return;
        }
        connect(boardName,boardAddress);
    }
    @android.annotation.SuppressLint("MissingPermission")
    public void connect(String name, String address) {
        if(busy||running()||isRecording()){message="请先停止当前任务再切换板卡";changed();return;}
        connected=false;
        boardName=name==null||name.isEmpty()?"未命名板卡":name;boardAddress=address;connectionState="连接中";
        getSharedPreferences("device",MODE_PRIVATE).edit().putString("name",boardName).putString("address",address).apply();
        state=macroState=null;
        job("正在连接本机板卡…",() -> {
            try {
                android.bluetooth.BluetoothDevice device=getSystemService(BluetoothManager.class).getAdapter().getRemoteDevice(address);
                link.connect(this,device);
                String actualName=device.getName();
                if(actualName!=null&&!actualName.isEmpty()){
                    boardName=actualName;getSharedPreferences("device",MODE_PRIVATE).edit().putString("name",actualName).apply();
                }
                state = link.status(); macroState = link.macroStatus(); connected = true;connectionState="已连接";
            } catch(Exception error){link.close();state=macroState=null;connectionState="连接失败";throw error;}
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
        if(toolbar!=null&&!isRecording()&&!toolbar.preparePlayback(macro)){message="工具条与手势区域冲突，请调整";changed();if(finished!=null)finished.run();return;}
        final int request = generation;
        final byte[] start=MacroCodec.start(macro);
        previewFinished = finished; previewSeen = false;
        job("正在上传动作宏…",() -> {
            checkReady();
            if(start.length==13&&MacroCodec.u32(state,8)<0x10101)throw new IllegalStateException("每轮间隔或坐标漂移需要 1.1.1 或更新固件");
            macroState = link.macroStatus();
            if ((macroState[1] & 255) >= 3) throw new IllegalStateException("请先停止当前回放");
            for (byte[] packet : MacroCodec.upload(records)) {
                if (request != generation) throw new CancellationException();
                link.write(packet);
            }
            macroState = link.macroStatus();
            if (macroState[1] != 2 || macroState[2] != 0) throw new IllegalStateException("板卡未确认完整宏，请重新上传");
            if (request != generation) throw new CancellationException();
            link.write(start);
            previewSeen = true; macroState = link.macroStatus();
            message = macro.playbackLabel()+" · 板卡正在回放";
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
    private void finishPreview() { if(toolbar!=null&&!isRecording())toolbar.restore();Runnable action = previewFinished; previewFinished = null; if (action != null) action.run();changed(); }
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
                    } catch (Exception e) { connected = false; connectionState="已断开";message = "状态读取失败，请重新连接"; main.post(() -> { if (recorder != null) recorder.close(); finishPreview(); }); }
                    finally { main.post(() -> { polling = false; changed(); }); }
                });
            }
            main.postDelayed(this,isRecording()?80:1000);
        }
    };
    public void record() {
        if (!connected || busy || macroState == null || macroState[1] >= 3) { message = "请先连接板卡并停止回放"; changed(); return; }
        if (recorder != null) recorder.close();
        recorder = new RecorderOverlay(this);
        if(toolbar!=null)toolbar.beginRecording();
        recorder.show();
        if(recorder!=null)message="录制中，可连续操作，结束时点完成录制";
        main.removeCallbacks(poll);main.post(poll);changed();
    }
    public boolean running(){return macroState!=null&&macroState[1]>=3;}
    public boolean isRecording(){return recorder!=null;}
    public boolean recordingReady(){return recorder!=null&&recorder.ready();}
    public int recordedGestures(){return recorder==null?0:recorder.gestureCount();}
    public void selectMacro(Macro macro,int index){selectedMacro=macro;selectedIndex=index;getSharedPreferences("macros",0).edit().putInt("selected",index).apply();changed();}
    public void saveSelectedMacro(){
        try{java.util.List<Macro> macros=MacroStore.load(this);macros.set(selectedIndex,selectedMacro);MacroStore.save(this,macros);changed();}
        catch(Exception error){message=error.getMessage();changed();}
    }
    public boolean showToolbar(){if(toolbar==null)toolbar=new FloatingToolbar(this);return toolbar.show();}
    public void raiseToolbar(){if(toolbar!=null)toolbar.raise();}
    public void finishRecording(){if(recorder==null)return;if(busy||previewSeen||running())stopMacro();else recorder.close();changed();}
    public void recordingClosed(RecorderOverlay source,Macro macro){
        if(recorder!=source)return;recorder=null;
        if(toolbar!=null)toolbar.restore();
        if(!macro.steps.isEmpty()){
            try{java.util.List<Macro> macros=MacroStore.load(this);macros.add(macro);MacroStore.save(this,macros);selectMacro(macro,macros.size()-1);message="录制已保存，可直接播放或调整参数";}
            catch(Exception error){message=error.getMessage();}
        }
        changed();
    }
    public void closeToolbar(){stopMacro();if(toolbar!=null){toolbar.close();toolbar=null;}}
    @Override public void onDestroy() {
        destroyed=true;
        if (recorder != null) recorder.close();
        if(toolbar!=null){toolbar.close();toolbar=null;}
        main.removeCallbacksAndMessages(null); link.close(); worker.shutdownNow(); super.onDestroy();
    }
}
