#ifndef DUAL_HID_ROUTER_H
#define DUAL_HID_ROUTER_H

#include <stdint.h>

#ifndef HID_HAS_TOUCH
#define HID_HAS_TOUCH 0
#endif
#define HID_INPUT_COUNT (HID_HAS_TOUCH ? 3 : 2)

#define NO_CONNECTION 0xFFFFu
#define REPORT_QUEUE_SIZE 16u
#define TOUCH_REPORT_SIZE 7u
#define MAX_HID_REPORT_SIZE 8u
#define HID_SEND_PERIOD_MS 5u
enum { ROLE_NONE, ROLE_HID, ROLE_CONTROLLER };
enum { CMD_KEYBOARD = 1, CMD_MOUSE = 2, CMD_TOUCH = 3, CMD_RELEASE = 4,
       CMD_TOUCH_MODE = 5 };
enum {
    RESULT_OK, RESULT_LENGTH, RESULT_VALUE, RESULT_ROLE, RESULT_NOT_READY,
    RESULT_BUSY, RESULT_OFFSET, RESULT_FLASH, RESULT_CRC, RESULT_STATE
};

typedef struct {
    uint8_t id;
    uint8_t size;
    uint8_t data[MAX_HID_REPORT_SIZE];
} hid_report_t;

typedef struct {
    uint16_t hid;
    uint16_t controller;
    uint8_t head;
    uint8_t count;
#if HID_HAS_TOUCH
    uint8_t touch_sent[TOUCH_REPORT_SIZE];
#endif
    hid_report_t reports[REPORT_QUEUE_SIZE];
} router_t;

void router_init(router_t *router);
uint8_t router_connect(router_t *router, uint16_t handle);
uint8_t router_role(const router_t *router, uint16_t handle);
void router_disconnect(router_t *router, uint16_t handle);
void router_release(router_t *router);
uint8_t router_command(router_t *router, uint16_t handle, uint8_t ready_mask,
                       const uint8_t *data, uint16_t size);
const hid_report_t *router_peek(const router_t *router);
void router_pop(router_t *router);
void router_sent(router_t *router);

#endif
