#include "CONFIG.h"
#include "ble_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

router_t app_router;
ota_t app_ota;
const uint8_t primaryServiceUUID[2] = {0,0x28};
const uint8_t characterUUID[2] = {3,0x28};
const uint8_t clientCharCfgUUID[2] = {2,0x29};
static unsigned notification_count, ota_calls;
static uint16_t notification_connection, notification_handle;
static uint8_t notification_bytes[MAX_HID_REPORT_SIZE], notification_length, fail_notification, encrypted = 1;

int diag_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

bStatus_t GATTServApp_RegisterService(gattAttribute_t *attrs, uint16_t count,
                                    uint8_t key_size, gattServiceCBs_t *callbacks)
{
    static uint16_t next_handle = 1;
    uint16_t i;
    (void)callbacks;
    assert(key_size == 16);
    for (i = 0; i < count; ++i) attrs[i].handle = next_handle++;
    return SUCCESS;
}

void GATTServApp_InitCharCfg(uint16_t handle, gattCharCfg_t *cfg)
{
    unsigned i;
    for (i = 0; i < GATT_MAX_NUM_CONN; ++i)
        if (handle == INVALID_CONNHANDLE || cfg[i].connHandle == handle) {
            cfg[i].connHandle = INVALID_CONNHANDLE;
            cfg[i].value = 0;
        }
}

uint16_t GATTServApp_ReadCharCfg(uint16_t handle, gattCharCfg_t *cfg)
{
    unsigned i;
    for (i = 0; i < GATT_MAX_NUM_CONN; ++i) if (cfg[i].connHandle == handle) return cfg[i].value;
    return 0;
}

bStatus_t GATTServApp_ProcessCCCWriteReq(uint16_t handle, gattAttribute_t *attr,
                                        uint8_t *p, uint16_t size, uint16_t offset, uint16_t mask)
{
    unsigned i;
    gattCharCfg_t *cfg = (gattCharCfg_t *)attr->pValue;
    assert(offset == 0 && mask == GATT_CLIENT_CFG_NOTIFY);
    if (size != 2) return ATT_ERR_INVALID_VALUE_SIZE;
    if (p[0] > 1 || p[1]) return ATT_ERR_INVALID_VALUE;
    for (i = 0; i < GATT_MAX_NUM_CONN; ++i)
        if (cfg[i].connHandle == handle || cfg[i].connHandle == INVALID_CONNHANDLE) {
            cfg[i].connHandle = handle;
            cfg[i].value = p[0];
            return SUCCESS;
        }
    return FAILURE;
}

uint8_t linkDB_State(uint16_t handle, uint8_t state)
{
    return handle == app_router.hid && state == LINK_ENCRYPTED && encrypted;
}

void *GATT_bm_alloc(uint16_t handle, uint8_t opcode, uint16_t size, uint16_t *allocated, uint8_t flag)
{
    (void)handle; (void)opcode; (void)allocated; (void)flag;
    return malloc(size);
}

bStatus_t GATT_Notification(uint16_t handle, attHandleValueNoti_t *p, uint8_t authenticated)
{
    (void)authenticated;
    if (fail_notification) return blePending;
    ++notification_count;
    notification_connection = handle;
    notification_handle = p->handle;
    notification_length = p->len;
    memcpy(notification_bytes,p->pValue,p->len);
    free(p->pValue);
    return SUCCESS;
}

void GATT_bm_free(gattMsg_t *message, uint8_t opcode)
{
    (void)opcode;
    free(((attHandleValueNoti_t *)message)->pValue);
}

uint8_t ota_command(ota_t *o, uint16_t owner, const uint8_t *data, uint16_t size)
{
    (void)o; (void)owner; (void)data; (void)size;
    ++ota_calls;
    return RESULT_OK;
}

void ota_status(const ota_t *o, uint8_t status[20])
{
    (void)o;
    memset(status,0,20);
}

/* Exercise the real callbacks and tables against the official WCH API types. */
#include "../src/services.c"

