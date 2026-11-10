#ifndef DUAL_HID_STORAGE_H
#define DUAL_HID_STORAGE_H

#include "layout.h"

int storage_erase(uint32_t address, uint32_t size);
int storage_write(uint32_t address, const void *data, uint32_t size);
void storage_read(uint32_t address, void *data, uint32_t size);
void storage_meta_read(uint32_t address, image_meta_t *meta);
int storage_meta_write(uint32_t address, const image_meta_t *meta);
int storage_meta_clear(uint32_t address);
uint32_t storage_crc(uint32_t address, uint32_t size);

#endif
