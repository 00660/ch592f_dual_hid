package com.ch592macro;

import android.content.*;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.view.*;
import android.widget.*;
import java.util.List;

public final class FloatingToolbar {
    private final MacroService service;
    private final WindowManager windows;
    private final WindowManager.LayoutParams layout;
    private LinearLayout root;
    private TextView title, status;
    private Button record, play, stop, settings, select;
    private boolean attached, compact;
    private int savedX, savedY;

    public FloatingToolbar(MacroService service) {
        this.service=service;windows=service.getSystemService(WindowManager.class);
        layout=new WindowManager.LayoutParams(dp(320),-2,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,PixelFormat.TRANSLUCENT);
        layout.gravity=Gravity.TOP|Gravity.LEFT;layout.x=dp(8);layout.y=dp(80);
    }
    private int dp(int value){return Math.round(value*service.getResources().getDisplayMetrics().density);}
    private Button button(String label,Runnable action) {
        Button b=new Button(service);b.setText(label);b.setTextSize(13);b.setAllCaps(false);b.setTextColor(0xff164c40);
        b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(dp(48));b.setMinimumHeight(dp(48));b.setPadding(dp(3),0,dp(3),0);
        b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xffe2f3eb));b.setOnClickListener(v->action.run());return b;
    }
    private void add(LinearLayout row,View view){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,dp(48),1);p.setMargins(dp(2),0,dp(2),0);row.addView(view,p);}
    private void drag(View handle) {
        handle.setOnTouchListener(new View.OnTouchListener(){
            float startX,startY;int x,y;boolean moved;
            public boolean onTouch(View view,android.view.MotionEvent event){
                if(event.getActionMasked()==MotionEvent.ACTION_DOWN){startX=event.getRawX();startY=event.getRawY();x=layout.x;y=layout.y;moved=false;return true;}
                if(event.getActionMasked()==MotionEvent.ACTION_MOVE){
                    float dx=event.getRawX()-startX,dy=event.getRawY()-startY;
                    if(Math.abs(dx)+Math.abs(dy)>dp(6))moved=true;
                    android.util.DisplayMetrics m=metrics();layout.x=Math.max(0,Math.min(m.widthPixels-layout.width,x+(int)dx));
                    layout.y=Math.max(dp(24),Math.min(m.heightPixels-root.getHeight()-dp(24),y+(int)dy));
                    if(attached)windows.updateViewLayout(root,layout);return true;
                }
                if(event.getActionMasked()==MotionEvent.ACTION_UP){if(!moved)view.performClick();return true;}
                return true;
            }
        });
    }
    private android.util.DisplayMetrics metrics(){android.util.DisplayMetrics m=new android.util.DisplayMetrics();windows.getDefaultDisplay().getRealMetrics(m);return m;}
    public boolean show(){render();return attached;}
    private void render(){
        detach();root=new LinearLayout(service);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(6),dp(6),dp(6),dp(6));
        GradientDrawable bg=new GradientDrawable();bg.setColor(0xfff5faf7);bg.setCornerRadius(dp(14));bg.setStroke(dp(1),0xffb6cdc2);root.setBackground(bg);
        layout.width=Math.min(metrics().widthPixels-dp(16),dp(compact?180:320));
        LinearLayout actions=new LinearLayout(service);actions.setOrientation(LinearLayout.HORIZONTAL);
        if(compact){
            Button handle=button("工具",()->{if(!service.running()){compact=false;render();}});handle.setContentDescription("拖动工具条");drag(handle);add(actions,handle);
            play=button("暂停",service::pauseResume);add(actions,play);stop=button("停止",service::stopMacro);add(actions,stop);root.addView(actions);
            title=status=null;record=settings=select=null;
        }else{
            LinearLayout header=new LinearLayout(service);header.setOrientation(LinearLayout.HORIZONTAL);
            title=new TextView(service);title.setTextSize(14);title.setTextColor(0xff203a32);title.setGravity(Gravity.CENTER_VERTICAL);title.setContentDescription("拖动工具条");drag(title);
            header.addView(title,new LinearLayout.LayoutParams(0,dp(48),1));
            Button hide=button("关闭",service::closeToolbar);header.addView(hide,new LinearLayout.LayoutParams(dp(56),dp(48)));root.addView(header);
            status=new TextView(service);status.setTextSize(12);status.setTextColor(0xff51675f);status.setPadding(dp(6),0,dp(6),dp(6));root.addView(status);
            record=button("录制",()->{if(service.isRecording())service.finishRecording();else service.record();});add(actions,record);
            play=button("播放",()->{if(service.running())service.pauseResume();else service.play(service.selectedMacro,null);});add(actions,play);
            stop=button("停止",service::stopMacro);add(actions,stop);root.addView(actions);
            LinearLayout options=new LinearLayout(service);options.setOrientation(LinearLayout.HORIZONTAL);
            select=button("选择宏",this::chooseMacro);add(options,select);
            settings=button("参数",()->MacroSettings.show(service,service.selectedMacro,true,service::saveSelectedMacro));add(options,settings);
            Button edit=button("编辑步骤",()->service.startActivity(new Intent(service,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)));add(options,edit);root.addView(options);
        }
        try{windows.addView(root,layout);attached=true;refresh();}
        catch(RuntimeException error){Toast.makeText(service,"悬浮工具条无法显示，请检查悬浮窗权限",Toast.LENGTH_LONG).show();}
    }
    private void chooseMacro(){
        try{
            List<Macro> macros=MacroStore.load(service);String[] names=new String[macros.size()];for(int i=0;i<names.length;i++)names[i]=macros.get(i).name;
            android.app.AlertDialog dialog=new android.app.AlertDialog.Builder(service).setTitle("选择动作宏").setItems(names,(d,i)->service.selectMacro(macros.get(i),i)).setNegativeButton("取消",null).create();
            dialog.getWindow().setType(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);dialog.show();
        }catch(Exception error){Toast.makeText(service,error.getMessage(),Toast.LENGTH_LONG).show();}
    }
    public void refresh(){
        if(!attached)return;
        int state=service.macroState==null?0:service.macroState[1];boolean running=service.running(), recording=service.isRecording();
        if(title!=null)title.setText("拖动 · "+service.selectedMacro.name);
        if(status!=null)status.setText((recording?"录制中 · ":service.selectedMacro.playbackLabel()+"\n")+service.message);
        if(record!=null){record.setText(recording?"完成录制":"录制");record.setEnabled(service.connected&&!service.busy&&(!running||recording));}
        play.setText(running?(state==4?"继续":"暂停"):"播放");play.setEnabled(service.connected&&!service.busy&&!recording&&state!=5);
        stop.setEnabled(service.connected||recording);
        if(settings!=null)settings.setEnabled(!running&&!recording&&!service.busy);
        if(select!=null)select.setEnabled(!running&&!recording&&!service.busy);
        if(running||recording)layout.flags|=WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON;
        else layout.flags&=~WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON;
        windows.updateViewLayout(root,layout);
    }
    public boolean preparePlayback(Macro macro){
        android.util.DisplayMetrics m=metrics();int width=dp(180),height=dp(60),margin=dp(28);
        int[][] places={{dp(8),margin},{m.widthPixels-width-dp(8),margin},{dp(8),m.heightPixels-height-margin},{m.widthPixels-width-dp(8),m.heightPixels-height-margin}};
        for(int[] p:places){
            boolean overlap=false;
            for(Macro.Step step:macro.steps){
                if(step.wait)continue;float left=1,top=1,right=0,bottom=0;
                for(Macro.Point point:step.points){left=Math.min(left,point.x);right=Math.max(right,point.x);top=Math.min(top,point.y);bottom=Math.max(bottom,point.y);}
                if(left*m.widthPixels-macro.jitterX<=p[0]+width&&right*m.widthPixels+macro.jitterX>=p[0]&&top*m.heightPixels-macro.jitterY<=p[1]+height+dp(24)&&bottom*m.heightPixels+macro.jitterY>=p[1]){overlap=true;break;}
            }
            if(!overlap){if(!compact){savedX=layout.x;savedY=layout.y;}compact=true;layout.x=p[0];layout.y=p[1];render();return true;}
        }
        Toast.makeText(service,"手势覆盖工具条停靠区域，请调整手势或坐标漂移范围",Toast.LENGTH_LONG).show();return false;
    }
    public void restore(){if(compact){compact=false;layout.x=savedX;layout.y=savedY;render();}else refresh();}
    public void raise(){if(attached){windows.removeView(root);windows.addView(root,layout);}}
    private void detach(){if(attached){windows.removeView(root);attached=false;}}
    public void close(){detach();}
}
