#include "router.h"
#include "layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t sequence[][MACRO_STEP_SIZE] = {
    {10,0,CMD_MOUSE,1,0,0,0,0,0,0,0},
    {10,0,CMD_MOUSE,1,20,0,0xF6,0xFF,0,0,0},
    {10,0,CMD_MOUSE,0,0,0,0,0,0,0,0},
    {10,0,CMD_MOUSE,2,0,0,0,0,0,0,0},
    {10,0,CMD_MOUSE,0,0,0,0,0,0,0,0},
    {10,0,CMD_MOUSE,0,0,0,0,0,1,0xFF,0},
    {10,0,CMD_KEYBOARD,8,0,44,0,0,0,0,0},
    {10,0,CMD_KEYBOARD,0,0,0,0,0,0,0,0},
    {20,0,0,0,0,0,0,0,0,0,0}
};
static const uint8_t start[] = {CMD_MACRO_START,2,0,30,0,0,0,0,0,40,0,0,0};

static void initialize(router_t *r)
{
    router_init(r);
    assert(router_connect(r,1) == ROLE_HID);
    assert(router_connect(r,2) == ROLE_CONTROLLER);
    assert(router_connect(r,3) == ROLE_NONE);
}

static void drain(router_t *r)
{
    while (router_peek(r)) router_sent(r);
}

static void upload(router_t *r)
{
    uint8_t begin[6] = {CMD_MACRO_BEGIN,sizeof(sequence)/MACRO_STEP_SIZE};
    uint8_t data[MACRO_STEP_SIZE+3] = {CMD_MACRO_DATA};
    const uint8_t commit = CMD_MACRO_COMMIT;
    unsigned i;
    store_le32(begin+2,crc32(sequence,sizeof(sequence)));
    assert(router_command(r,2,3,begin,sizeof(begin)) == RESULT_OK);
    for (i=0;i<sizeof(sequence)/MACRO_STEP_SIZE;++i) {
        data[1]=(uint8_t)(i*MACRO_STEP_SIZE);
        data[2]=(uint8_t)((i*MACRO_STEP_SIZE)>>8);
        memcpy(data+3,sequence[i],MACRO_STEP_SIZE);
        assert(router_command(r,2,3,data,sizeof(data)) == RESULT_OK);
        assert(router_command(r,2,3,data,sizeof(data)) == RESULT_OK);
    }
    assert(router_command(r,2,3,&commit,1) == RESULT_OK);
}

static void test_roles_and_validation(void)
{
    router_t r;
    uint8_t begin[] = {CMD_MACRO_BEGIN,1,0,0,0,0};
    uint8_t data[MACRO_STEP_SIZE+3] = {CMD_MACRO_DATA,0,0};
    uint8_t bad_start[sizeof(start)];
    const uint8_t touch[] = {CMD_TOUCH,1,0,0,0,0,0,1};
    const uint8_t commit = CMD_MACRO_COMMIT, stop = CMD_MACRO_STOP;
    initialize(&r);
    assert(router_command(&r,1,3,begin,sizeof(begin)) == RESULT_ROLE);
    assert(router_command(&r,3,3,begin,sizeof(begin)) == RESULT_ROLE);
    assert(router_command(&r,2,3,touch,sizeof(touch)) == RESULT_VALUE);
    assert(router_command(&r,2,3,begin,sizeof(begin)) == RESULT_OK);
    data[5]=CMD_TOUCH;
    assert(router_command(&r,2,3,data,sizeof(data)) == RESULT_VALUE);
    data[5]=CMD_MOUSE;data[6]=1;
    assert(router_command(&r,2,3,data,sizeof(data)) == RESULT_OK);
    assert(router_command(&r,2,3,&commit,1) == RESULT_CRC);
    store_le32(begin+2,crc32(data+3,MACRO_STEP_SIZE));
    assert(router_command(&r,2,3,begin,sizeof(begin)) == RESULT_OK);
    assert(router_command(&r,2,3,data,sizeof(data)) == RESULT_OK);
    assert(router_command(&r,2,3,&commit,1) == RESULT_VALUE);
    assert(router_command(&r,2,3,&stop,1) == RESULT_OK);
    drain(&r);upload(&r);
    memcpy(bad_start,start,sizeof(start));bad_start[5]=1;
    assert(router_command(&r,2,3,bad_start,sizeof(bad_start)) == RESULT_VALUE);
    assert(router_command(&r,2,3,bad_start,9) == RESULT_VALUE);
    bad_start[5]=0;store_le32(bad_start+9,3600001u);
    assert(router_command(&r,2,3,bad_start,sizeof(bad_start)) == RESULT_VALUE);
    assert(r.macro_state == MACRO_READY && !r.count);
    assert(router_command(&r,2,1,start,sizeof(start)) == RESULT_NOT_READY);
    assert(router_command(&r,1,3,start,sizeof(start)) == RESULT_ROLE);
    puts("PASS iOS controller isolation, CRC, held-input validation and unsupported touch/jitter rejection");
}

