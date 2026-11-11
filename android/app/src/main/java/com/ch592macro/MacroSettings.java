package com.ch592macro;

import android.app.AlertDialog;
import android.content.Context;
import android.text.InputType;
import android.view.*;
import android.widget.*;

public final class MacroSettings {
    private MacroSettings() {}
    private static EditText field(Context context, LinearLayout form, String name, String value, boolean numeric) {
        TextView label=new TextView(context);label.setText(name);label.setTextColor(0xff51675f);form.addView(label);
        EditText input=new EditText(context);input.setText(value);input.setSingleLine();input.setContentDescription(name);
        input.setInputType(numeric?InputType.TYPE_CLASS_NUMBER:InputType.TYPE_CLASS_TEXT);
        form.addView(input);return input;
    }
    private static int value(EditText input, int min, int max) {
        int number;
        try { number=Integer.parseInt(input.getText().toString()); }
        catch (NumberFormatException e) { throw new IllegalArgumentException("请输入整数"); }
        if(number<min||number>max)throw new IllegalArgumentException("数值范围："+min+" 至 "+max);
        return number;
    }
    public static void show(Context context, Macro macro, boolean overlay, Runnable saved) {
        int pad=Math.round(24*context.getResources().getDisplayMetrics().density);
        LinearLayout form=new LinearLayout(context);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(pad,pad/2,pad,pad/2);
        EditText name=field(context,form,"宏名称",macro.name,false);
        TextView caption=new TextView(context);caption.setText("执行方式");form.addView(caption);
        Spinner mode=new Spinner(context);mode.setContentDescription("执行方式");
        mode.setAdapter(new ArrayAdapter<>(context,android.R.layout.simple_spinner_dropdown_item,new String[]{"执行一次","指定循环次数","持续循环直到停止"}));
        form.addView(mode);mode.setSelection(macro.repeats==0?2:macro.repeats==1?0:1);
        EditText repeats=field(context,form,"循环次数",String.valueOf(Math.max(2,macro.repeats)),true);
        repeats.setEnabled(mode.getSelectedItemPosition()==1);
        mode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> p,View v,int position,long id){repeats.setEnabled(position==1);}
            public void onNothingSelected(AdapterView<?> p){}
        });
        EditText delay=field(context,form,"启动延时 / 毫秒",String.valueOf(macro.startDelay),true);
        EditText gap=field(context,form,"每轮结束后等待 / 毫秒",String.valueOf(macro.loopDelay),true);
        EditText jitterX=field(context,form,"每轮水平坐标漂移 / 像素，0 关闭",String.valueOf(macro.jitterX),true);
        EditText jitterY=field(context,form,"每轮垂直坐标漂移 / 像素，0 关闭",String.valueOf(macro.jitterY),true);
        TextView hint=new TextView(context);hint.setText("每轮对整段路径使用同一个随机偏移，下一轮重新选择；偏移受全路径坐标边界限制。请按目标区域大小设置。");form.addView(hint);
        TextView error=new TextView(context);error.setTextColor(0xffad302f);form.addView(error);
        ScrollView scroll=new ScrollView(context);scroll.addView(form);
        AlertDialog dialog=new AlertDialog.Builder(context).setTitle("执行参数").setView(scroll).setNegativeButton("取消",null).setPositiveButton("保存",null).create();
        if(overlay)dialog.getWindow().setType(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
        dialog.setOnShowListener(d->dialog.getButton(-1).setOnClickListener(v->{
            try {
                String title=name.getText().toString().trim();if(title.isEmpty())throw new IllegalArgumentException("名称不能为空");
                android.util.DisplayMetrics screen=new android.util.DisplayMetrics();
                context.getSystemService(WindowManager.class).getDefaultDisplay().getRealMetrics(screen);
                int count=mode.getSelectedItemPosition()==2?0:mode.getSelectedItemPosition()==0?1:value(repeats,2,65535);
                int ms=value(delay,0,65535),interval=value(gap,0,3600000);
                int x=value(jitterX,0,screen.widthPixels-1),y=value(jitterY,0,screen.heightPixels-1);
                macro.name=title;macro.repeats=count;macro.startDelay=ms;macro.loopDelay=interval;macro.jitterX=x;macro.jitterY=y;
                macro.screenWidth=screen.widthPixels;macro.screenHeight=screen.heightPixels;
                saved.run();dialog.dismiss();
            } catch (Exception problem) { error.setText(problem.getMessage()); }
        }));dialog.show();
    }
}
