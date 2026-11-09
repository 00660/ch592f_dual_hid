#include "wire.h"
#include <string.h>

void wire_reset(wire_t *w)
{
    w->position = w->size = 0;
}

int wire_feed(wire_t *w, uint8_t byte)
{
    if (w->position == 0 && byte != 0xA5) return 0;
    if (w->position == 1 && byte != 0x5A) {
        w->position = byte == 0xA5 ? 1 : 0;
        return 0;
    }
    w->bytes[w->position++] = byte;
    if (w->position == 4) {
        w->size = w->bytes[2] | ((uint16_t)w->bytes[3] << 8);
        if (!w->size || w->size > OTA_MAX_PACKET) {
            wire_reset(w);
            return -1;
        }
    }
    if (w->position >= 8 && w->position == w->size + 8) {
        uint16_t size = w->size;
        int valid = crc32(w->bytes + 4, size) == load_le32(w->bytes + 4 + size);
        wire_reset(w);
        return valid ? size : -1;
    }
    return 0;
}

uint16_t wire_encode(uint8_t *out, const uint8_t *p, uint16_t size)
{
    out[0] = 0xA5;
    out[1] = 0x5A;
    out[2] = (uint8_t)size;
    out[3] = (uint8_t)(size >> 8);
    memcpy(out + 4, p, size);
    store_le32(out + 4 + size, crc32(p, size));
    return size + 8;
}