int main(int argc, char **argv)
{
    uint8_t enable[] = {1,0}, out[64], key[] = {1,0,0,4,0,0,0,0,0}, ota[] = {OTA_INFO};
    uint8_t mouse[] = {CMD_MOUSE,1,0xFE,0xFF,3,0,0,0};
    uint8_t touch[] = {CMD_TOUCH,1,0,0xFF,0x7F,0xFF,0x7F,1};
    uint8_t mode[] = {CMD_TOUCH_MODE,1};
    uint16_t length;
    const uint8_t address[] = {0x66,0x55,0x44,0x33,0x22,0x11};
    unsigned i, button;
    router_init(&app_router);
    assert(services_init(address) == SUCCESS);
    router_connect(&app_router,0);
    router_connect(&app_router,7);
    assert(GATT_NUM_ATTRS(hid_attributes) == (HID_HAS_TOUCH ? 25 : 18));
    assert(sizeof(references) == (HID_HAS_TOUCH ? 10 : 6));
    assert(read_attribute(0,&device_attributes[12],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
    assert(length == 12 && !memcmp(out,"112233445566",12));
    assert(read_attribute(7,&device_attributes[14],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
    assert(length == 7 && out[0] == 1 && out[1] == 0xD7 && out[2] == 7);
    assert(read_attribute(0,&battery_attributes[2],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
    assert(length == 1 && out[0] == 100);
    assert(write_attribute(0,&controller_attributes[2],key,sizeof(key),0,ATT_WRITE_REQ) == ATT_ERR_INSUFFICIENT_AUTHOR);
    assert(write_attribute(0,&controller_attributes[6],ota,1,0,ATT_WRITE_REQ) == ATT_ERR_INSUFFICIENT_AUTHOR);
    assert(ota_calls == 0);
    for (i = 0; i < HID_INPUT_COUNT; ++i) {
        assert(write_attribute(7,&hid_attributes[9+4*i],enable,2,0,ATT_WRITE_REQ) == ATT_ERR_INSUFFICIENT_AUTHOR);
        assert(write_attribute(0,&hid_attributes[9+4*i],enable,2,0,ATT_WRITE_REQ) == SUCCESS);
        assert(read_attribute(0,&hid_attributes[10+4*i],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
        assert(length == 2 && out[0] == i+1 && out[1] == 1);
    }
    assert(services_ready() == (HID_HAS_TOUCH ? 7 : 3));
    assert(read_attribute(7,&controller_attributes[4],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
    assert(length == 20 && out[17] == !HID_HAS_TOUCH && out[18] == (HID_HAS_TOUCH ? 7 : 3));
    assert(read_attribute(7,&hid_attributes[6],out,&length,0,64,ATT_READ_REQ) == ATT_ERR_INSUFFICIENT_AUTHOR);
    for (i = 0; i < sizeof(report_map); i += 23) {
        unsigned expected = sizeof(report_map)-i > 23 ? 23 : sizeof(report_map)-i;
        assert(read_attribute(0,&hid_attributes[6],out,&length,i,23,ATT_READ_BLOB_REQ) == SUCCESS);
        assert(length == expected && !memcmp(out,report_map+i,expected));
    }
    assert(read_attribute(0,&hid_attributes[6],out,&length,sizeof(report_map),23,ATT_READ_BLOB_REQ) == SUCCESS && !length);
    assert(read_attribute(0,&hid_attributes[6],out,&length,sizeof(report_map)+1,23,ATT_READ_BLOB_REQ) == ATT_ERR_INVALID_OFFSET);
    assert(write_attribute(7,&controller_attributes[2],key,sizeof(key),0,ATT_PREPARE_WRITE_REQ) == ATT_ERR_ATTR_NOT_LONG);
    assert(write_attribute(7,&controller_attributes[2],key,sizeof(key),0,ATT_WRITE_REQ) == SUCCESS);
    fail_notification = 1;
    services_send();
    assert(app_router.count == 1 && !notification_count);
    fail_notification = 0;
    services_send();
    assert(notification_count == 1 && notification_connection == 0 && notification_length == 8);
    assert(notification_handle == hid_attributes[KEY_VALUE].handle && !memcmp(notification_bytes,key+1,8));
    for (button = 1; button <= 2; ++button) {
        mouse[1] = button;
        assert(write_attribute(7,&controller_attributes[2],mouse,sizeof(mouse),0,ATT_WRITE_REQ) == SUCCESS);
        services_send();
        assert(notification_connection == 0 && notification_handle == hid_attributes[MOUSE_VALUE].handle);
        assert(notification_length == 7 && !memcmp(notification_bytes,mouse+1,7));
        assert(read_attribute(0,&hid_attributes[MOUSE_VALUE],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
        assert(length == 7 && out[0] == button);
        mouse[1] = 0;
        assert(write_attribute(7,&controller_attributes[2],mouse,sizeof(mouse),0,ATT_WRITE_REQ) == SUCCESS);
        services_send();
        assert(notification_length == 7 && notification_bytes[0] == 0 && !app_router.count);
    }
    assert(write_attribute(7,&controller_attributes[2],mode,sizeof(mode),0,ATT_WRITE_REQ) == 0x80+RESULT_VALUE);
    assert(!app_router.count);
#if HID_HAS_TOUCH
    assert(write_attribute(7,&controller_attributes[2],touch,sizeof(touch),0,ATT_WRITE_REQ) == SUCCESS);
    services_send();
    assert(notification_length == TOUCH_REPORT_SIZE && notification_handle == hid_attributes[TOUCH_VALUE].handle);
    assert(!memcmp(notification_bytes,touch+1,TOUCH_REPORT_SIZE));
    touch[1] = 0;
    assert(write_attribute(7,&controller_attributes[2],touch,sizeof(touch),0,ATT_WRITE_REQ) == SUCCESS);
    services_send();
    assert(notification_length == TOUCH_REPORT_SIZE && notification_bytes[0] == 0);
    assert(read_attribute(0,&hid_attributes[FEATURE_VALUE],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
    assert(length == 1 && out[0] == 2);
    assert(write_attribute(0,&hid_attributes[FEATURE_VALUE],enable,1,0,ATT_WRITE_REQ) == ATT_ERR_WRITE_NOT_PERMITTED);
    enable[0] = 0;
    assert(write_attribute(0,&hid_attributes[TOUCH_VALUE+1],enable,2,0,ATT_WRITE_REQ) == SUCCESS);
    assert(services_ready() == 3);
    assert(write_attribute(7,&controller_attributes[2],touch,sizeof(touch),0,ATT_WRITE_REQ) == 0x80+RESULT_NOT_READY);
    enable[0] = 1;
    assert(write_attribute(0,&hid_attributes[TOUCH_VALUE+1],enable,2,0,ATT_WRITE_REQ) == SUCCESS);
#else
    assert(write_attribute(7,&controller_attributes[2],touch,sizeof(touch),0,ATT_WRITE_REQ) == 0x80+RESULT_VALUE);
#endif
    enable[0] = 0;
    assert(write_attribute(0,&hid_attributes[MOUSE_VALUE+1],enable,2,0,ATT_WRITE_REQ) == SUCCESS);
    assert(services_ready() == (HID_HAS_TOUCH ? 5 : 1));
    assert(write_attribute(7,&controller_attributes[2],mouse,sizeof(mouse),0,ATT_WRITE_REQ) == 0x80+RESULT_NOT_READY);
    enable[0] = 1;
    assert(write_attribute(0,&hid_attributes[MOUSE_VALUE+1],enable,2,0,ATT_WRITE_REQ) == SUCCESS);
    assert(write_attribute(0,&hid_attributes[LED_VALUE],enable,1,0,ATT_WRITE_REQ) == SUCCESS);
    assert(read_attribute(0,&hid_attributes[LED_VALUE],out,&length,0,64,ATT_READ_REQ) == SUCCESS);
    assert(length == 1 && out[0] == 1);
    assert(write_attribute(7,&controller_attributes[6],ota,1,0,ATT_WRITE_REQ) == SUCCESS && ota_calls == 1);
    encrypted = 0;
    assert(services_ready() == 0);
    assert(write_attribute(7,&controller_attributes[2],key,sizeof(key),0,ATT_WRITE_REQ) == 0x80+RESULT_NOT_READY);
    encrypted = 1;
    mouse[1] = 1;
    assert(write_attribute(7,&controller_attributes[2],mouse,sizeof(mouse),0,ATT_WRITE_REQ) == SUCCESS);
    services_send();
    router_disconnect(&app_router,7);
    assert(app_router.count == 2);
    services_send();
    assert(notification_handle == hid_attributes[KEY_VALUE].handle && !notification_bytes[0]);
    services_send();
    assert(notification_handle == hid_attributes[MOUSE_VALUE].handle && !notification_bytes[0] && !app_router.count);
    services_disconnect(0);
    assert(services_ready() == 0);
    assert(controller_attributes[2].permissions & GATT_PERMIT_ENCRYPT_WRITE);
    assert(controller_attributes[6].permissions & GATT_PERMIT_ENCRYPT_WRITE);
    if (argc == 2) {
        FILE *map = fopen(argv[1],"wb");
        assert(map);
        assert(fwrite(report_map,1,sizeof(report_map),map) == sizeof(report_map));
        assert(fclose(map) == 0);
    }
    puts("PASS profile, left/right buttons, releases, GATT ACLs, CCCDs, long map reads, encryption and retry");
    return 0;
}
