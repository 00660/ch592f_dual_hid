#ifndef DUAL_HID_SERIAL_OTA_H
#define DUAL_HID_SERIAL_OTA_H
#include "ota.h"
void serial_init(void);
void serial_poll(ota_t *ota);
void serial_tick(uint16_t elapsed_ms);
#endif
