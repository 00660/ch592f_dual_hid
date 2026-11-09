#include "CONFIG.h"
#include "ble_app.h"
#include "diag.h"
#include "report_map.h"
#include <string.h>

/* 5920000x-3c7f-4a9e-8c51-3b9d4b6a1200 in ATT byte order. */
#define UUID_BYTES(x) 0x00,0x12,0x6A,0x4B,0x9D,0x3B,0x51,0x8C,0x9E,0x4A,0x7F,0x3C,x,0x00,0x20,0x59
const uint8_t control_service_uuid[16] = {UUID_BYTES(1)};
static const uint8_t command_uuid[16] = {UUID_BYTES(2)};
static const uint8_t status_uuid[16] = {UUID_BYTES(3)};
static const uint8_t ota_uuid[16] = {UUID_BYTES(4)};
static const uint8_t hid_uuid[] = {0x12,0x18};
static const uint8_t info_uuid[] = {0x4A,0x2A};
static const uint8_t control_uuid[] = {0x4C,0x2A};
static const uint8_t map_uuid[] = {0x4B,0x2A};
static const uint8_t report_uuid[] = {0x4D,0x2A};
static const uint8_t reference_uuid[] = {0x08,0x29};
static const uint8_t dis_uuid[] = {0x0A,0x18};
static const uint8_t model_uuid[] = {0x24,0x2A};
static const uint8_t firmware_uuid[] = {0x26,0x2A};
static const uint8_t manufacturer_uuid[] = {0x29,0x2A};
static const uint8_t hardware_uuid[] = {0x27,0x2A};
static const uint8_t software_uuid[] = {0x28,0x2A};
static const uint8_t serial_uuid[] = {0x25,0x2A};
static const uint8_t pnp_uuid[] = {0x50,0x2A};
static const uint8_t battery_uuid[] = {0x0F,0x18};
static const uint8_t level_uuid[] = {0x19,0x2A};
static const gattAttrType_t hid_service = {2, hid_uuid};
static const gattAttrType_t controller_service = {16, control_service_uuid};
static const gattAttrType_t device_service = {2, dis_uuid};
static const gattAttrType_t battery_service = {2, battery_uuid};
static uint8_t prop_read = GATT_PROP_READ;
static uint8_t prop_write = GATT_PROP_WRITE;
static uint8_t prop_write_nr = GATT_PROP_WRITE_NO_RSP;
static uint8_t prop_read_write = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t prop_report = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t hid_info[] = {0x11,0x01,0x00,0x02};
static uint8_t input[HID_INPUT_COUNT][MAX_HID_REPORT_SIZE], led_output, control_point;
#if HID_HAS_TOUCH
static uint8_t contact_max = 1;
static uint8_t references[][2] = {{1,1},{2,1},{3,1},{1,2},{4,3}};
#else
static uint8_t references[][2] = {{1,1},{2,1},{1,2}};
#endif
static gattCharCfg_t report_ccc[HID_INPUT_COUNT][GATT_MAX_NUM_CONN];
static uint8_t placeholder, last_result;
static uint8_t model[] = "CH592F-3IN1";
static uint8_t firmware[16];
static uint8_t manufacturer[] = "CH592F DIY";
static uint8_t hardware[] = "1";
static uint8_t serial_number[13];
/* Development PnP identity from the pinned WCH HID_Keyboard example. */
static uint8_t pnp[] = {1,0xD7,0x07,0,0,0,0x01};
/* This build models an externally powered device, not a battery gauge. */
static uint8_t battery_level = 100;

#define ATTR(uuid,perm,value) {{sizeof(uuid),uuid},perm,0,(uint8_t *)(value)}
#define DECL(prop) {{2,characterUUID},GATT_PERMIT_READ,0,&prop}
#define PRIMARY(value) {{2,primaryServiceUUID},GATT_PERMIT_READ,0,(uint8_t *)&value}
#define CCC(index) {{2,clientCharCfgUUID},GATT_PERMIT_READ|GATT_PERMIT_ENCRYPT_WRITE,0,(uint8_t *)report_ccc[index]}
#define ER GATT_PERMIT_ENCRYPT_READ
#define EW GATT_PERMIT_ENCRYPT_WRITE

enum { KEY_VALUE = 8, MOUSE_VALUE = 12, TOUCH_VALUE = 16,
       LED_VALUE = 16 + 4 * HID_HAS_TOUCH, FEATURE_VALUE = 23 };
