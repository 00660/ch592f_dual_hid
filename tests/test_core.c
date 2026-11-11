#include "router.h"
#include "ota.h"
#include "storage.h"
#include "wire.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[FLASH_END], nv[0x8000], image[8192 + 128];
static int writes, operation, power_cut = -1;
static jmp_buf reboot;

static void mutate(void *destination, const void *source, uint32_t size, int erase)
{
    uint32_t count = operation++ == power_cut ? size / 2 : size;
    if (erase) memset(destination,0xFF,count);
    else {
        uint8_t *out = destination;
        const uint8_t *in = source;
        uint32_t i;
        for (i = 0; i < count; ++i) {
            assert((out[i] & in[i]) == in[i]);
            out[i] &= in[i];
        }
    }
    if (count != size) longjmp(reboot,1);
}

int storage_erase(uint32_t address, uint32_t size)
{
    assert(address >= APP_BASE && address + size <= IAP_BASE);
    assert(address % ERASE_SIZE == 0 && size % ERASE_SIZE == 0);
    mutate(flash+address,NULL,size,1);
    return 0;
}

int storage_write(uint32_t address, const void *data, uint32_t size)
{
    assert(address >= APP_BASE && address + size <= IAP_BASE);
    assert((address & 3) == 0 && (size & 3) == 0);
    ++writes;
    mutate(flash+address,data,size,0);
    return 0;
}

void storage_read(uint32_t address, void *data, uint32_t size)
{
    assert(address + size <= FLASH_END);
    memcpy(data,flash+address,size);
}

void storage_meta_read(uint32_t address, image_meta_t *meta)
{
    memcpy(meta,nv+address,sizeof(*meta));
}

int storage_meta_clear(uint32_t address)
{
    assert(address == ACTIVE_META || address == PENDING_META);
    mutate(nv+address,NULL,META_PAGE_SIZE,1);
    return 0;
}

int storage_meta_write(uint32_t address, const image_meta_t *meta)
{
    storage_meta_clear(address);
    mutate(nv+address,meta,sizeof(*meta),0);
    return 0;
}

uint32_t storage_crc(uint32_t address, uint32_t size)
{
    return crc32(flash+address,size);
}

static void reset_storage(void)
{
    unsigned i;
    memset(flash,0xFF,sizeof(flash));
    memset(nv,0xFF,sizeof(nv));
    for (i = 0; i < sizeof(image); ++i) image[i] = (uint8_t)(i * 73 + i / 257);
    writes = operation = 0;
    power_cut = -1;
}

static void begin(ota_t *o, uint16_t owner, uint32_t size, uint32_t crc)
{
    uint8_t command[17] = {OTA_BEGIN};
    store_le32(command+1,size);
    store_le32(command+5,crc);
    store_le32(command+9,FW_VERSION);
    store_le32(command+13,BOARD_ID);
    assert(ota_command(o,owner,command,sizeof(command)) == RESULT_OK);
}

static void finish_erase(ota_t *o)
{
    while (o->state == OTA_ERASING) ota_poll(o);
    assert(o->state == OTA_RECEIVING);
}

static void upload(ota_t *o, uint16_t owner, const uint8_t *data, unsigned size)
{
    unsigned offset;
    uint8_t packet[OTA_MAX_PACKET];
    packet[0] = OTA_DATA;
    for (offset = 0; offset < size; offset += OTA_MAX_DATA) {
        unsigned count = size-offset > OTA_MAX_DATA ? OTA_MAX_DATA : size-offset;
        store_le32(packet+1,offset);
        memcpy(packet+5,data+offset,count);
        assert(ota_command(o,owner,packet,count+5) == RESULT_OK);
    }
}

