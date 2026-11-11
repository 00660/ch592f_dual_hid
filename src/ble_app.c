#include "CONFIG.h"
#include "ble_app.h"
#include "serial_ota.h"
#include "diag.h"
#include <string.h>

#define START_EVENT 0x0001
#define TICK_EVENT 0x0002
#define ADV_EVENT 0x0004
#define PARAM_EVENT 0x0008
#define RESET_EVENT 0x0010
#define LOG_EVENT 0x0020
#define SERVICE_CHANGED_EVENT 0x0040

router_t app_router;
ota_t app_ota;
static tmosTaskID task;
static uint8_t ota_divider, reset_scheduled, ota_was_active;
static uint8_t changed_pending, changed_sent, changed_attempts[2];
static uint8_t device_name[] = "CH592F-3IN1";
static uint8_t scan_response[] = {12,GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'C','H','5','9','2','F','-','3','I','N','1'};

static unsigned init_result(const char *name, unsigned status)
{
    diag_printf("[BLE] %s rc=%02X\n",name,status);
    return status;
}

static void advertise(void)
{
    uint8_t adv[31] = {2,GAP_ADTYPE_FLAGS,GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED};
    uint8_t size = 3;
    uint8_t enable = app_router.hid == NO_CONNECTION || app_router.controller == NO_CONNECTION;
    if (app_router.hid == NO_CONNECTION) {
        const uint8_t hid[] = {3,GAP_ADTYPE_16BIT_MORE,0x12,0x18,3,GAP_ADTYPE_APPEARANCE,0xC0,0x03};
        memcpy(adv + size,hid,sizeof(hid));
        size += sizeof(hid);
    } else {
        adv[size++] = 17;
        adv[size++] = GAP_ADTYPE_128BIT_MORE;
        memcpy(adv + size,control_service_uuid,16);
        size += 16;
    }
    uint8_t data_status = GAPRole_SetParameter(GAPROLE_ADVERT_DATA,size,adv);
    uint8_t enable_status = GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,sizeof(enable),&enable);
    diag_printf("[BLE] adv enable=%u data_rc=%02X enable_rc=%02X\n",
                (unsigned)enable,(unsigned)data_status,(unsigned)enable_status);
}

static void state_changed(gapRole_States_t state, gapRoleEvent_t *event)
{
    diag_printf("[BLE] state=%02X event=%02X status=%02X\n",(unsigned)state,
                event ? (unsigned)event->gap.opcode : 0xFFu,
                event ? (unsigned)event->gap.hdr.status : 0xFFu);
    if (!event) return;
    if (event->gap.opcode == GAP_LINK_ESTABLISHED_EVENT && event->gap.hdr.status == SUCCESS) {
        uint16_t handle = event->linkCmpl.connectionHandle;
        if (!router_connect(&app_router,handle)) GAPRole_TerminateLink(handle);
        else {
            diag_printf("[BLE] connected handle=%04X role=%u\n",
                        (unsigned)handle,(unsigned)router_role(&app_router,handle));
            tmos_start_task(task,PARAM_EVENT,MS1_TO_SYSTEM_TIME(1200));
            GAPBondMgr_PeriSecurityReq(handle);
        }
        tmos_start_task(task,ADV_EVENT,MS1_TO_SYSTEM_TIME(20));
    } else if (event->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
        uint16_t handle = event->linkTerminate.connectionHandle;
        uint8_t role = router_role(&app_router,handle);
        if (role) changed_pending &= (uint8_t)~(1u << (role-1));
        diag_printf("[BLE] disconnected handle=%04X reason=%02X\n",
                    (unsigned)handle,(unsigned)event->linkTerminate.reason);
        services_disconnect(handle);
        router_disconnect(&app_router,handle);
        ota_disconnect(&app_ota,handle);
        tmos_start_task(task,ADV_EVENT,MS1_TO_SYSTEM_TIME(20));
    }
}

static void pair_changed(uint16_t handle, uint8_t state, uint8_t status)
{
    diag_printf("[BLE] pair handle=%04X state=%u status=%02X\n",
                (unsigned)handle,(unsigned)state,(unsigned)status);
    if ((state == GAPBOND_PAIRING_STATE_COMPLETE || state == GAPBOND_PAIRING_STATE_BONDED) &&
        status != SUCCESS) GAPRole_TerminateLink(handle);
    if ((state == GAPBOND_PAIRING_STATE_COMPLETE || state == GAPBOND_PAIRING_STATE_BONDED) &&
        status == SUCCESS) {
        uint8_t role = router_role(&app_router,handle);
        if (role && !(changed_sent & (1u << (role-1)))) {
            changed_pending |= 1u << (role-1);
            changed_attempts[role-1] = 0;
            tmos_start_task(task,SERVICE_CHANGED_EVENT,MS1_TO_SYSTEM_TIME(500));
        }
    }
}

static gapRolesCBs_t role_callbacks = {state_changed,NULL,NULL};
static gapBondCBs_t bond_callbacks = {NULL,pair_changed,NULL};