static void test_replay_and_disconnected_controller(void)
{
    router_t r;
    unsigned ticks, reports=0, index;
    initialize(&r);upload(&r);
    assert(router_command(&r,2,3,start,sizeof(start)) == RESULT_OK);
    router_disconnect(&r,2);
    for (ticks=0;ticks<1000 && r.macro_state==MACRO_RUNNING;++ticks) {
        const hid_report_t *report;
        router_macro_tick(&r,3,5);
        report=router_peek(&r);
        if (!report) continue;
        if (r.macro_state!=MACRO_RUNNING) { drain(&r);break; }
        index=reports%8;
        assert(report->id==sequence[index][2]);
        assert(report->size==(report->id==CMD_KEYBOARD?8:7));
        assert(!memcmp(report->data,sequence[index]+3,report->size));
        ++reports;router_sent(&r);
    }
    assert(reports==16 && r.macro_state==MACRO_READY && !r.macro_held);
    assert(!memcmp(r.macro_data,sequence,sizeof(sequence)));
    assert(router_connect(&r,8)==ROLE_CONTROLLER);
    assert(r.hid==1);
    puts("PASS iOS left/right click, relative drag, wheel/pan, keyboard and timed repeat after controller disconnect");
}

static void test_pause_stop_and_failures(void)
{
    router_t r;
    uint8_t continuous[sizeof(start)];
    const uint8_t pause=CMD_MACRO_PAUSE, resume=CMD_MACRO_RESUME, stop=CMD_MACRO_STOP;
    unsigned i, reports=0;
    initialize(&r);upload(&r);
    memcpy(continuous,start,sizeof(start));continuous[1]=0;
    assert(router_command(&r,2,3,continuous,sizeof(continuous)) == RESULT_OK);
    router_macro_tick(&r,3,5);
    assert(r.macro_delay==25 && !r.count);
    assert(router_command(&r,2,3,&pause,1)==RESULT_OK);
    router_macro_tick(&r,3,5);
    assert(r.macro_state==MACRO_PAUSED && r.macro_delay==25);
    router_macro_tick(&r,3,100);
    assert(r.macro_delay==25 && !r.count);
    assert(router_command(&r,2,3,&resume,1)==RESULT_OK);
    router_macro_tick(&r,3,25);assert(router_peek(&r)->data[0]==1);
    router_sent(&r);
    assert(router_command(&r,2,3,&pause,1)==RESULT_OK);
    for (i=0;i<30 && r.macro_state!=MACRO_PAUSED;++i) {
        router_macro_tick(&r,3,5);
        if (r.count) { assert(router_peek(&r)->id==CMD_MOUSE);++reports;router_sent(&r); }
    }
    assert(reports==2 && r.macro_state==MACRO_PAUSED && !r.macro_held && r.macro_index==3);
    assert(router_command(&r,2,3,&resume,1)==RESULT_OK);
    for (i=0;i<500;++i) { router_macro_tick(&r,3,5);drain(&r); }
    assert(r.macro_state==MACRO_RUNNING && !r.macro_loops);
    assert(router_command(&r,2,3,&stop,1)==RESULT_OK);
    assert(r.macro_state==MACRO_READY && r.count==2);
    assert(router_peek(&r)->id==CMD_KEYBOARD && !router_peek(&r)->data[0]);
    router_sent(&r);
    assert(router_peek(&r)->id==CMD_MOUSE && !router_peek(&r)->data[0]);
    router_sent(&r);
    for (i=0;i<100;++i) router_macro_tick(&r,3,5);
    assert(!r.count && !r.macro_held);
    assert(router_command(&r,2,3,start,sizeof(start))==RESULT_OK);
    router_macro_tick(&r,1,5);
    assert(r.macro_state==MACRO_READY && r.macro_result==RESULT_NOT_READY);
    drain(&r);
    assert(router_command(&r,2,3,start,sizeof(start))==RESULT_OK);
    router_macro_tick(&r,3,30);
    for (i=0;i<1001;++i) router_macro_tick(&r,3,5);
    assert(r.macro_state==MACRO_READY && r.macro_result==RESULT_BUSY);
    drain(&r);
    assert(router_command(&r,2,3,start,sizeof(start))==RESULT_OK);
    router_disconnect(&r,1);
    assert(r.macro_state==MACRO_EMPTY && !r.count && r.controller==2);
    puts("PASS iOS continuous repeat, pause after button release, stop, subscription loss, stall and HID disconnect");
}

int main(void)
{
    assert(!HID_HAS_TOUCH && HID_INPUT_COUNT==2);
    test_roles_and_validation();
    test_replay_and_disconnected_controller();
    test_pause_stop_and_failures();
    puts("ALL IOS MACRO TESTS PASSED");
    return 0;
}
