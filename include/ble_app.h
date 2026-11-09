#ifndef DUAL_HID_BLE_APP_H
#define DUAL_HID_BLE_APP_H
#include "router.h"
#include "ota.h"

extern router_t app_router;
extern ota_t app_ota;
extern const uint8_t control_service_uuid[16];
uint8_t services_init(const uint8_t address[6]);
void services_disconnect(uint16_t handle);
uint8_t services_ready(void);
void services_send(void);
void ble_app_init(void);

#endif