static tmosEvents process(tmosTaskID id, tmosEvents events)
{
    if (events & SYS_EVENT_MSG) {
        uint8_t *message;
        while ((message = tmos_msg_receive(id)) != NULL) {
            if (((tmos_event_hdr_t *)message)->event == GATT_MSG_EVENT) {
                gattMsgEvent_t *gatt = (gattMsgEvent_t *)message;
                GATT_bm_free(&gatt->msg,gatt->method);
            }
            tmos_msg_deallocate(message);
        }
        return events ^ SYS_EVENT_MSG;
    }
    if (events & START_EVENT) {
        init_result("start",GAPRole_PeripheralStartDevice(task,&bond_callbacks,&role_callbacks));
        tmos_start_task(task,TICK_EVENT,MS1_TO_SYSTEM_TIME(HID_SEND_PERIOD_MS));
        tmos_start_task(task,LOG_EVENT,MS1_TO_SYSTEM_TIME(5000));
        return events ^ START_EVENT;
    }
    if (events & LOG_EVENT) {
        diag_printf("[APP] alive hid=%04X ctrl=%04X ready=%02X ota=%u\n",
                    (unsigned)app_router.hid,(unsigned)app_router.controller,
                    (unsigned)services_ready(),(unsigned)app_ota.state);
        tmos_start_task(task,LOG_EVENT,MS1_TO_SYSTEM_TIME(5000));
        return events ^ LOG_EVENT;
    }
    if (events & ADV_EVENT) {
        advertise();
        return events ^ ADV_EVENT;
    }
    if (events & PARAM_EVENT) {
        if (app_router.hid != NO_CONNECTION)
            GAPRole_PeripheralConnParamUpdateReq(app_router.hid,12,24,0,600,task);
        if (app_router.controller != NO_CONNECTION)
            GAPRole_PeripheralConnParamUpdateReq(app_router.controller,12,24,0,600,task);
        return events ^ PARAM_EVENT;
    }
    if (events & SERVICE_CHANGED_EVENT) {
        uint8_t i;
        for (i = 0; i < 2; ++i) {
            uint16_t handle = i ? app_router.controller : app_router.hid;
            if (!(changed_pending & (1u << i))) continue;
            if (handle == NO_CONNECTION) {
                changed_pending &= (uint8_t)~(1u << i);
                continue;
            }
            uint8_t status = GATTServApp_SendServiceChangedInd(handle,task);
            diag_printf("[BLE] service changed handle=%04X rc=%02X\n",
                        (unsigned)handle,(unsigned)status);
            if (status == SUCCESS) changed_sent |= 1u << i;
            if (status == SUCCESS || ++changed_attempts[i] >= 3)
                changed_pending &= (uint8_t)~(1u << i);
        }
        if (changed_pending)
            tmos_start_task(task,SERVICE_CHANGED_EVENT,MS1_TO_SYSTEM_TIME(500));
        return events ^ SERVICE_CHANGED_EVENT;
    }
    if (events & RESET_EVENT) {
        diag_printf("[APP] OTA committed; reset\n");
        diag_flush();
        SYS_ResetExecute();
        return events ^ RESET_EVENT;
    }
    if (events & TICK_EVENT) {
        uint8_t active = app_ota.state != OTA_IDLE && app_ota.state != OTA_FAILED;
        if (active && !ota_was_active) {
            router_macro_stop(&app_router);
            router_release(&app_router);
        }
        ota_was_active = active;
        services_send();
        if (app_ota.state == OTA_IDLE || app_ota.state == OTA_FAILED)
            router_macro_tick(&app_router,services_ready(),HID_SEND_PERIOD_MS);
        serial_tick(HID_SEND_PERIOD_MS);
        if (++ota_divider >= 2) {
            ota_divider = 0;
            ota_poll(&app_ota);
        }
        if (app_ota.state == OTA_READY && !reset_scheduled) {
            reset_scheduled = 1;
            tmos_start_task(task,RESET_EVENT,MS1_TO_SYSTEM_TIME(1500));
        }
        tmos_start_task(task,TICK_EVENT,MS1_TO_SYSTEM_TIME(HID_SEND_PERIOD_MS));
        return events ^ TICK_EVENT;
    }
    return 0;
}

void ble_app_init(void)
{
    uint8_t enabled = TRUE, disabled = FALSE;
    uint8_t pair_mode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8_t io = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint16_t appearance = 0x03C0;
    __attribute__((aligned(4))) uint8_t address[6];
    router_init(&app_router);
    ota_init(&app_ota);
    task = TMOS_ProcessEventRegister(process);
    diag_printf("[BLE] task=%u\n",(unsigned)task);
    if (task == INVALID_TASK_ID) {
        diag_printf("[BLE] task registration failed; HALTED\n");
        diag_flush();
        for (;;) {}
    }
    init_result("pair mode",GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,1,&pair_mode));
    init_result("MITM",GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION,1,&disabled));
    init_result("IO",GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES,1,&io));
    init_result("bonding",GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED,1,&enabled));
    init_result("resolve list",GAPBondMgr_SetParameter(GAPBOND_AUTO_SYNC_RL,1,&enabled));
    init_result("GAP service",GGS_AddService(GATT_ALL_SERVICES));
    init_result("GATT service",GATTServApp_AddService(GATT_ALL_SERVICES));
    init_result("device name",GGS_SetParameter(GGS_DEVICE_NAME_ATT,sizeof(device_name)-1,device_name));
    init_result("appearance",GGS_SetParameter(GGS_APPEARANCE_ATT,sizeof(appearance),&appearance));
    init_result("MAC",GetMACAddress(address));
    if (init_result("HID/control services",services_init(address)) != SUCCESS) {
        diag_printf("[BLE] services failed; HALTED\n");
        diag_flush();
        for (;;) {}
    }
    init_result("scan response",GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,sizeof(scan_response),scan_response));
    /* 20 ms discovery interval; this build assumes external power. */
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN,32);
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX,32);
    advertise();
    tmos_set_event(task,START_EVENT);
}
