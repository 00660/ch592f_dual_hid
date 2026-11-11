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
#define MACRO_STEP_SIZE 11u
#define MACRO_MAX_STEPS 64u
enum { ROLE_NONE, ROLE_HID, ROLE_CONTROLLER };
enum { CMD_KEYBOARD = 1, CMD_MOUSE = 2, CMD_TOUCH = 3, CMD_RELEASE = 4,
       CMD_TOUCH_MODE = 5 };
enum { CMD_MACRO_BEGIN = 0x10, CMD_MACRO_DATA = 0x11,
       CMD_MACRO_COMMIT = 0x12, CMD_MACRO_START = 0x13,
       CMD_MACRO_STOP = 0x14, CMD_MACRO_PAUSE = 0x15, CMD_MACRO_RESUME = 0x16 };
enum { MACRO_EMPTY, MACRO_UPLOADING, MACRO_READY, MACRO_RUNNING,
       MACRO_PAUSED, MACRO_PAUSING };
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
    uint8_t macro_data[MACRO_STEP_SIZE * MACRO_MAX_STEPS];
    uint8_t macro_state;
    uint8_t macro_steps;
    uint8_t macro_received;
    uint8_t macro_index;
    uint16_t macro_loops;
    uint32_t macro_delay;
    uint32_t macro_loop_delay;
    uint32_t macro_crc;
    uint16_t macro_owner;
    uint16_t macro_stall;
    uint8_t macro_result;
    uint8_t macro_required;
    uint8_t macro_held;
    uint32_t macro_random;
    int16_t macro_x_min, macro_x_max, macro_y_min, macro_y_max;
    int16_t macro_dx, macro_dy;
#if HID_HAS_TOUCH
    uint8_t touch_sent[TOUCH_REPORT_SIZE];
#endif
    hid_report_t reports[REPORT_QUEUE_SIZE];
} router_t;

void router_init(router_t *router);
uint8_t router_connect(router_t *router, uint16_t handle);
uint8_t router_role(const router_t *router, uint16_t handle);
uint8_t router_can_control(const router_t *router, uint16_t handle);
void router_disconnect(router_t *router, uint16_t handle);
void router_release(router_t *router);
uint8_t router_command(router_t *router, uint16_t handle, uint8_t ready_mask,
                       const uint8_t *data, uint16_t size);
void router_macro_tick(router_t *router, uint8_t ready_mask, uint8_t elapsed_ms);
void router_macro_stop(router_t *router);
const hid_report_t *router_peek(const router_t *router);
void router_pop(router_t *router);
void router_sent(router_t *router);

#endif
