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
    assert(router_command(&r,0,7,keyboard,sizeof(keyboard)) == RESULT_ROLE);
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

static void test_removed_ios_touch_mode(void)
{
    router_t r;
    const uint8_t mode[] = {CMD_TOUCH_MODE,1};
    router_init(&r);
    router_connect(&r,1);
    router_connect(&r,2);
    assert(router_command(&r,1,7,mode,2) == RESULT_ROLE);
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
    test_removed_ios_touch_mode();
    test_ota();
    test_power_loss();
    test_wire();
    puts("ALL CORE TESTS PASSED");
    return 0;
}
