package com.ch592macro;

import org.junit.Test;
import java.util.List;
import java.util.zip.CRC32;
import static org.junit.Assert.*;

public class MacroCodecTest {
    private static Macro sample() {
        Macro macro=new Macro();macro.steps.add(Macro.Step.line(.5f,.8f,.5f,.25f,300));macro.steps.add(Macro.Step.waitFor(15000));return macro;
    }
    @Test public void swipeKeepsContactAndReleasesAtEndpoint() {
        Macro m=sample();byte[] bytes=MacroCodec.compile(m);
        int records=bytes.length/MacroCodec.RECORD_SIZE;
        assertEquals(18,records);
        assertEquals(3,bytes[2]);assertEquals(1,bytes[3]);
        assertEquals(16384,MacroCodec.u16(bytes,5));assertEquals(26214,MacroCodec.u16(bytes,7));
        int up=(records-2)*MacroCodec.RECORD_SIZE;
        assertEquals(0,bytes[up+3]);assertEquals(8192,MacroCodec.u16(bytes,up+7));
        assertEquals(15000,MacroCodec.u16(bytes,up+MacroCodec.RECORD_SIZE));
    }
    @Test public void uploadMatchesFirmwareLayoutAndCrc() {
        byte[] bytes=MacroCodec.compile(sample());List<byte[]> packets=MacroCodec.upload(bytes);
        CRC32 crc=new CRC32();crc.update(bytes);
        assertEquals(crc.getValue(),MacroCodec.u32(packets.get(0),2));
        for(int i=1;i<packets.size()-1;i++) {
            assertEquals(14,packets.get(i).length);assertEquals((i-1)*11,MacroCodec.u16(packets.get(i),1));
            for(int j=0;j<11;j++)assertEquals(bytes[(i-1)*11+j],packets.get(i)[j+3]);
        }
        assertArrayEquals(new byte[]{0x12},packets.get(packets.size()-1));
    }
    @Test public void startDelayIsSeparateFromRepeatingRecords() {
        Macro m=sample();byte[] before=MacroCodec.compile(m);m.startDelay=30000;
        assertArrayEquals(before,MacroCodec.compile(m));assertArrayEquals(new byte[]{0x13,10,0,0x30,0x75},MacroCodec.start(10,30000));
    }
    @Test public void preservesCurvedPath() {
        Macro m=new Macro();Macro.Step s=Macro.Step.line(.1f,.9f,.9f,.1f,320);
        s.points.add(1,new Macro.Point(.1f,.1f,160));m.steps.add(s);
        byte[] bytes=MacroCodec.compile(m);int middle=8*11;
        assertEquals(3277,MacroCodec.u16(bytes,middle+5));assertEquals(3277,MacroCodec.u16(bytes,middle+7));
    }
    @Test(expected=IllegalArgumentException.class) public void refusesOverflow() {
        Macro m=sample();for(int i=0;i<6;i++)m.steps.add(Macro.Step.line(.5f,.8f,.5f,.2f,500));MacroCodec.compile(m);
    }
    @Test(expected=IllegalArgumentException.class) public void refusesInvalidCoordinate() {
        Macro m=new Macro();m.steps.add(Macro.Step.line(Float.NaN,.2f,.3f,.4f,200));MacroCodec.compile(m);
    }
    @Test public void splitsLongWaitWithoutIntegerOverflow() {
        Macro m=sample();m.steps.get(1).duration=70000;byte[] bytes=MacroCodec.compile(m);
        assertEquals(65535,MacroCodec.u16(bytes,17*11));assertEquals(4465,MacroCodec.u16(bytes,18*11));
    }
    @Test public void configurableLoopAndPixelOffsets() {
        Macro macro=sample();macro.repeats=0;macro.loopDelay=2400;macro.screenWidth=721;macro.screenHeight=1601;macro.jitterX=10;macro.jitterY=20;
        byte[] records=MacroCodec.compile(macro),start=MacroCodec.start(macro);
        assertEquals(18,records.length/MacroCodec.RECORD_SIZE);
        assertEquals(13,start.length);assertEquals(0,MacroCodec.u16(start,1));assertEquals(455,MacroCodec.u16(start,5));assertEquals(410,MacroCodec.u16(start,7));
        assertEquals(2400,MacroCodec.u32(start,9));
        macro.repeats=17;assertEquals(17,MacroCodec.u16(MacroCodec.start(macro),1));
        macro.jitterX=macro.jitterY=macro.loopDelay=0;assertEquals(5,MacroCodec.start(macro).length);
    }
}