static void test_roles_and_reports(void)
{
    router_t r;
    const uint8_t keyboard[] = {1,2,0,4,0,0,0,0,0};
    const uint8_t mouse[] = {2,1,0x34,0x12,0xFE,0xFF,1,0};
    const uint8_t touch[] = {3,1,0,0xFF,0x7F,0x00,0x40,1};
    uint8_t bad[8];
    unsigned i;
    router_init(&r);
    assert(router_connect(&r,0) == ROLE_HID);
    assert(router_connect(&r,17) == ROLE_CONTROLLER);
    assert(router_connect(&r,22) == ROLE_NONE);
    assert(router_connect(&r,17) == ROLE_CONTROLLER);
    assert(router_command(&r,0,7,keyboard,sizeof(keyboard)) == RESULT_OK);
    assert(router_peek(&r)->id == CMD_KEYBOARD);
    router_pop(&r);
    assert(router_command(&r,22,7,keyboard,sizeof(keyboard)) == RESULT_ROLE);
    assert(router_command(&r,17,0,keyboard,sizeof(keyboard)) == RESULT_NOT_READY);
    assert(router_command(&r,17,7,keyboard,sizeof(keyboard)-1) == RESULT_LENGTH);
    assert(router_command(&r,17,7,keyboard,sizeof(keyboard)) == RESULT_OK);
    assert(router_command(&r,17,7,mouse,sizeof(mouse)) == RESULT_OK);
    assert(router_command(&r,17,7,touch,sizeof(touch)) == RESULT_OK);
    assert(router_peek(&r)->id == 1 && router_peek(&r)->data[2] == 4);
    router_sent(&r);
    assert(router_peek(&r)->id == 2 && router_peek(&r)->data[1] == 0x34);
    router_sent(&r);
    assert(router_peek(&r)->id == 3 && router_peek(&r)->size == 7 &&
           !memcmp(router_peek(&r)->data,touch+1,7));
    memcpy(bad,touch,sizeof(bad));
    bad[4] = 0x80;
    assert(router_command(&r,17,7,bad,sizeof(bad)) == RESULT_VALUE);
    memcpy(bad,touch,sizeof(bad));
    bad[2] = 1;
    assert(router_command(&r,17,7,bad,sizeof(bad)) == RESULT_VALUE);
    router_disconnect(&r,0);
    assert(r.controller == 17 && r.hid == NO_CONNECTION && !r.count);
    assert(router_connect(&r,31) == ROLE_HID);
    for (i = 0; i < REPORT_QUEUE_SIZE; ++i)
        assert(router_command(&r,17,7,keyboard,sizeof(keyboard)) == RESULT_OK);
    assert(router_command(&r,17,7,keyboard,sizeof(keyboard)) == RESULT_BUSY);
    router_disconnect(&r,17);
    assert(r.hid == 31 && r.count == 2);
    for (i = 1; i <= 2; ++i) {
        static const uint8_t zero[8] = {0};
        const hid_report_t *report = router_peek(&r);
        assert(report->id == i && !memcmp(report->data,zero,report->size));
        router_sent(&r);
    }
    router_connect(&r,40);
    assert(router_command(&r,40,7,touch,sizeof(touch)) == RESULT_OK);
    router_sent(&r);
    assert(r.touch_sent[0] == 1);
    router_release(&r);
    router_sent(&r);
    router_sent(&r);
    assert(router_peek(&r)->id == CMD_TOUCH && router_peek(&r)->data[0] == 0 &&
           !memcmp(router_peek(&r)->data+1,touch+2,6));
    router_sent(&r);
    assert(!router_peek(&r) && !r.touch_sent[0]);
    assert(router_command(&r,40,7,touch,sizeof(touch)) == RESULT_OK);
    memcpy(bad,touch,sizeof(bad));
    bad[1] = 0;
    bad[3] = bad[4] = bad[5] = bad[6] = 0;
    assert(router_command(&r,40,7,bad,sizeof(bad)) == RESULT_OK);
    router_sent(&r);
    assert(router_peek(&r)->id == CMD_TOUCH && router_peek(&r)->data[0] == 0 &&
           !memcmp(router_peek(&r)->data+1,touch+2,6));
    puts("PASS roles, native touch, disconnect release and queue pressure");
}

