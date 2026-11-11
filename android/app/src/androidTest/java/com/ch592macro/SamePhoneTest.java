package com.ch592macro;

import android.Manifest;
import android.bluetooth.*;
import android.content.Context;
import android.os.SystemClock;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.util.concurrent.atomic.AtomicReference;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class SamePhoneTest {
    @Test public void uploadedMacroReturnsAsHidInputToSamePhone() throws Exception {
        var instrumentation=InstrumentationRegistry.getInstrumentation();
        instrumentation.getUiAutomation().adoptShellPermissionIdentity(Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.BLUETOOTH_SCAN);
        Context context=instrumentation.getTargetContext();
        String address=InstrumentationRegistry.getArguments().getString("board");
        assertNotNull("Pass -e board with the confirmed board address",address);
        BluetoothDevice device=context.getSystemService(BluetoothManager.class).getAdapter().getRemoteDevice(address);
        AtomicReference<InputTestActivity> activity=new AtomicReference<>();
        try(ActivityScenario<InputTestActivity> screen=ActivityScenario.launch(InputTestActivity.class);BleLink link=new BleLink(()->{})) {
            screen.onActivity(activity::set);
            link.connect(context,device);
            byte[] status=link.status();assertEquals("Must share the HID connection",1,status[1]);assertTrue((status[4]&4)!=0);
            Macro macro=new Macro();macro.repeats=1;macro.startDelay=800;macro.steps.add(Macro.Step.line(.5f,.8f,.5f,.25f,300));
            link.write(new byte[]{0x14});
            while(link.status()[5]!=0)SystemClock.sleep(20);
            for(byte[] packet:MacroCodec.upload(MacroCodec.compile(macro)))link.write(packet);
            link.write(MacroCodec.start(1,800));
            long deadline=SystemClock.elapsedRealtime()+12000;
            while(activity.get().gestures<1&&SystemClock.elapsedRealtime()<deadline)SystemClock.sleep(100);
            assertTrue("No physical HID gesture received",activity.get().gestures>=1);
            assertTrue("Unexpected input device: "+activity.get().inputName,activity.get().inputName.toUpperCase().contains("CH592"));
            assertTrue("Must be an upward swipe",activity.get().startY-activity.get().endY>200);
            link.write(new byte[]{0x14});
            android.util.Log.i("HIDMacroAcceptance","SELF_CONTROL_PASS role="+status[1]+" input="+activity.get().inputName+" y="+activity.get().startY+"->"+activity.get().endY);
        } finally { instrumentation.getUiAutomation().dropShellPermissionIdentity(); }
    }
}
