#include "ota.h"
#include "storage.h"

int boot_active_valid(void)
{
    image_meta_t meta;
    storage_meta_read(ACTIVE_META, &meta);
    return meta_valid(&meta) && storage_crc(APP_BASE, meta.size) == meta.crc;
}

int boot_install_pending(void)
{
    image_meta_t meta;
    uint32_t offset, size;
    uint32_t buffer[WRITE_SIZE / 4];
    storage_meta_read(PENDING_META, &meta);
    if (!meta_valid(&meta)) return 0;
    if (storage_crc(STAGING_BASE, meta.size) != meta.crc) return -1;
    /* Pending metadata survives until both the application and active record verify. */
    for (offset = 0; offset < meta.size; offset += ERASE_SIZE)
        if (storage_erase(APP_BASE + offset, ERASE_SIZE)) return -1;
    for (offset = 0; offset < meta.size; offset += size) {
        size = meta.size - offset;
        if (size > sizeof(buffer)) size = sizeof(buffer);
        storage_read(STAGING_BASE + offset, buffer, size);
        if (storage_write(APP_BASE + offset, buffer, size)) return -1;
    }
    if (storage_crc(APP_BASE, meta.size) != meta.crc) return -1;
    if (storage_meta_write(ACTIVE_META, &meta)) return -1;
    if (storage_meta_clear(PENDING_META)) return -1;
    return 1;
}