static void test_macro(void)
{
    router_t r;
    uint8_t records[2 * MACRO_STEP_SIZE] = {
        10,0,CMD_TOUCH,1,0,0x00,0x20,0x00,0x40,1,0,
        0,0,CMD_TOUCH,0,0,0x00,0x20,0x00,0x40,1,0
    };
    uint8_t begin[6] = {CMD_MACRO_BEGIN,2,0,0,0,0};
    uint8_t data[MACRO_STEP_SIZE + 3] = {CMD_MACRO_DATA,0,0};
    const uint8_t commit[] = {CMD_MACRO_COMMIT};
    const uint8_t start[] = {CMD_MACRO_START,2,0};
    const uint8_t stop[] = {CMD_MACRO_STOP};
    const uint8_t pause[] = {CMD_MACRO_PAUSE}, resume[] = {CMD_MACRO_RESUME};
    const uint8_t delayed[] = {CMD_MACRO_START,1,0,100,0};
    unsigned i, guard, touches;

    router_init(&r);
    router_connect(&r,4);
    router_connect(&r,9);
    store_le32(begin + 2,crc32(records,sizeof(records)));
    assert(router_command(&r,4,7,begin,sizeof(begin)) == RESULT_OK);
    assert(router_command(&r,9,7,begin,sizeof(begin)) == RESULT_BUSY);
    assert(router_command(&r,9,7,commit,sizeof(commit)) == RESULT_STATE);
    store_le32(begin + 2,crc32(records,sizeof(records)) + 1);
    assert(router_command(&r,4,7,begin,sizeof(begin)) == RESULT_OK);
    for (i = 0; i < 2; ++i) {
        data[1] = (uint8_t)(i * MACRO_STEP_SIZE);
        memcpy(data + 3,records + i * MACRO_STEP_SIZE,MACRO_STEP_SIZE);
        assert(router_command(&r,4,7,data,sizeof(data)) == RESULT_OK);
        assert(router_command(&r,9,7,data,sizeof(data)) == RESULT_BUSY);
        assert(router_command(&r,4,7,data,sizeof(data)) == RESULT_OK);
        assert(r.macro_received == i + 1);
        data[3] ^= 1;
        assert(router_command(&r,4,7,data,sizeof(data)) == RESULT_OFFSET);
        data[3] ^= 1;
    }
    assert(router_command(&r,4,7,commit,sizeof(commit)) == RESULT_CRC);
    store_le32(begin + 2,crc32(records,sizeof(records)));
    assert(router_command(&r,4,7,begin,sizeof(begin)) == RESULT_OK);
    for (i = 0; i < 2; ++i) {
        data[1] = (uint8_t)(i * MACRO_STEP_SIZE);
        memcpy(data + 3,records + i * MACRO_STEP_SIZE,MACRO_STEP_SIZE);
        assert(router_command(&r,4,7,data,sizeof(data)) == RESULT_OK);
    }
    assert(router_command(&r,4,7,commit,sizeof(commit)) == RESULT_OK);
    assert(router_command(&r,4,3,start,sizeof(start)) == RESULT_NOT_READY);
    assert(router_command(&r,4,7,delayed,sizeof(delayed)) == RESULT_OK);
    router_macro_tick(&r,7,50);
    assert(!r.count && r.macro_delay == 50);
    assert(router_command(&r,4,7,pause,sizeof(pause)) == RESULT_OK);
    router_macro_tick(&r,7,5);
    assert(r.macro_state == MACRO_PAUSED && r.macro_delay == 50);
    router_macro_tick(&r,7,100);
    assert(!r.count && r.macro_delay == 50);
    assert(router_command(&r,4,7,resume,sizeof(resume)) == RESULT_OK);
    router_macro_tick(&r,7,50);
    assert(r.count == 1 && router_peek(&r)->data[0] == 1);
    router_sent(&r);
    assert(router_command(&r,4,7,pause,sizeof(pause)) == RESULT_OK);
    for (guard = 0; guard < 10 && r.macro_state != MACRO_PAUSED; ++guard) {
        router_macro_tick(&r,7,5);
        if (r.count) router_sent(&r);
    }
    assert(r.macro_state == MACRO_PAUSED && !r.touch_sent[0] && !r.count);
    assert(router_command(&r,4,7,stop,sizeof(stop)) == RESULT_OK);
    while (router_peek(&r)) router_sent(&r);
    assert(router_command(&r,4,7,start,sizeof(start)) == RESULT_OK);
    router_disconnect(&r,9);
    assert(r.macro_state == MACRO_RUNNING);
    touches = 0;
    for (guard = 0; r.macro_state == MACRO_RUNNING && guard < 100; ++guard) {
        router_macro_tick(&r,7,5);
        if (router_peek(&r)) {
            if (router_peek(&r)->id == CMD_TOUCH) ++touches;
            router_sent(&r);
        }
    }
    assert(r.macro_state == MACRO_READY && touches == 4);
    while (router_peek(&r)) router_sent(&r);
    assert(router_command(&r,4,7,start,sizeof(start)) == RESULT_OK);
    router_macro_tick(&r,7,5);
    assert(router_peek(&r)->id == CMD_TOUCH && router_peek(&r)->data[0] == 1);
    router_sent(&r);
    assert(router_command(&r,4,7,stop,sizeof(stop)) == RESULT_OK);
    assert(r.macro_state == MACRO_READY && r.touch_sent[0] == 1);
    while (router_peek(&r)) router_sent(&r);
    assert(!r.touch_sent[0] && !r.count);
    assert(router_command(&r,4,7,start,sizeof(start)) == RESULT_OK);
    router_macro_tick(&r,7,5);
    for (guard = 0; guard < 1001 && r.macro_state == MACRO_RUNNING; ++guard)
        router_macro_tick(&r,7,5);
    assert(r.macro_state == MACRO_READY && r.macro_result == RESULT_BUSY);
    while (router_peek(&r)) router_sent(&r);
    router_disconnect(&r,4);
    assert(r.macro_state == MACRO_EMPTY);
    puts("PASS macro ownership, retransmit, CRC, delay, pause/release, repeat, disconnect and stall");
}

