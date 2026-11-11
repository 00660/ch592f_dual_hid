package com.ch592macro;

import android.Manifest;
import android.content.*;
import android.os.*;
import android.provider.Settings;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class RecordingFlowTest {
    private static void await(BooleanSupplier condition, int seconds, String message) {
        long end=SystemClock.elapsedRealtime()+seconds*1000L;
        while(!condition.getAsBoolean() && SystemClock.elapsedRealtime()<end) SystemClock.sleep(100);
        assertTrue(message,condition.getAsBoolean());
    }

    @Test public void overlayRecordsAndBoardReplaysThenStops() throws Exception {
        var instrumentation=InstrumentationRegistry.getInstrumentation();
        instrumentation.getUiAutomation().adoptShellPermissionIdentity(Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.POST_NOTIFICATIONS);
        Context context=instrumentation.getTargetContext();
        assertTrue("Grant SYSTEM_ALERT_WINDOW appop before running",Settings.canDrawOverlays(context));
        String address=InstrumentationRegistry.getArguments().getString("board");
        assertNotNull(address);
        CompletableFuture<MacroService> bound=new CompletableFuture<>();
        ServiceConnection connection=new ServiceConnection() {
            public void onServiceConnected(ComponentName n,IBinder b){bound.complete(((MacroService.LocalBinder)b).service());}
            public void onServiceDisconnected(ComponentName n){}
        };
        AtomicReference<InputTestActivity> target=new AtomicReference<>();
        MacroService service=null;
        boolean attached=false;
        try(ActivityScenario<InputTestActivity> screen=ActivityScenario.launch(InputTestActivity.class)) {
            screen.onActivity(target::set);
            context.startForegroundService(new Intent(context,MacroService.class));
            attached=context.bindService(new Intent(context,MacroService.class),connection,Context.BIND_AUTO_CREATE);
            service=bound.get(5,TimeUnit.SECONDS);
            final MacroService board=service;
            instrumentation.runOnMainSync(()->board.connect(address));
            await(()->board.connected&&!board.busy,45,"Same-phone service did not connect");
            int before=MacroStore.load(context).size();
            instrumentation.runOnMainSync(board::showToolbar);
            UiDevice ui=UiDevice.getInstance(instrumentation);
            assertTrue(ui.wait(Until.hasObject(By.text("录制")),5000));ui.findObject(By.text("录制")).click();
            assertTrue(ui.wait(Until.hasObject(By.text("完成录制")),5000));
            android.graphics.Rect finishBounds=ui.findObject(By.text("完成录制")).getVisibleBounds();
            assertFalse("Recording must not show the expanded panel",ui.hasObject(By.text("选择宏")));
            int x=ui.getDisplayWidth()/2;
            for(int index=0;index<3;index++){
                final int expected=index+1;
                await(board::recordingReady,8,"Recording did not rearm automatically");
                // These inputs emulate the user's recording gestures, not the board's HID output.
                if(index==1)ui.click(x,ui.getDisplayHeight()*7/10);
                else ui.swipe(x,ui.getDisplayHeight()*(index==0?4:3)/5,x,ui.getDisplayHeight()*(index==0?3:4)/5,60);
                long deadline=SystemClock.elapsedRealtime()+15000;
                while((target.get().gestures<expected||!board.recordingReady())&&SystemClock.elapsedRealtime()<deadline){
                    UiObject2 finish=ui.findObject(By.text("完成录制"));
                    assertNotNull("Finish recording disappeared during a preview",finish);
                    assertTrue("Finish recording must remain available",finish.isEnabled());
                    assertEquals("Recording toolbar moved during a preview",finishBounds,finish.getVisibleBounds());
                    assertFalse("A preview expanded the toolbar",ui.hasObject(By.text("选择宏")));
                    SystemClock.sleep(100);
                }
                assertEquals("Expected one HID gesture for each recorded action",expected,target.get().gestures);
                assertTrue("Recording did not resume after preview",board.recordingReady());
                assertEquals(expected,board.recordedGestures());
                assertEquals("A partial recording was saved before Finish",before,MacroStore.load(context).size());
                assertTrue("Replay must come from the physical board: "+target.get().inputName,target.get().inputName.contains("CH592"));
                if(index==0)assertTrue(target.get().startY-target.get().endY>200);
                if(index==1)assertTrue(Math.abs(target.get().startY-target.get().endY)<3);
                if(index==2)assertTrue(target.get().endY-target.get().startY>200);
            }
            assertTrue(ui.takeScreenshot(new java.io.File(context.getExternalFilesDir(null),"continuous-recording.png")));
            ui.findObject(By.text("完成录制")).click();
            await(()->MacroStore.load(context).size()==before+1,5,"Recorded macro was not saved");
            Macro recorded=MacroStore.load(context).get(before);
            assertEquals(3,recorded.steps.stream().filter(step->!step.wait).count());
            assertFalse(board.isRecording());
            assertFalse(ui.hasObject(By.text("完成录制")));
            recorded.repeats=0;recorded.startDelay=300;recorded.loopDelay=800;recorded.jitterX=8;recorded.jitterY=8;
            instrumentation.runOnMainSync(()->{board.selectMacro(recorded,before);board.saveSelectedMacro();});
            try(ActivityScenario<InputTestActivity> replay=ActivityScenario.launch(InputTestActivity.class)) {
                replay.onActivity(target::set);
                ui.findObject(By.text("播放")).click();
                await(()->target.get().gestures>=6,30,"Continuous playback did not repeat the entire recording");
                assertTrue(target.get().inputName.contains("CH592"));
                ui.findObject(By.text("暂停")).click();
                await(()->board.macroState!=null&&board.macroState[1]==4,8,"Pause did not settle");
                int count=target.get().gestures;SystemClock.sleep(1800);assertEquals(count,target.get().gestures);
                assertTrue(ui.wait(Until.hasObject(By.text("继续")),3000));ui.findObject(By.text("继续")).click();
                await(()->target.get().gestures>count,8,"Resume did not produce HID input");
                ui.findObject(By.text("停止")).click();
                await(()->!board.busy&&board.macroState!=null&&board.macroState[1]==2,8,"Notification stop action did not stop");
                SystemClock.sleep(500);int stopped=target.get().gestures;
                SystemClock.sleep(1800);assertEquals(stopped,target.get().gestures);
            }
            android.util.Log.i("HIDMacroAcceptance","CONTINUOUS_RECORD_THREE_ACTIONS_LOOP_JITTER_PAUSE_STOP_PASS");
        } finally {
            if(service!=null){MacroService board=service;instrumentation.runOnMainSync(board::stopMacro);SystemClock.sleep(500);}
            if(attached)context.unbindService(connection);
            context.stopService(new Intent(context,MacroService.class));
            instrumentation.getUiAutomation().dropShellPermissionIdentity();
        }
    }
}
