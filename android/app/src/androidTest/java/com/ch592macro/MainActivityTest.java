package com.ch592macro;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.provider.Settings;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class MainActivityTest {
    @Test public void restoresSelectedBoardAndOpensToolbarFromMainScreen() throws Exception {
        var instrumentation=InstrumentationRegistry.getInstrumentation();
        instrumentation.getUiAutomation().adoptShellPermissionIdentity(Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.POST_NOTIFICATIONS);
        Context context=instrumentation.getTargetContext();
        String address=InstrumentationRegistry.getArguments().getString("board");
        assertNotNull("Pass -e board with the confirmed board address",address);
        assertTrue("Grant SYSTEM_ALERT_WINDOW appop before running",Settings.canDrawOverlays(context));
        context.stopService(new Intent(context,MacroService.class));
        assertTrue(context.getSharedPreferences("device",0).edit().putString("address",address).putString("name","上次选择的板卡").commit());
        String name=context.getSystemService(android.bluetooth.BluetoothManager.class).getAdapter().getRemoteDevice(address).getName();
        UiDevice ui=UiDevice.getInstance(instrumentation);
        try(ActivityScenario<MainActivity> screen=ActivityScenario.launch(MainActivity.class)) {
            assertTrue("Saved board was not reconnected on launch",ui.wait(Until.hasObject(By.text("正在使用的板卡")),45000));
            assertNotNull("Active board address is missing",ui.findObject(By.textContains(address)));
            if(name!=null&&!name.isEmpty())assertNotNull("Active board name was not refreshed",ui.findObject(By.textContains(name)));
            assertNotNull("Active connection is not shown",ui.findObject(By.textStartsWith("已连接")));
            assertTrue(ui.takeScreenshot(new java.io.File(context.getExternalFilesDir(null),"active-board.png")));
            UiScrollable content=new UiScrollable(new UiSelector().scrollable(true));
            assertTrue("Toolbar button was not reachable",content.scrollTextIntoView("开启悬浮工具条"));
            ui.waitForIdle();
            UiObject2 open=ui.findObject(By.text("开启悬浮工具条"));
            assertNotNull(open);assertTrue("Toolbar button is disabled",open.isEnabled());open.click();
            assertTrue("Toolbar did not open from the main screen",ui.wait(Until.hasObject(By.text("录制")),10000));
            assertTrue("Recording is unavailable after reconnect",ui.findObject(By.text("录制")).isEnabled());
            assertTrue("Playback is unavailable after reconnect",ui.findObject(By.text("播放")).isEnabled());
            assertTrue(ui.takeScreenshot(new java.io.File(context.getExternalFilesDir(null),"toolbar-open.png")));
            ui.findObject(By.text("关闭")).click();
            assertTrue("Toolbar did not close",ui.wait(Until.gone(By.text("录制")),5000));
            android.util.Log.i("HIDMacroAcceptance","MAIN_RECONNECT_ACTIVE_BOARD_TOOLBAR_PASS");
        } catch(AssertionError error) {
            ui.takeScreenshot(new java.io.File(context.getExternalFilesDir(null),"main-screen-failure.png"));
            throw error;
        } finally {
            context.stopService(new Intent(context,MacroService.class));
            instrumentation.getUiAutomation().dropShellPermissionIdentity();
        }
    }
}