static void test_macro_jitter_and_loop_delay(void)
{
    router_t r;
    uint8_t records[3 * MACRO_STEP_SIZE] = {
        5,0,CMD_TOUCH,1,0,0,0,100,0,1,0,
        5,0,CMD_TOUCH,1,0,0xF8,0x7F,200,0,1,0,
        5,0,CMD_TOUCH,0,0,0xF8,0x7F,200,0,1,0
    };
    uint8_t begin[] = {CMD_MACRO_BEGIN,3,0,0,0,0};
    uint8_t data[14] = {CMD_MACRO_DATA,0,0};
    uint8_t start[] = {CMD_MACRO_START,4,0,0,0,0x88,0x13,0xE8,3,20,0,0,0};
    const uint8_t legacy_start[] = {CMD_MACRO_START,1,0,0,0,0,0,0,0};
    const uint8_t commit = CMD_MACRO_COMMIT;
    int x_offset = 0, y_offset = 0, previous_x = -10000, previous_y = -10000, varied = 0;
    unsigned i, reports = 0, guard;
    int pause_checked = 0;
    router_init(&r);router_connect(&r,1);
    store_le32(begin+2,crc32(records,sizeof(records)));
    assert(router_command(&r,1,7,begin,sizeof(begin)) == RESULT_OK);
    for (i = 0; i < 3; ++i) {
        data[1] = i * MACRO_STEP_SIZE;memcpy(data+3,records+i*MACRO_STEP_SIZE,MACRO_STEP_SIZE);
        assert(router_command(&r,1,7,data,sizeof(data)) == RESULT_OK);
    }
    assert(router_command(&r,1,7,&commit,1) == RESULT_OK);
    store_le32(start + 9,3600001u);
    assert(router_command(&r,1,7,start,sizeof(start)) == RESULT_VALUE);
    store_le32(start + 9,20);
    start[5] = 0xFF;
    start[6] = 0xFF;
    assert(router_command(&r,1,7,start,sizeof(start)) == RESULT_VALUE);
    start[5] = 0x88;
    start[6] = 0x13;
    assert(router_command(&r,1,7,start,sizeof(start)) == RESULT_OK);
    assert(r.macro_loop_delay == 20);
    for (guard = 0; guard < 200 && r.macro_state == MACRO_RUNNING; ++guard) {
        const hid_report_t *report;
        router_macro_tick(&r,7,5);
        if (!pause_checked && reports == 3 && r.macro_index == 0 && r.macro_delay == 20) {
            assert(router_command(&r,1,7,(const uint8_t[]){CMD_MACRO_PAUSE},1) == RESULT_OK);
            router_macro_tick(&r,7,5);
            assert(r.macro_state == MACRO_PAUSED && r.macro_delay == 20);
            router_macro_tick(&r,7,100);
            assert(r.macro_delay == 20);
            assert(router_command(&r,1,7,(const uint8_t[]){CMD_MACRO_RESUME},1) == RESULT_OK);
            for (i = 0; i < 3; ++i) {
                router_macro_tick(&r,7,5);
                assert(!r.count);
            }
            assert(r.macro_delay == 5);
            router_macro_tick(&r,7,5);
            assert(r.count == 1);
            pause_checked = 1;
        }
        while ((report=router_peek(&r)) != NULL) {
            if (report->id == CMD_TOUCH) {
                int x=report->data[2]+256*report->data[3], y=report->data[4]+256*report->data[5];
                if (reports % 3 == 0) {
                    x_offset=x;y_offset=y-100;
                    assert(x_offset>=0 && x_offset<=7 && y_offset>=-100 && y_offset<=1000);
                    if (reports && (previous_x!=x_offset || previous_y!=y_offset)) varied=1;
                    previous_x=x_offset;previous_y=y_offset;
                } else assert(x==32760+x_offset && y==200+y_offset);
                assert(x>=0 && x<=32767 && y>=0 && y<=32767);
                ++reports;
            }
            router_sent(&r);
        }
    }
    assert(reports==12 && varied && pause_checked && r.macro_state==MACRO_READY);
    assert(!memcmp(records,r.macro_data,sizeof(records)));
    assert(router_command(&r,1,7,legacy_start,sizeof(legacy_start))==RESULT_OK);
    assert(r.macro_loop_delay==0);
    assert(r.macro_dx==0 && r.macro_dy==0);
    puts("PASS loop jitter, 32-bit interval, pause retention, edge bounds and legacy START compatibility");
}

