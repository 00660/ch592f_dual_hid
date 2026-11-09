#ifndef DUAL_HID_WIRE_H
#define DUAL_HID_WIRE_H
#include "ota.h"

typedef struct {
    uint16_t position;
    uint16_t size;
    uint8_t bytes[OTA_MAX_PACKET + 8];
} wire_t;

void wire_reset(wire_t *wire);
int wire_feed(wire_t *wire, uint8_t byte);
uint16_t wire_encode(uint8_t *out, const uint8_t *payload, uint16_t size);

#endif
