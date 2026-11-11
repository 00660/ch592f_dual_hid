package com.ch592macro;

import android.app.Activity;
import android.os.Bundle;
import android.view.*;
import android.widget.*;

public final class InputTestActivity extends Activity {
    public volatile int gestures;
    public volatile float startX, startY, endX, endY;
    public volatile String inputName = "";
    private TextView evidence;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(24,48,24,24);root.setBackgroundColor(0xfff3f6f4);
        evidence=new TextView(this);evidence.setTextSize(20);evidence.setTextColor(0xff203a32);evidence.setText("输入验证\n等待板卡点按或滑动");root.addView(evidence);
        TextView instructions=new TextView(this);instructions.setTextSize(16);instructions.setText("此页显示实际收到的输入设备与手势起终点。\n回放前切到此页，保持屏幕点亮。");root.addView(instructions);
        setContentView(root);
    }
    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        if(event.getActionMasked()==MotionEvent.ACTION_DOWN){startX=event.getRawX();startY=event.getRawY();}
        if(event.getActionMasked()==MotionEvent.ACTION_UP){
            endX=event.getRawX();endY=event.getRawY();
            InputDevice device=event.getDevice();inputName=device==null?"未知":device.getName();
            gestures++;
            String value="已收到 "+gestures+" 次手势\n设备："+inputName+"\n起点："+(int)startX+", "+(int)startY+"\n终点："+(int)endX+", "+(int)endY;
            evidence.setText(value);android.util.Log.i("HIDMacroInput",value.replace('\n',' '));
        }
        return true;
    }
}