static void test_removed_ios_touch_mode(void)
{
    router_t r;
    const uint8_t mode[] = {CMD_TOUCH_MODE,1};
    router_init(&r);
    router_connect(&r,1);
    router_connect(&r,2);
    assert(router_command(&r,1,7,mode,2) == RESULT_VALUE);
    assert(router_command(&r,2,7,mode,2) == RESULT_VALUE);
    assert(!r.count);
    puts("PASS removed iOS touch conversion is rejected");
}

static void test_ota(void)
{
    ota_t o;
    image_meta_t meta;
    uint8_t packet[17] = {OTA_BEGIN};
    uint8_t commit = OTA_COMMIT, abort = OTA_ABORT;
    unsigned i;
    int count;
    reset_storage();
    ota_init(&o);
    assert(crc32("123456789",9) == 0xCBF43926u);
    store_le32(packet+1,APP_CAPACITY+4);
    store_le32(packet+13,BOARD_ID);
    assert(ota_command(&o,2,packet,17) == RESULT_VALUE);
    assert(operation == 0);
    begin(&o,2,sizeof(image),crc32(image,sizeof(image)));
    assert(ota_command(&o,3,&abort,1) == RESULT_BUSY);
    finish_erase(&o);
    assert(ota_command(&o,2,&commit,1) == RESULT_STATE);
    upload(&o,2,image,sizeof(image));
    count = writes;
    packet[0] = OTA_DATA;
    store_le32(packet+1,0);
    memcpy(packet+5,image,12);
    assert(ota_command(&o,2,packet,17) == RESULT_OK && writes == count);
    packet[5] ^= 1;
    assert(ota_command(&o,2,packet,17) == RESULT_OFFSET);
    store_le32(packet+1,0xFFFFFFFCu);
    assert(ota_command(&o,2,packet,17) == RESULT_OFFSET);
    assert(ota_command(&o,2,&commit,1) == RESULT_OK);
    while (o.state == OTA_VERIFYING) ota_poll(&o);
    assert(o.state == OTA_READY);
    storage_meta_read(PENDING_META,&meta);
    assert(meta_valid(&meta));
    ota_disconnect(&o,2);
    assert(o.state == OTA_READY);
    assert(boot_install_pending() == 1 && boot_active_valid());
    assert(!memcmp(flash+APP_BASE,image,sizeof(image)));
    storage_meta_read(PENDING_META,&meta);
    assert(!meta_valid(&meta));

    ota_init(&o);
    begin(&o,OTA_OWNER_UART,sizeof(image),0);
    finish_erase(&o);
    upload(&o,OTA_OWNER_UART,image,sizeof(image));
    assert(ota_command(&o,OTA_OWNER_UART,&commit,1) == RESULT_OK);
    while (o.state == OTA_VERIFYING) ota_poll(&o);
    assert(o.state == OTA_FAILED && o.result == RESULT_CRC);
    assert(boot_active_valid());
    ota_disconnect(&o,OTA_OWNER_UART);
    assert(o.state == OTA_IDLE && o.owner == OTA_OWNER_NONE);
    for (i = 0; i < APP_BASE; ++i) assert(flash[i] == 0xFF);
    for (i = IAP_BASE; i < FLASH_END; ++i) assert(flash[i] == 0xFF);
    puts("PASS OTA bounds, transport ownership, duplicate packets, CRC and commit");
}

