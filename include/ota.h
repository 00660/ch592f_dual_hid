#ifndef DUAL_HID_OTA_H
#define DUAL_HID_OTA_H

#include "layout.h"
#include "router.h"

#define OTA_MAX_DATA 128u
#define OTA_MAX_PACKET (OTA_MAX_DATA + 5u)
#define OTA_STATUS_SIZE 20u
enum { OTA_INFO = 0x80, OTA_BEGIN, OTA_DATA, OTA_COMMIT, OTA_ABORT };
enum { OTA_IDLE, OTA_ERASING, OTA_RECEIVING, OTA_VERIFYING, OTA_READY, OTA_FAILED };
#define OTA_OWNER_UART 0xFFFEu
#define OTA_OWNER_NONE 0xFFFFu

typedef struct {
    image_meta_t image;
    uint32_t offset;
    uint32_t erase_offset;
    uint32_t verify_offset;
    uint32_t verify_crc;
    uint16_t owner;
    uint8_t state;
    uint8_t result;
    uint8_t command;
} ota_t;

void ota_init(ota_t *ota);
uint8_t ota_command(ota_t *ota, uint16_t owner, const uint8_t *data, uint16_t size);
void ota_poll(ota_t *ota);
void ota_disconnect(ota_t *ota, uint16_t owner);
void ota_status(const ota_t *ota, uint8_t out[OTA_STATUS_SIZE]);
int boot_install_pending(void);
int boot_active_valid(void);

#endif
