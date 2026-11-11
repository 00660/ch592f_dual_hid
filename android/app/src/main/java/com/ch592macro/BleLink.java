package com.ch592macro;

import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.content.Context;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

@SuppressLint("MissingPermission")
public final class BleLink implements AutoCloseable {
    public static final UUID SERVICE = uuid(1), COMMAND = uuid(2), STATUS = uuid(3), MACRO = uuid(5);
    private volatile BluetoothGatt gatt;
    private volatile BluetoothGattCharacteristic command, status, macro;
    private volatile CompletableFuture<byte[]> pending;
    private volatile UUID pendingUuid;
    private volatile CompletableFuture<Void> connecting;
    private final Runnable disconnected;

    public BleLink(Runnable disconnected) { this.disconnected = disconnected; }
    private static UUID uuid(int n) { return UUID.fromString("5920000" + n + "-3c7f-4a9e-8c51-3b9d4b6a1200"); }
    public boolean ready() { return command != null && status != null && macro != null; }

    public void connect(Context context, BluetoothDevice device) throws Exception {
        close();
        if (device.getBondState() != BluetoothDevice.BOND_BONDED)
            throw new IllegalStateException("请先在系统蓝牙设置中配对并连接板卡，再返回这里");
        CompletableFuture<Void> future = new CompletableFuture<>(); connecting = future;
        gatt = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE);
        if (gatt == null) throw new IllegalStateException("系统无法建立蓝牙连接");
        try { future.get(35, TimeUnit.SECONDS); }
        catch (Exception error) { close(); throw error; }
        byte[] state = status();
        if (state[1] != 1) { close(); throw new IllegalStateException("本机未占用 HID 连接：请断开其他手机，让本机先连接板卡"); }
    }

    public byte[] status() throws Exception {
        byte[] value = read(status);
        if (value.length != 20 || value[0] != 1 || MacroCodec.u32(value,12) != 0x592F0301L)
            throw new IllegalStateException("设备状态与 CH592F 协议不匹配");
        if (MacroCodec.u32(value,8) < 0x10100)
            throw new IllegalStateException("请先升级支持动作宏的 1.1.0 或更新固件");
        return value;
    }

    public byte[] macroStatus() throws Exception {
        byte[] value = read(macro);
        if (value.length != 20 || value[0] != 1 || MacroCodec.u16(value,12) != MacroCodec.RECORD_SIZE)
            throw new IllegalStateException("板卡宏协议不兼容");
        return value;
    }

    private byte[] read(BluetoothGattCharacteristic characteristic) throws Exception {
        if (gatt == null || characteristic == null) throw new IllegalStateException("板卡尚未连接或固件不支持动作宏");
        CompletableFuture<byte[]> future = new CompletableFuture<>(); pending = future; pendingUuid = characteristic.getUuid();
        try {
            if (!gatt.readCharacteristic(characteristic)) throw new IllegalStateException("蓝牙读取未启动");
            return future.get(8, TimeUnit.SECONDS);
        } catch (TimeoutException error) { close(); disconnected.run(); throw error; }
        finally { if (pending == future) pending = null; }
    }

    @SuppressWarnings("deprecation")
    public void write(byte[] bytes) throws Exception {
        if (gatt == null || command == null) throw new IllegalStateException("板卡未连接");
        CompletableFuture<byte[]> future = new CompletableFuture<>(); pending = future; pendingUuid = COMMAND;
        try {
            boolean started;
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                started = gatt.writeCharacteristic(command, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS;
            } else {
                command.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT); command.setValue(bytes);
                started = gatt.writeCharacteristic(command);
            }
            if (!started) throw new IllegalStateException("蓝牙写入未启动");
            future.get(8, TimeUnit.SECONDS);
        } catch (TimeoutException error) { close(); disconnected.run(); throw error; }
        finally { if (pending == future) pending = null; }
    }

    private void result(UUID uuid, int code, byte[] value) {
        CompletableFuture<byte[]> future = pending;
        if (future == null || !uuid.equals(pendingUuid)) return;
        if (code == BluetoothGatt.GATT_SUCCESS) future.complete(value);
        else future.completeExceptionally(new IllegalStateException("板卡拒绝请求：GATT " + code + "，请检查配对、输入就绪和宏状态"));
    }

    private final BluetoothGattCallback callback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt source, int code, int state) {
            if (source != gatt) return;
            if (code == BluetoothGatt.GATT_SUCCESS && state == BluetoothProfile.STATE_CONNECTED) {
                if (!source.discoverServices()) fail("无法发现板卡服务");
            } else if (state == BluetoothProfile.STATE_DISCONNECTED || code != BluetoothGatt.GATT_SUCCESS) {
                fail("蓝牙连接已断开：" + code); command = status = macro = null; disconnected.run();
            }
        }
        @Override public void onServicesDiscovered(BluetoothGatt source, int code) {
            if (source != gatt) return;
            BluetoothGattService service = source.getService(SERVICE);
            if (code != BluetoothGatt.GATT_SUCCESS || service == null) { fail("未发现板卡控制服务"); return; }
            command = service.getCharacteristic(COMMAND); status = service.getCharacteristic(STATUS); macro = service.getCharacteristic(MACRO);
            if (!ready()) { fail("固件缺少宏服务，请升级至 1.1.0 或更新版本"); return; }
            if (connecting != null) connecting.complete(null);
        }
        @Override public void onCharacteristicRead(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value, int code) {
            if (source == gatt) result(characteristic.getUuid(),code,value);
        }
        @Override @SuppressWarnings("deprecation") public void onCharacteristicRead(BluetoothGatt source, BluetoothGattCharacteristic characteristic, int code) {
            if (android.os.Build.VERSION.SDK_INT < 33 && source == gatt) result(characteristic.getUuid(),code,characteristic.getValue());
        }
        @Override public void onCharacteristicWrite(BluetoothGatt source, BluetoothGattCharacteristic characteristic, int code) {
            if (source == gatt) result(characteristic.getUuid(),code,new byte[0]);
        }
        @Override public void onServiceChanged(BluetoothGatt source) {
            if (source != gatt) return;
            if (connecting != null && !connecting.isDone()) {
                if (!source.discoverServices()) fail("无法重新发现板卡服务");
                return;
            }
            command = status = macro = null;
            fail("板卡服务已更新，请重新连接"); close(); disconnected.run();
        }
    };

    private void fail(String message) {
        IllegalStateException error = new IllegalStateException(message);
        if (connecting != null) connecting.completeExceptionally(error);
        if (pending != null) pending.completeExceptionally(error);
    }
    @Override public void close() {
        BluetoothGatt old = gatt; gatt = null; command = status = macro = null;
        fail("连接已关闭");
        if (old != null) { old.disconnect(); old.close(); }
    }
}
