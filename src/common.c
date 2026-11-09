#include "layout.h"
#include <stddef.h>

uint32_t crc32_update(uint32_t state, const void *data, uint32_t size)
{
    const uint8_t *p = data;
    while (size--) {
        unsigned bit;
        state ^= *p++;
        for (bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ (0xEDB88320u & (0u - (state & 1u)));
    }
    return state;
}

uint32_t crc32(const void *data, uint32_t size)
{
    return crc32_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void store_le32(uint8_t *p, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

int meta_valid(const image_meta_t *meta)
{
    return meta->magic == META_MAGIC && meta->board == BOARD_ID &&
           meta->size >= 256 && meta->size <= APP_CAPACITY &&
           (meta->size & 3u) == 0 &&
           meta->header_crc == crc32(meta, offsetof(image_meta_t, header_crc));
}

void meta_make(image_meta_t *meta, uint32_t size, uint32_t crc, uint32_t version)
{
    meta->magic = META_MAGIC;
    meta->board = BOARD_ID;
    meta->size = size;
    meta->crc = crc;
    meta->version = version;
    meta->header_crc = crc32(meta, offsetof(image_meta_t, header_crc));
}