static void test_power_loss(void)
{
    image_meta_t meta;
    int total, cut;
    reset_storage();
    memcpy(flash+STAGING_BASE,image,sizeof(image));
    meta_make(&meta,sizeof(image),crc32(image,sizeof(image)),FW_VERSION);
    memcpy(nv+PENDING_META,&meta,sizeof(meta));
    assert(boot_install_pending() == 1);
    total = operation;
    for (cut = 0; cut < total; ++cut) {
        reset_storage();
        memset(flash+APP_BASE,0x5A,sizeof(image));
        memcpy(flash+STAGING_BASE,image,sizeof(image));
        memcpy(nv+PENDING_META,&meta,sizeof(meta));
        power_cut = cut;
        if (setjmp(reboot) == 0) {
            boot_install_pending();
            assert(0);
        }
        power_cut = -1;
        boot_install_pending();
        assert(boot_active_valid());
        assert(!memcmp(flash+APP_BASE,image,sizeof(image)));
    }
    reset_storage();
    memcpy(nv+PENDING_META,&meta,sizeof(meta));
    assert(boot_install_pending() == -1 && operation == 0);
    printf("PASS power loss at all %d erase/write/metadata operations; bad staging rejected\n",total);
}

static void test_wire(void)
{
    wire_t w;
    uint8_t payload[OTA_MAX_PACKET], frame[OTA_MAX_PACKET+8];
    unsigned i, size;
    int result = 0;
    for (i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
    wire_reset(&w);
    assert(wire_feed(&w,0x55) == 0);
    size = wire_encode(frame,payload,sizeof(payload));
    for (i = 0; i < size; ++i) result = wire_feed(&w,frame[i]);
    assert(result == sizeof(payload) && !memcmp(w.bytes+4,payload,sizeof(payload)));
    frame[size-1] ^= 1;
    for (i = 0; i < size; ++i) result = wire_feed(&w,frame[i]);
    assert(result == -1);
    frame[2] = frame[3] = 0xFF;
    for (i = 0; i < 4; ++i) result = wire_feed(&w,frame[i]);
    assert(result == -1 && !w.position);
    puts("PASS UART framing, CRC, noise and oversized frames");
}

int main(void)
{
    test_roles_and_reports();
    test_macro();
    test_macro_jitter_and_loop_delay();
    test_removed_ios_touch_mode();
    test_ota();
    test_power_loss();
    test_wire();
    puts("ALL CORE TESTS PASSED");
    return 0;
}