#if HID_HAS_TOUCH
static const uint8_t report_ids[] = {CMD_KEYBOARD,CMD_MOUSE,CMD_TOUCH};
static const uint8_t report_indexes[] = {KEY_VALUE,MOUSE_VALUE,TOUCH_VALUE};
static const uint8_t report_sizes[] = {8,7,TOUCH_REPORT_SIZE};
static const uint8_t report_ready_bits[] = {1,2,4};
#else
static const uint8_t report_ids[] = {CMD_KEYBOARD,CMD_MOUSE};
static const uint8_t report_indexes[] = {KEY_VALUE,MOUSE_VALUE};
static const uint8_t report_sizes[] = {8,7};
static const uint8_t report_ready_bits[] = {1,2};
#endif
static gattAttribute_t hid_attributes[] = {
    PRIMARY(hid_service),
    DECL(prop_read), ATTR(info_uuid,ER,hid_info),
    DECL(prop_write_nr), ATTR(control_uuid,EW,&control_point),
    DECL(prop_read), ATTR(map_uuid,ER,report_map),
    DECL(prop_report), ATTR(report_uuid,ER,input[0]), CCC(0), ATTR(reference_uuid,ER,references[0]),
    DECL(prop_report), ATTR(report_uuid,ER,input[1]), CCC(1), ATTR(reference_uuid,ER,references[1]),
#if HID_HAS_TOUCH
    DECL(prop_report), ATTR(report_uuid,ER,input[2]), CCC(2), ATTR(reference_uuid,ER,references[2]),
    DECL(prop_read_write), ATTR(report_uuid,ER|EW,&led_output), ATTR(reference_uuid,ER,references[3]),
    DECL(prop_read), ATTR(report_uuid,ER,&contact_max), ATTR(reference_uuid,ER,references[4])
#else
    DECL(prop_read_write), ATTR(report_uuid,ER|EW,&led_output), ATTR(reference_uuid,ER,references[2])
#endif
};
static gattAttribute_t controller_attributes[] = {
    PRIMARY(controller_service),
    DECL(prop_write), ATTR(command_uuid,EW,&placeholder),
    DECL(prop_read), ATTR(status_uuid,GATT_PERMIT_READ,&placeholder),
    DECL(prop_read_write), ATTR(ota_uuid,ER|EW,&placeholder)
};
static gattAttribute_t device_attributes[] = {
    PRIMARY(device_service),
    DECL(prop_read), ATTR(model_uuid,GATT_PERMIT_READ,model),
    DECL(prop_read), ATTR(firmware_uuid,GATT_PERMIT_READ,firmware),
    DECL(prop_read), ATTR(manufacturer_uuid,GATT_PERMIT_READ,manufacturer),
    DECL(prop_read), ATTR(hardware_uuid,GATT_PERMIT_READ,hardware),
    DECL(prop_read), ATTR(software_uuid,GATT_PERMIT_READ,firmware),
    DECL(prop_read), ATTR(serial_uuid,GATT_PERMIT_READ,serial_number),
    DECL(prop_read), ATTR(pnp_uuid,GATT_PERMIT_READ,pnp)
};
static gattAttribute_t battery_attributes[] = {
    PRIMARY(battery_service),
    DECL(prop_read), ATTR(level_uuid,GATT_PERMIT_READ,&battery_level)
};

static bStatus_t copy_value(uint8_t *out, uint16_t *length, uint16_t offset,
                           uint16_t maximum, const uint8_t *value, uint16_t size)
{
    if (offset > size) return ATT_ERR_INVALID_OFFSET;
    *length = size - offset;
    if (*length > maximum) *length = maximum;
    memcpy(out, value + offset, *length);
    return SUCCESS;
}

uint8_t services_ready(void)
{
    uint8_t mask = 0, i;
    uint16_t handle = app_router.hid;
    if (handle == NO_CONNECTION || control_point == 0 ||
        !linkDB_State(handle, LINK_ENCRYPTED)) return 0;
    for (i = 0; i < HID_INPUT_COUNT; ++i)
        if (GATTServApp_ReadCharCfg(handle, report_ccc[i]) & GATT_CLIENT_CFG_NOTIFY)
            mask |= report_ready_bits[i];
    return mask;
}

static void read_status(uint16_t handle, uint8_t out[20])
{
    memset(out, 0, 20);
    out[0] = 1;
    out[1] = router_role(&app_router, handle);
    out[2] = app_router.hid != NO_CONNECTION;
    out[3] = app_router.controller != NO_CONNECTION;
    out[4] = services_ready();
    out[5] = app_router.count;
    out[6] = last_result;
    out[7] = led_output;
    store_le32(out + 8, FW_VERSION);
    store_le32(out + 12, BOARD_ID);
    out[16] = app_ota.state;
    out[17] = HID_HAS_TOUCH ? 0 : 1;
    out[18] = HID_HAS_TOUCH ? 7 : 3;
}

