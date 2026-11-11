package com.ch592macro;

import java.util.ArrayList;
import java.util.List;

public final class Macro {
    public String name = "我的动作宏";
    public int repeats = 1;
    public int startDelay = 1000;
    public int loopDelay, jitterX, jitterY, screenWidth, screenHeight;
    public int orientation = 1;
    public final List<Step> steps = new ArrayList<>();

    public static final class Point {
        public final float x, y;
        public final int time;
        public Point(float x, float y, int time) { this.x = x; this.y = y; this.time = time; }
    }

    public static final class Step {
        public final List<Point> points = new ArrayList<>();
        public int duration;
        public boolean wait;
        public String title() { return wait ? "等待" : points.size() <= 2 && distance() < .01f ? "点按 / 长按" : "滑动"; }
        private float distance() {
            if (points.isEmpty()) return 0;
            Point a = points.get(0), b = points.get(points.size() - 1);
            return Math.abs(a.x - b.x) + Math.abs(a.y - b.y);
        }
        public static Step waitFor(int ms) {
            Step step = new Step(); step.wait = true; step.duration = ms; return step;
        }
        public static Step line(float x0, float y0, float x1, float y1, int ms) {
            Step step = new Step(); step.duration = ms;
            step.points.add(new Point(x0, y0, 0)); step.points.add(new Point(x1, y1, ms));
            return step;
        }
    }

    public String playbackLabel() {
        return repeats == 0 ? "持续循环，手动停止" : repeats == 1 ? "执行一次" : "循环 " + repeats + " 次";
    }
}
