#include "CH59x_common.h"
#include "storage.h"
#include <string.h>

int storage_erase(uint32_t address, uint32_t size)
{
    if (address < APP_BASE || address >= IAP_BASE || size > IAP_BASE - address ||
        (address % ERASE_SIZE) || (size % ERASE_SIZE)) return -1;
    return FLASH_ROM_ERASE(address, size) != 0;
}

int storage_write(uint32_t address, const void *data, uint32_t size)
{
    if (address < APP_BASE || address >= IAP_BASE || size > IAP_BASE - address ||
        (address & 3u) || (size & 3u)) return -1;
    return FLASH_ROM_WRITE(address, (void *)data, size) != 0;
}

void storage_read(uint32_t address, void *data, uint32_t size)
{
    FLASH_ROM_READ(address, data, size);
}

void storage_meta_read(uint32_t address, image_meta_t *meta)
{
    EEPROM_READ(address, meta, sizeof(*meta));
}

int storage_meta_clear(uint32_t address)
{
    if (address != ACTIVE_META && address != PENDING_META) return -1;
    return EEPROM_ERASE(address, META_PAGE_SIZE) != 0;
}

int storage_meta_write(uint32_t address, const image_meta_t *meta)
{
    image_meta_t check;
    if (storage_meta_clear(address)) return -1;
    if (EEPROM_WRITE(address, (void *)meta, sizeof(*meta))) return -1;
    storage_meta_read(address, &check);
    return memcmp(meta, &check, sizeof(check)) != 0;
}

uint32_t storage_crc(uint32_t address, uint32_t size)
{
    uint32_t state = 0xFFFFFFFFu;
    uint32_t buffer[WRITE_SIZE / 4];
    while (size) {
        uint32_t count = size > sizeof(buffer) ? sizeof(buffer) : size;
        storage_read(address, buffer, count);
        state = crc32_update(state, buffer, count);
        address += count;
        size -= count;
    }
    return state ^ 0xFFFFFFFFu;
}