static bStatus_t read_attribute(uint16_t handle, gattAttribute_t *attr, uint8_t *out,
                               uint16_t *length, uint16_t offset, uint16_t maximum,
                               uint8_t method)
{
    uint8_t status[20];
    unsigned i;
    (void)method;
    *length = 0;
    if (attr == &controller_attributes[4]) {
        read_status(handle, status);
        return copy_value(out,length,offset,maximum,status,sizeof(status));
    }
    if (attr == &controller_attributes[6]) {
        if (router_role(&app_router, handle) != ROLE_CONTROLLER) return ATT_ERR_INSUFFICIENT_AUTHOR;
        ota_status(&app_ota, status);
        return copy_value(out,length,offset,maximum,status,sizeof(status));
    }
    if (attr->pValue == model) return copy_value(out,length,offset,maximum,model,sizeof(model)-1);
    if (attr->pValue == firmware) return copy_value(out,length,offset,maximum,firmware,strlen((char *)firmware));
    if (attr->pValue == manufacturer) return copy_value(out,length,offset,maximum,manufacturer,sizeof(manufacturer)-1);
    if (attr->pValue == hardware) return copy_value(out,length,offset,maximum,hardware,sizeof(hardware)-1);
    if (attr->pValue == serial_number) return copy_value(out,length,offset,maximum,serial_number,sizeof(serial_number)-1);
    if (attr->pValue == pnp) return copy_value(out,length,offset,maximum,pnp,sizeof(pnp));
    if (attr->pValue == &battery_level) return copy_value(out,length,offset,maximum,&battery_level,1);
    if (router_role(&app_router, handle) != ROLE_HID) return ATT_ERR_INSUFFICIENT_AUTHOR;
    if (attr->pValue == (uint8_t *)report_map) {
        diag_printf("[HID] map h=%04X offset=%u max=%u size=%u touch=%u\n",
                    (unsigned)handle,(unsigned)offset,(unsigned)maximum,
                    (unsigned)sizeof(report_map),(unsigned)HID_HAS_TOUCH);
        return copy_value(out,length,offset,maximum,report_map,sizeof(report_map));
    }
    if (attr->pValue == hid_info) return copy_value(out,length,offset,maximum,hid_info,sizeof(hid_info));
    {
        for (i = 0; i < HID_INPUT_COUNT; ++i) {
            if (attr->pValue == input[i])
                return copy_value(out,length,offset,maximum,input[i],report_sizes[i]);
            if (attr->pValue == (uint8_t *)report_ccc[i]) {
                uint16_t cfg = GATTServApp_ReadCharCfg(handle, report_ccc[i]);
                status[0] = (uint8_t)cfg;
                status[1] = (uint8_t)(cfg >> 8);
                return copy_value(out,length,offset,maximum,status,2);
            }
        }
    }
    for (i = 0; i < sizeof(references)/sizeof(references[0]); ++i)
        if (attr->pValue == references[i]) return copy_value(out,length,offset,maximum,references[i],2);
#if HID_HAS_TOUCH
    if (attr->pValue == &contact_max)
        return copy_value(out,length,offset,maximum,&contact_max,1);
#endif
    if (attr->pValue == &led_output)
        return copy_value(out,length,offset,maximum,attr->pValue,1);
    return ATT_ERR_ATTR_NOT_FOUND;
}

static bStatus_t write_attribute(uint16_t handle, gattAttribute_t *attr, uint8_t *p,
                                uint16_t size, uint16_t offset, uint8_t method)
{
    unsigned i;
    uint8_t status;
    if (offset) return ATT_ERR_INVALID_OFFSET;
    if (method == ATT_PREPARE_WRITE_REQ) return ATT_ERR_ATTR_NOT_LONG;
    if (attr == &controller_attributes[2] || attr == &controller_attributes[6]) {
        if (router_role(&app_router, handle) != ROLE_CONTROLLER) return ATT_ERR_INSUFFICIENT_AUTHOR;
        if (attr == &controller_attributes[2]) {
            status = app_ota.state != OTA_IDLE && app_ota.state != OTA_FAILED
                ? RESULT_BUSY : router_command(&app_router,handle,services_ready(),p,size);
            last_result = status;
        } else {
            status = ota_command(&app_ota,handle,p,size);
            if (status == RESULT_OK && size && p[0] == OTA_BEGIN) router_release(&app_router);
        }
        return status == RESULT_OK ? SUCCESS : 0x80u + status;
    }
    if (router_role(&app_router, handle) != ROLE_HID) return ATT_ERR_INSUFFICIENT_AUTHOR;
    for (i = 0; i < HID_INPUT_COUNT; ++i)
        if (attr->pValue == (uint8_t *)report_ccc[i])
            return GATTServApp_ProcessCCCWriteReq(handle,attr,p,size,offset,GATT_CLIENT_CFG_NOTIFY);
    if (attr->pValue == &led_output) {
        if (size != 1) return ATT_ERR_INVALID_VALUE_SIZE;
        led_output = p[0] & 0x1F;
        return SUCCESS;
    }
    if (attr->pValue == &control_point) {
        if (size != 1 || p[0] > 1) return ATT_ERR_INVALID_VALUE_SIZE;
        control_point = p[0];
        router_release(&app_router);
        return SUCCESS;
    }
    return ATT_ERR_WRITE_NOT_PERMITTED;
}

