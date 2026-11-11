package com.ch592macro;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.CRC32;

public final class MacroCodec {
    public static final int RECORD_SIZE = 11, MAX_RECORDS = 64;
    private MacroCodec() {}

    public static byte[] compile(Macro macro) {
        if (macro.steps.isEmpty()) throw new IllegalArgumentException("请先录制或添加动作");
        if (macro.repeats < 0 || macro.repeats > 65535 || macro.startDelay < 0 || macro.startDelay > 65535)
            throw new IllegalArgumentException("循环次数或启动延时超出范围");
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        boolean gesture = false;
        for (Macro.Step step : macro.steps) {
            if (step.wait) {
                if (step.duration < 1 || step.duration > 3600000) throw new IllegalArgumentException("等待时间须为 1 毫秒至 1 小时");
                int left = step.duration;
                while (left > 0) { int ms = Math.min(left, 65535); record(out, ms, 0, 0, 0, 0); left -= ms; }
            } else {
                gesture = true;
                if (step.duration < 40 || step.duration > 10000 || step.points.size() < 2)
                    throw new IllegalArgumentException("手势须包含起终点，时长为 40 至 10000 毫秒");
                int previous = -1;
                for (Macro.Point p : step.points) {
                    if (!Float.isFinite(p.x) || !Float.isFinite(p.y) || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1 || p.time < previous)
                        throw new IllegalArgumentException("手势坐标或时间无效");
                    previous = p.time;
                }
                int recordedTime = step.points.get(step.points.size() - 1).time;
                if (recordedTime <= 0) throw new IllegalArgumentException("手势没有有效时长");
                int intervals = Math.min(16, Math.max(2, step.duration / 20));
                for (int i = 0; i <= intervals; i++) {
                    float t = (float) recordedTime * i / intervals;
                    Macro.Point p = interpolate(step.points, t);
                    int delay = i == intervals ? 20 : step.duration * (i + 1) / intervals - step.duration * i / intervals;
                    record(out, delay, 3, 1, Math.round(p.x * 32767), Math.round(p.y * 32767));
                }
                Macro.Point end = step.points.get(step.points.size() - 1);
                record(out, 40, 3, 0, Math.round(end.x * 32767), Math.round(end.y * 32767));
            }
            if (out.size() / RECORD_SIZE > MAX_RECORDS)
                throw new IllegalArgumentException("动作过多：板卡最多容纳 64 条输入记录，请拆分宏");
        }
        if (!gesture) throw new IllegalArgumentException("宏至少需要一个点按或滑动动作");
        return out.toByteArray();
    }

    private static Macro.Point interpolate(List<Macro.Point> path, float t) {
        for (int i = 1; i < path.size(); i++) {
            Macro.Point a = path.get(i - 1), b = path.get(i);
            if (t <= b.time) {
                float f = b.time == a.time ? 1 : Math.max(0, Math.min(1, (t - a.time) / (b.time - a.time)));
                return new Macro.Point(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, (int)t);
            }
        }
        return path.get(path.size() - 1);
    }

    private static void record(ByteArrayOutputStream out, int delay, int id, int tip, int x, int y) {
        byte[] record = new byte[RECORD_SIZE];
        put16(record, 0, delay); record[2] = (byte) id;
        if (id == 3) {
            record[3] = (byte)tip; put16(record, 5, x); put16(record, 7, y); record[9] = 1;
        }
        out.write(record, 0, record.length);
    }

    public static List<byte[]> upload(byte[] records) {
        if (records.length == 0 || records.length % RECORD_SIZE != 0 || records.length > MAX_RECORDS * RECORD_SIZE)
            throw new IllegalArgumentException("宏记录长度无效");
        CRC32 crc = new CRC32(); crc.update(records);
        byte[] begin = {0x10, (byte)(records.length / RECORD_SIZE), 0, 0, 0, 0};
        for (int i = 0; i < 4; i++) begin[i + 2] = (byte)(crc.getValue() >>> (i * 8));
        List<byte[]> packets = new ArrayList<>(); packets.add(begin);
        for (int offset = 0; offset < records.length; offset += RECORD_SIZE) {
            byte[] packet = new byte[RECORD_SIZE + 3]; packet[0] = 0x11; put16(packet, 1, offset);
            System.arraycopy(records, offset, packet, 3, RECORD_SIZE); packets.add(packet);
        }
        packets.add(new byte[]{0x12}); return packets;
    }

    public static byte[] start(int repeats, int delay) {
        byte[] p = {0x13,0,0,0,0}; put16(p,1,repeats); put16(p,3,delay); return p;
    }
    public static int u16(byte[] b, int at) { return (b[at] & 255) | ((b[at + 1] & 255) << 8); }
    public static long u32(byte[] b, int at) { return u16(b,at) | ((long)u16(b,at + 2) << 16); }
    private static void put16(byte[] b, int at, int value) { b[at] = (byte)value; b[at + 1] = (byte)(value >>> 8); }
}
