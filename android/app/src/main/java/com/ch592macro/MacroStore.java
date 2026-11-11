package com.ch592macro;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.List;

public final class MacroStore {
    private MacroStore() {}
    public static List<Macro> load(Context context) {
        String data = context.getSharedPreferences("macros",0).getString("items",null);
        List<Macro> macros = new ArrayList<>();
        if (data == null) { macros.add(new Macro()); return macros; }
        try {
            JSONArray items = new JSONArray(data);
            for (int i = 0; i < items.length(); i++) macros.add(decode(items.getJSONObject(i)));
        } catch (Exception error) { throw new IllegalStateException("本地宏读取失败，原始数据已保留",error); }
        return macros;
    }
    public static void save(Context context, List<Macro> macros) {
        try {
            JSONArray items = new JSONArray();
            for (Macro macro : macros) items.put(encode(macro));
            context.getSharedPreferences("macros",0).edit().putString("items",items.toString()).apply();
        } catch (Exception error) { throw new IllegalStateException("保存动作宏失败",error); }
    }
    public static JSONObject encode(Macro macro) throws Exception {
        JSONObject item = new JSONObject().put("name",macro.name).put("repeats",macro.repeats)
                .put("delay",macro.startDelay).put("orientation",macro.orientation)
                .put("loopDelay",macro.loopDelay).put("jitterX",macro.jitterX).put("jitterY",macro.jitterY)
                .put("screenWidth",macro.screenWidth).put("screenHeight",macro.screenHeight);
        JSONArray steps = new JSONArray();
        for (Macro.Step step : macro.steps) {
            JSONArray points = new JSONArray();
            for (Macro.Point p : step.points) points.put(new JSONArray().put(p.x).put(p.y).put(p.time));
            steps.put(new JSONObject().put("wait",step.wait).put("duration",step.duration).put("points",points));
        }
        return item.put("steps",steps);
    }
    public static Macro decode(JSONObject item) throws Exception {
        Macro macro = new Macro();
        macro.name = item.getString("name"); macro.repeats = item.getInt("repeats"); macro.startDelay = item.getInt("delay");
        macro.orientation = item.optInt("orientation",1);
        if(!item.has("loopDelay") && macro.name.equals("短视频上滑"))macro.name="滑动与等待";
        macro.loopDelay = item.optInt("loopDelay",0); macro.jitterX = item.optInt("jitterX",0); macro.jitterY = item.optInt("jitterY",0);
        macro.screenWidth = item.optInt("screenWidth",0); macro.screenHeight = item.optInt("screenHeight",0);
        JSONArray steps = item.getJSONArray("steps");
        for (int i = 0; i < steps.length(); i++) {
            JSONObject value = steps.getJSONObject(i); Macro.Step step = new Macro.Step();
            step.wait = value.getBoolean("wait"); step.duration = value.getInt("duration");
            JSONArray points = value.getJSONArray("points");
            for (int j = 0; j < points.length(); j++) {
                JSONArray p = points.getJSONArray(j); step.points.add(new Macro.Point((float)p.getDouble(0),(float)p.getDouble(1),p.getInt(2)));
            }
            macro.steps.add(step);
        }
        return macro;
    }
}