static gattServiceCBs_t callbacks = {read_attribute,write_attribute,NULL};

static void format_identity(const uint8_t address[6])
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned parts[] = {FW_VERSION >> 16,(FW_VERSION >> 8) & 255,FW_VERSION & 255};
    uint8_t *out = firmware;
    unsigned i;
    for (i = 0; i < 6; ++i) {
        serial_number[2*i] = hex[address[5-i] >> 4];
        serial_number[2*i+1] = hex[address[5-i] & 15];
    }
    serial_number[12] = 0;
    for (i = 0; i < 3; ++i) {
        uint8_t reversed[5];
        unsigned value = parts[i], count = 0;
        if (i) *out++ = '.';
        do {
            reversed[count++] = '0' + value % 10;
            value /= 10;
        } while (value);
        while (count) *out++ = reversed[--count];
    }
    *out = 0;
}

uint8_t services_init(const uint8_t address[6])
{
    unsigned i;
    uint8_t status;
    control_point = 1;
    format_identity(address);
    diag_printf("[HID] profile=%u inputs=%u map=%u touch=%u\n",
                (unsigned)!HID_HAS_TOUCH,(unsigned)HID_INPUT_COUNT,
                (unsigned)sizeof(report_map),(unsigned)HID_HAS_TOUCH);
    for (i = 0; i < HID_INPUT_COUNT; ++i) GATTServApp_InitCharCfg(INVALID_CONNHANDLE,report_ccc[i]);
    status = GATTServApp_RegisterService(hid_attributes,GATT_NUM_ATTRS(hid_attributes),16,&callbacks);
    if (status) return status;
    status = GATTServApp_RegisterService(controller_attributes,GATT_NUM_ATTRS(controller_attributes),16,&callbacks);
    if (status) return status;
    status = GATTServApp_RegisterService(device_attributes,GATT_NUM_ATTRS(device_attributes),16,&callbacks);
    if (status) return status;
    return GATTServApp_RegisterService(battery_attributes,GATT_NUM_ATTRS(battery_attributes),16,&callbacks);
}

void services_disconnect(uint16_t handle)
{
    unsigned i;
    for (i = 0; i < HID_INPUT_COUNT; ++i) GATTServApp_InitCharCfg(handle,report_ccc[i]);
    if (handle == app_router.hid) {
        memset(input,0,sizeof(input));
        led_output = 0;
        control_point = 1;
    }
}

void services_send(void)
{
    const hid_report_t *report = router_peek(&app_router);
    attHandleValueNoti_t notification;
    uint8_t index, i;
    if (!report || app_router.hid == NO_CONNECTION) return;
    index = 0xFF;
    for (i = 0; i < HID_INPUT_COUNT; ++i) if (report->id == report_ids[i]) index = i;
    if (index == 0xFF) { router_pop(&app_router); return; }
    if (!(services_ready() & report_ready_bits[index])) {
        router_pop(&app_router);
        return;
    }
    notification.pValue = GATT_bm_alloc(app_router.hid,ATT_HANDLE_VALUE_NOTI,report->size,NULL,0);
    if (!notification.pValue) return;
    notification.handle = hid_attributes[report_indexes[index]].handle;
    notification.len = report->size;
    memcpy(notification.pValue,report->data,report->size);
    if (GATT_Notification(app_router.hid,&notification,FALSE) == SUCCESS) {
#if HID_HAS_TOUCH
        if (report->id == CMD_TOUCH && ((report->data[0] ^ app_router.touch_sent[0]) & 1u))
            diag_printf("[HID] touch h=%04X down=%u x=%u y=%u count=%u\n",
                        (unsigned)notification.handle,(unsigned)report->data[0],
                        (unsigned)(report->data[2] | ((uint16_t)report->data[3] << 8)),
                        (unsigned)(report->data[4] | ((uint16_t)report->data[5] << 8)),
                        (unsigned)report->data[6]);
#endif
        if (report->id == CMD_MOUSE && input[index][0] != report->data[0])
            diag_printf("[HID] mouse h=%04X buttons=%02X\n",
                        (unsigned)notification.handle,(unsigned)report->data[0]);
        memcpy(input[index],report->data,report->size);
        router_sent(&app_router);
    } else GATT_bm_free((gattMsg_t *)&notification,ATT_HANDLE_VALUE_NOTI);
}
