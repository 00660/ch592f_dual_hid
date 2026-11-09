#ifndef DUAL_HID_LAYOUT_H
#define DUAL_HID_LAYOUT_H

#include <stdint.h>

#define FW_VERSION       0x00010005u
#define BOARD_ID         0x592F0301u
#define JUMP_BASE        0x00000000u
#define APP_BASE         0x00001000u
#define APP_CAPACITY     0x00036000u
#define STAGING_BASE     0x00037000u
#define IAP_BASE         0x0006D000u
#define IAP_CAPACITY     0x00003000u
#define FLASH_END        0x00070000u
#define ERASE_SIZE       4096u
#define WRITE_SIZE       256u
#define ACTIVE_META      0x6000u
#define PENDING_META     0x6100u
#define META_PAGE_SIZE   256u
#define META_MAGIC       0x31484456u

typedef struct {
    uint32_t magic;
    uint32_t board;
    uint32_t size;
    uint32_t crc;
    uint32_t version;
    uint32_t header_crc;
} image_meta_t;

uint32_t crc32_update(uint32_t state, const void *data, uint32_t size);
uint32_t crc32(const void *data, uint32_t size);
uint32_t load_le32(const uint8_t *p);
void store_le32(uint8_t *p, uint32_t value);
int meta_valid(const image_meta_t *meta);
void meta_make(image_meta_t *meta, uint32_t size, uint32_t crc, uint32_t version);

#endif
