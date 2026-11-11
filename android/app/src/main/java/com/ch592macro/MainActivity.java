package com.ch592macro;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.*;
import android.bluetooth.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import java.util.*;

public final class MainActivity extends Activity {
    private static final int INK=0xff203a32, MUTED=0xff51675f, GREEN=0xff117c69, BG=0xfff3f6f4;
    private final Handler main = new Handler(Looper.getMainLooper());
    private MacroService service;
    private List<Macro> macros = new ArrayList<>();
    private int selected;
    private LinearLayout content, steps;
    private TextView connection, progress, capacity;
    private Button play, pause, stop, record;
    private Spinner library;
    private boolean loading;
    private boolean bound;
    private final ServiceConnection binding = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder binder) { service=((MacroService.LocalBinder)binder).service(); service.listener=MainActivity.this::refreshStatus; refreshStatus(); }
        @Override public void onServiceDisconnected(ComponentName name) { service=null; refreshStatus(); }
    };
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (!reload()) return;
        draw(); bound = bindService(new Intent(this,MacroService.class),binding,Context.BIND_AUTO_CREATE);
    }
    private boolean reload() {
        try { macros=MacroStore.load(this); if(macros.isEmpty()) macros.add(new Macro()); selected=Math.min(selected,macros.size()-1); return true; }
        catch(Exception e) {
            new AlertDialog.Builder(this).setTitle("无法读取动作宏").setMessage(e.getMessage()+"\n原有数据已保留，请检查应用数据后重试。")
                    .setCancelable(false).setPositiveButton("关闭",(d,w)->finish()).show();
            return false;
        }
    }
    @Override protected void onResume() { super.onResume(); if(content!=null && reload()) { refreshLibrary(); refreshSteps(); } }
    @Override protected void onDestroy() {
        main.removeCallbacksAndMessages(null);
        if(service!=null) service.listener=null;
        if (bound) unbindService(binding); super.onDestroy();
    }
    private int dp(int x) { return Math.round(x*getResources().getDisplayMetrics().density); }
    private TextView text(String value,int sp,int color) {
        TextView view=new TextView(this); view.setText(value); view.setTextSize(sp); view.setTextColor(color); view.setPadding(0,dp(4),0,dp(6)); return view;
    }
    private Button button(String label,Runnable action) {
        Button view=new Button(this); view.setText(label); view.setAllCaps(false); view.setTextColor(GREEN); view.setMinHeight(dp(48)); view.setOnClickListener(v->action.run()); return view;
    }
    private LinearLayout row() { LinearLayout view=new LinearLayout(this); view.setOrientation(LinearLayout.HORIZONTAL); return view; }
    private void rowButton(LinearLayout row,String label,Runnable action) { row.addView(button(label,action),new LinearLayout.LayoutParams(0,-2,1)); }
    private LinearLayout card(String title) {
        LinearLayout card=new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL); card.setPadding(dp(24),dp(20),dp(24),dp(20));
        GradientDrawable background=new GradientDrawable(); background.setColor(0xffffffff); background.setCornerRadius(dp(16)); background.setStroke(dp(1),0xffdce7e0); card.setBackground(background);
        LinearLayout.LayoutParams layout=new LinearLayout.LayoutParams(-1,-2); layout.bottomMargin=dp(16); content.addView(card,layout);
        TextView heading=text(title,18,INK); heading.setTypeface(null,Typeface.BOLD); card.addView(heading); return card;
    }
    private void draw() {
        ScrollView scroll=new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(BG);
        content=new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL); content.setPadding(dp(20),dp(24),dp(20),dp(32)); scroll.addView(content);
        scroll.setOnApplyWindowInsetsListener((v,insets)->{ v.setPadding(0,insets.getSystemWindowInsetTop(),0,insets.getSystemWindowInsetBottom()); return insets; });
        setContentView(scroll);
        TextView heading=text("HID 宏工作台",28,INK); heading.setTypeface(null,Typeface.BOLD); content.addView(heading);
        content.addView(text("录制一个手势，让板卡重复执行",15,MUTED));
        LinearLayout device=card("本机板卡"); connection=text("先配对系统蓝牙，再连接控制服务",14,MUTED); device.addView(connection);
        LinearLayout connectRow=row(); rowButton(connectRow,"选择板卡",this::chooseDevice); rowButton(connectRow,"系统蓝牙",()->startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS))); device.addView(connectRow);

        LinearLayout macro=card("动作宏"); library=new Spinner(this); macro.addView(library); refreshLibrary();
        library.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            public void onItemSelected(android.widget.AdapterView<?> p,View v,int position,long id) { if(!loading) { selected=position; refreshSteps(); } }
            public void onNothingSelected(android.widget.AdapterView<?> p) {}
        });
        LinearLayout manage=row(); rowButton(manage,"新建",()->{ macros.add(new Macro()); selected=macros.size()-1; save(); refreshLibrary(); refreshSteps(); settings(); });
        rowButton(manage,"设置",this::settings); rowButton(manage,"删除",()->new AlertDialog.Builder(this).setTitle("删除此动作宏？").setMessage(current().name).setNegativeButton("取消",null).setPositiveButton("删除",(d,w)->{macros.remove(selected); if(macros.isEmpty())macros.add(new Macro()); selected=0;save();refreshLibrary();refreshSteps();}).show()); macro.addView(manage);
        capacity=text("",13,MUTED); macro.addView(capacity);
        steps=new LinearLayout(this); steps.setOrientation(LinearLayout.VERTICAL); macro.addView(steps);
        LinearLayout add=row(); rowButton(add,"画手势",this::drawGesture); rowButton(add,"等待",()->editWait(-1)); rowButton(add,"上滑模板",()->{current().steps.add(Macro.Step.line(.5f,.8f,.5f,.25f,300)); save();refreshSteps();}); macro.addView(add);
        record=button("在目标应用上录制",this::record); macro.addView(record);
        macro.addView(text("录制时先切到目标应用。悬浮层记录手势，松手后板卡重放；结束后可调整步骤与等待时间。",13,MUTED));

        LinearLayout player=card("回放"); progress=text("尚未开始",14,MUTED); player.addView(progress);
        play=button("上传并播放",()->{
            if(service==null)return;
            save(); foreground(); service.play(current(),null);
        });
        play.setTextColor(0xffffffff); play.setBackgroundTintList(android.content.res.ColorStateList.valueOf(GREEN)); player.addView(play);
        LinearLayout controls=row(); pause=button("暂停",()->{if(service!=null)service.pauseResume();}); stop=button("停止",()->{if(service!=null)service.stopMacro();});
        controls.addView(pause,new LinearLayout.LayoutParams(0,-2,1)); controls.addView(stop,new LinearLayout.LayoutParams(0,-2,1)); player.addView(controls);
        player.addView(text("默认预留 5 秒切换应用。回放由板卡执行；切换应用后，可从通知栏停止。板卡断电后需重新上传。",13,MUTED));
        content.addView(button("输入验证页",()->startActivity(new Intent(this,InputTestActivity.class))));
        refreshSteps(); refreshStatus();
    }
    private Macro current() { return macros.get(selected); }
    private void save() { try { MacroStore.save(this,macros); } catch(Exception e) {toast(e.getMessage());} }
    private void refreshLibrary() {
        if(library==null || macros.isEmpty())return; loading=true;
        List<String> names=new ArrayList<>();for(Macro m:macros)names.add(m.name);
        library.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,names));library.setSelection(selected);loading=false;
    }
    private void refreshSteps() {
        if(steps==null || macros.isEmpty())return; steps.removeAllViews();
        for(int i=0;i<current().steps.size();i++) {
            final int index=i; Macro.Step step=current().steps.get(i);
            TextView line=text((i+1)+"  "+step.title()+"  ·  "+step.duration+" ms",16,INK); line.setMinHeight(dp(48)); line.setGravity(Gravity.CENTER_VERTICAL);
            line.setBackgroundResource(android.R.drawable.list_selector_background); line.setOnClickListener(v->stepMenu(index)); steps.addView(line);
        }
        if(current().steps.isEmpty())steps.addView(text("添加手势或开始录制",14,MUTED));
        String detail=(current().repeats==0?"持续循环":current().repeats+" 次循环")+" · "+current().startDelay/1000f+" 秒后开始";
        try { detail+=" · "+MacroCodec.compile(current()).length/MacroCodec.RECORD_SIZE+" / 64 条记录"; }
        catch(Exception e) { detail+="\n"+e.getMessage(); }
        capacity.setText(detail);
    }
    private void refreshStatus() {
        if(connection==null || service==null)return;
        connection.setText(service.message);
        byte[] m=service.macroState,s=service.state; int state=m==null?0:m[1]&255;
        boolean active=state>=3;
        play.setEnabled(service.connected&&!service.busy&&!active);
        record.setEnabled(service.connected&&!service.busy&&!active);
        pause.setEnabled(service.connected&&!service.busy&&(state==3||state==4)); pause.setText(state==4?"继续":"暂停");
        stop.setEnabled(service.connected);
        String[] names={"板卡无宏","上传中","就绪","回放中","已暂停","手势结束后暂停"};
        progress.setText(m==null?"尚未连接":(state<names.length?names[state]:"未知状态")+" · 输入 "+(m[3]&255)+" / "+(m[4]&255)+"\n剩余循环 "+(MacroCodec.u16(m,6)==0?"持续":MacroCodec.u16(m,6))+(s!=null&&s[1]==1?" · 本机控制":""));
    }
    private void toast(String text) { Toast.makeText(this,text,Toast.LENGTH_LONG).show(); }
    private void foreground() {
        startForegroundService(new Intent(this,MacroService.class));
        if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED
                && !getPreferences(MODE_PRIVATE).getBoolean("notificationAsked",false)) {
            getPreferences(MODE_PRIVATE).edit().putBoolean("notificationAsked",true).apply();
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},92);
        }
    }

    private EditText field(LinearLayout form,String label,String value,boolean number) {
        form.addView(text(label,14,MUTED)); EditText edit=new EditText(this);edit.setSingleLine(true);edit.setText(value);edit.setMinHeight(dp(48));
        if(number)edit.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        form.addView(edit);return edit;
    }
    private LinearLayout form() { LinearLayout f=new LinearLayout(this);f.setOrientation(LinearLayout.VERTICAL);f.setPadding(dp(24),dp(8),dp(24),dp(8));return f; }
    private interface SaveForm { void run() throws Exception; }
    private void dialog(String title,View view,SaveForm action) {
        ScrollView scroll=new ScrollView(this);scroll.addView(view);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(title).setView(scroll).setNegativeButton("取消",null).setPositiveButton("保存",null).create();
        dialog.setOnShowListener(d->dialog.getButton(-1).setOnClickListener(v->{try{action.run();save();refreshLibrary();refreshSteps();dialog.dismiss();}catch(Exception e){toast(e.getMessage()==null?"请检查输入":e.getMessage());}}));dialog.show();
    }
    private int number(EditText edit,int min,int max) {
        int value;try{value=Integer.parseInt(edit.getText().toString());}catch(Exception e){throw new IllegalArgumentException("请输入整数");}
        if(value<min||value>max)throw new IllegalArgumentException("数值须在 "+min+" 至 "+max+" 之间");return value;
    }
    private void settings() {
        LinearLayout f=form();Macro macro=current();
        EditText name=field(f,"名称",macro.name,false), repeats=field(f,"循环次数（0 为持续循环）",""+macro.repeats,true), delay=field(f,"启动延时 / 毫秒",""+macro.startDelay,true);
        dialog("宏设置",f,()->{String value=name.getText().toString().trim();if(value.isEmpty())throw new IllegalArgumentException("名称不能为空");int n=number(repeats,0,65535),ms=number(delay,0,65535);macro.name=value;macro.repeats=n;macro.startDelay=ms;});
    }
    private void editWait(int index) {
        LinearLayout f=form(); EditText duration=field(f,"等待时间 / 毫秒",index<0?"15000":""+current().steps.get(index).duration,true);
        dialog("等待",f,()->{int ms=number(duration,1,3600000);if(index<0)current().steps.add(Macro.Step.waitFor(ms));else current().steps.get(index).duration=ms;});
    }
    private void stepMenu(int index) {
        Macro.Step step=current().steps.get(index);
        String[] options=step.wait?new String[]{"调整时间","上移","下移","删除"}:new String[]{"调整时间","上移","下移","删除","设置直线坐标"};
        new AlertDialog.Builder(this).setTitle((index+1)+" · "+step.title()).setItems(options,(d,which)->{
            if(which==0) {
                if(step.wait){editWait(index);return;}
                LinearLayout f=form();EditText value=field(f,"手势时长 / 毫秒",""+step.duration,true);
                dialog("调整时长",f,()->step.duration=number(value,40,10000));return;
            }
            if(which==1&&index>0)Collections.swap(current().steps,index,index-1);
            if(which==2&&index<current().steps.size()-1)Collections.swap(current().steps,index,index+1);
            if(which==3)current().steps.remove(index);
            if(which==4){coordinates(index);return;}
            save();refreshSteps();
        }).show();
    }
    private void coordinates(int index) {
        Macro.Step step=current().steps.get(index);Macro.Point a=step.points.get(0),b=step.points.get(step.points.size()-1);
        LinearLayout f=form();f.addView(text("坐标为全屏百分比；保存后使用直线路径。",14,MUTED));
        EditText x0=field(f,"起点 X / %",""+Math.round(a.x*100),true),y0=field(f,"起点 Y / %",""+Math.round(a.y*100),true),x1=field(f,"终点 X / %",""+Math.round(b.x*100),true),y1=field(f,"终点 Y / %",""+Math.round(b.y*100),true);
        dialog("手势坐标",f,()->current().steps.set(index,Macro.Step.line(number(x0,0,100)/100f,number(y0,0,100)/100f,number(x1,0,100)/100f,number(y1,0,100)/100f,step.duration)));
    }
    private void drawGesture() {
        LinearLayout f=form();f.addView(text("画板代表完整手机屏幕。手势自动加入当前宏，重复绘制可添加多步。",14,MUTED));
        TextView feedback=text("等待手势",14,MUTED);f.addView(feedback);
        GesturePad pad=new GesturePad(this,step->{current().steps.add(step);current().orientation=getResources().getConfiguration().orientation;save();refreshSteps();feedback.setText("已添加 "+step.title()+" · "+step.duration+" ms");});
        f.addView(pad,new LinearLayout.LayoutParams(-1,dp(360)));
        new AlertDialog.Builder(this).setTitle("录入手势").setView(f).setPositiveButton("完成",null).show();
    }
    private void record() {
        if(!Settings.canDrawOverlays(this)){startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,Uri.parse("package:"+getPackageName())));toast("授权悬浮窗后返回，重新点击录制");return;}
        if(service==null)return;
        foreground();
        new AlertDialog.Builder(this).setTitle("开始录制").setMessage("确认后有 5 秒切换到目标应用。随后悬浮层采集单指手势，板卡在松手后执行。点击底部“结束录制并保存”结束。").setNegativeButton("取消",null).setPositiveButton("5 秒后开始",(d,w)->{moveTaskToBack(true);main.postDelayed(()->{if(service!=null)service.record();},5000);}).show();
    }
    private boolean permissions() {
        List<String> needed=new ArrayList<>();
        if(Build.VERSION.SDK_INT>=31){needed.add(Manifest.permission.BLUETOOTH_CONNECT);needed.add(Manifest.permission.BLUETOOTH_SCAN);}
        else {needed.add(Manifest.permission.ACCESS_COARSE_LOCATION);needed.add(Manifest.permission.ACCESS_FINE_LOCATION);}
        needed.removeIf(p->checkSelfPermission(p)==PackageManager.PERMISSION_GRANTED);
        if(needed.isEmpty())return true;
        requestPermissions(needed.toArray(new String[0]),91);return false;
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results) {
        super.onRequestPermissionsResult(request,permissions,results);
        if(request==91)toast("权限已处理，请点击选择板卡；蓝牙权限为必需权限");
        if(request==92 && (results.length==0 || results[0]!=PackageManager.PERMISSION_GRANTED))toast("通知未开启，回放时请返回应用点击停止");
    }
    @SuppressLint("MissingPermission") private void chooseDevice() {
        if(!permissions())return;
        BluetoothManager manager=getSystemService(BluetoothManager.class);BluetoothAdapter adapter=manager.getAdapter();
        if(adapter==null||!adapter.isEnabled()){startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));return;}
        List<BluetoothDevice> devices=new ArrayList<>(adapter.getBondedDevices());devices.sort(Comparator.comparing(d->String.valueOf(d.getName())));
        List<String> labels=new ArrayList<>();for(BluetoothDevice d:devices)labels.add(String.valueOf(d.getName())+"\n"+d.getAddress());
        ArrayAdapter<String> items=new ArrayAdapter<>(this,android.R.layout.simple_list_item_1,labels);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("选择本机已配对的板卡").setAdapter(items,(d,which)->{
            BluetoothDevice device=devices.get(which);
            if(device.getBondState()!=BluetoothDevice.BOND_BONDED){toast("请先在系统蓝牙设置中配对此板卡");startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));return;}
            if(service!=null){foreground();service.connect(device.getAddress());}
        }).setNeutralButton("扫描附近",null).setNegativeButton("关闭",null).create();
        android.bluetooth.le.BluetoothLeScanner scanner=adapter.getBluetoothLeScanner();
        android.bluetooth.le.ScanCallback callback=new android.bluetooth.le.ScanCallback(){
            @Override public void onScanResult(int type,android.bluetooth.le.ScanResult result){runOnUiThread(()->{
                BluetoothDevice device=result.getDevice();String name=result.getScanRecord()==null?device.getName():result.getScanRecord().getDeviceName();
                if(name==null||!name.startsWith("CH592F")||devices.contains(device))return;
                devices.add(device);items.add(name+"\n"+device.getAddress()+" · 请先系统配对");
            });}
            @Override public void onScanFailed(int code){runOnUiThread(()->toast("扫描失败："+code));}
        };
        dialog.setOnShowListener(d->dialog.getButton(-3).setOnClickListener(v->{if(scanner!=null){scanner.startScan(callback);dialog.getButton(-3).setEnabled(false);main.postDelayed(()->{scanner.stopScan(callback);if(dialog.isShowing())dialog.getButton(-3).setEnabled(true);},8000);}}));
        dialog.setOnDismissListener(d->{if(scanner!=null)scanner.stopScan(callback);});dialog.show();
    }
}
