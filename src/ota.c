#include "ota.h"
#include "storage.h"
#include <string.h>

void ota_init(ota_t *o)
{
    memset(o, 0, sizeof(*o));
    o->owner = OTA_OWNER_NONE;
}

static uint8_t result(ota_t *o, uint8_t command, uint8_t status)
{
    o->command = command;
    o->result = status;
    return status;
}

uint8_t ota_command(ota_t *o, uint16_t owner, const uint8_t *p, uint16_t n)
{
    uint8_t cmd;
    uint32_t offset, size;
    uint32_t aligned[OTA_MAX_DATA / 4];
    if (!n || n > OTA_MAX_PACKET || owner == OTA_OWNER_NONE)
        return result(o, 0, RESULT_LENGTH);
    cmd = p[0];
    if (cmd == OTA_INFO) return n == 1 ? RESULT_OK : RESULT_LENGTH;
    if (o->owner != OTA_OWNER_NONE && o->owner != owner) return RESULT_BUSY;
    if (o->state == OTA_READY) return RESULT_BUSY;
    if (cmd == OTA_ABORT) {
        if (n != 1) return result(o, cmd, RESULT_LENGTH);
        ota_init(o);
        return result(o, cmd, RESULT_OK);
    }
    if (cmd == OTA_BEGIN) {
        if (n != 17) return result(o, cmd, RESULT_LENGTH);
        if (o->state != OTA_IDLE && o->state != OTA_FAILED)
            return result(o, cmd, RESULT_BUSY);
        size = load_le32(p + 1);
        if (size < 256 || size > APP_CAPACITY || (size & 3) ||
            load_le32(p + 13) != BOARD_ID) return result(o, cmd, RESULT_VALUE);
        if (storage_meta_clear(PENDING_META)) return result(o, cmd, RESULT_FLASH);
        meta_make(&o->image, size, load_le32(p + 5), load_le32(p + 9));
        o->owner = owner;
        o->offset = o->erase_offset = 0;
        o->state = OTA_ERASING;
        return result(o, cmd, RESULT_OK);
    }
    if (o->owner != owner) return result(o, cmd, RESULT_STATE);
    if (cmd == OTA_DATA) {
        if (n < 9 || ((n - 5) & 3)) return result(o, cmd, RESULT_LENGTH);
        if (o->state != OTA_RECEIVING) return result(o, cmd, RESULT_STATE);
        offset = load_le32(p + 1);
        size = n - 5;
        if (offset > o->image.size || size > o->image.size - offset)
            return result(o, cmd, RESULT_OFFSET);
        if (offset != o->offset) {
            /* A lost reply may cause an exact retransmission. Never rewrite flash. */
            if (offset < o->offset && size <= o->offset - offset) {
                storage_read(STAGING_BASE + offset, aligned, size);
                if (!memcmp(aligned, p + 5, size)) return result(o, cmd, RESULT_OK);
            }
            return result(o, cmd, RESULT_OFFSET);
        }
        memcpy(aligned, p + 5, size);
        if (storage_write(STAGING_BASE + offset, aligned, size)) {
            o->state = OTA_FAILED;
            return result(o, cmd, RESULT_FLASH);
        }
        storage_read(STAGING_BASE + offset, aligned, size);
        if (memcmp(aligned, p + 5, size)) {
            o->state = OTA_FAILED;
            return result(o, cmd, RESULT_FLASH);
        }
        o->offset += size;
        return result(o, cmd, RESULT_OK);
    }
    if (cmd == OTA_COMMIT) {
        if (n != 1) return result(o, cmd, RESULT_LENGTH);
        if (o->state != OTA_RECEIVING || o->offset != o->image.size)
            return result(o, cmd, RESULT_STATE);
        o->state = OTA_VERIFYING;
        o->verify_offset = 0;
        o->verify_crc = 0xFFFFFFFFu;
        return result(o, cmd, RESULT_OK);
    }
    return result(o, cmd, RESULT_VALUE);
}

void ota_poll(ota_t *o)
{
    uint32_t buffer[WRITE_SIZE / 4];
    uint32_t size;
    if (o->state == OTA_ERASING) {
        if (storage_erase(STAGING_BASE + o->erase_offset, ERASE_SIZE)) {
            o->state = OTA_FAILED;
            o->result = RESULT_FLASH;
            return;
        }
        o->erase_offset += ERASE_SIZE;
        if (o->erase_offset >= o->image.size) o->state = OTA_RECEIVING;
    } else if (o->state == OTA_VERIFYING) {
        size = o->image.size - o->verify_offset;
        if (size > sizeof(buffer)) size = sizeof(buffer);
        storage_read(STAGING_BASE + o->verify_offset, buffer, size);
        o->verify_crc = crc32_update(o->verify_crc, buffer, size);
        o->verify_offset += size;
        if (o->verify_offset == o->image.size) {
            if ((o->verify_crc ^ 0xFFFFFFFFu) != o->image.crc) {
                o->state = OTA_FAILED;
                o->result = RESULT_CRC;
            } else if (storage_meta_write(PENDING_META, &o->image)) {
                o->state = OTA_FAILED;
                o->result = RESULT_FLASH;
            } else {
                o->state = OTA_READY;
                o->result = RESULT_OK;
            }
        }
    }
}

void ota_disconnect(ota_t *o, uint16_t owner)
{
    if (owner == o->owner && o->state != OTA_READY) ota_init(o);
}

void ota_status(const ota_t *o, uint8_t out[OTA_STATUS_SIZE])
{
#ifdef DUAL_IAP
    out[0] = 0x81;
#else
    out[0] = 1;
#endif
    out[1] = o->command;
    out[2] = o->result;
    out[3] = o->state;
    store_le32(out + 4, o->offset);
    store_le32(out + 8, o->image.size);
    store_le32(out + 12, o->image.crc);
    store_le32(out + 16, FW_VERSION);
}
