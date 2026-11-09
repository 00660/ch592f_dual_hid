#include "router.h"
#include <string.h>

void router_init(router_t *r)
{
    memset(r, 0, sizeof(*r));
    r->hid = r->controller = NO_CONNECTION;
}

uint8_t router_role(const router_t *r, uint16_t h)
{
    if (h == NO_CONNECTION) return ROLE_NONE;
    if (h == r->hid) return ROLE_HID;
    if (h == r->controller) return ROLE_CONTROLLER;
    return ROLE_NONE;
}

uint8_t router_connect(router_t *r, uint16_t h)
{
    uint8_t role = router_role(r, h);
    if (role || h == NO_CONNECTION) return role;
    if (r->hid == NO_CONNECTION) {
        r->hid = h;
        return ROLE_HID;
    }
    if (r->controller == NO_CONNECTION) {
        r->controller = h;
        return ROLE_CONTROLLER;
    }
    return ROLE_NONE;
}

static hid_report_t *enqueue(router_t *r, uint8_t id, const uint8_t *data, uint8_t size)
{
    hid_report_t *report = &r->reports[(r->head + r->count) % REPORT_QUEUE_SIZE];
    report->id = id;
    report->size = size;
    memcpy(report->data, data, size);
    ++r->count;
    return report;
}

#if HID_HAS_TOUCH
static void latest_report(const router_t *r, uint8_t id,
                          const uint8_t *fallback, uint8_t *out, uint8_t size)
{
    unsigned i;
    memcpy(out, fallback, size);
    for (i = 0; i < r->count; ++i) {
        const hid_report_t *queued = &r->reports[(r->head + i) % REPORT_QUEUE_SIZE];
        if (queued->id == id) memcpy(out, queued->data, size);
    }
}

static void enqueue_touch(router_t *r, const uint8_t *command)
{
    uint8_t screen[TOUCH_REPORT_SIZE];
    if (command[1]) memcpy(screen, command + 1, sizeof(screen));
    else {
        latest_report(r, CMD_TOUCH, r->touch_sent, screen, sizeof(screen));
        screen[0] = 0;
        screen[6] = 1;
    }
    enqueue(r, CMD_TOUCH, screen, sizeof(screen));
}
#endif

void router_release(router_t *r)
{
    static const uint8_t zero[8] = {0};
#if HID_HAS_TOUCH
    uint8_t screen[TOUCH_REPORT_SIZE];
#endif

    r->head = r->count = 0;
    if (r->hid == NO_CONNECTION) return;
    enqueue(r, CMD_KEYBOARD, zero, 8);
    enqueue(r, CMD_MOUSE, zero, 7);
#if HID_HAS_TOUCH
    if (r->touch_sent[0] & 1u) {
        memcpy(screen, r->touch_sent, sizeof(screen));
        screen[0] = 0;
        enqueue(r, CMD_TOUCH, screen, sizeof(screen));
    }
#endif
}

void router_disconnect(router_t *r, uint16_t h)
{
    if (h == r->hid) {
        r->hid = NO_CONNECTION;
        r->head = r->count = 0;
#if HID_HAS_TOUCH
        memset(r->touch_sent, 0, sizeof(r->touch_sent));
#endif
    } else if (h == r->controller) {
        r->controller = NO_CONNECTION;
        router_release(r);
    }
}

uint8_t router_command(router_t *r, uint16_t h, uint8_t ready,
                       const uint8_t *p, uint16_t n)
{
    uint8_t id, size;
    if (router_role(r, h) != ROLE_CONTROLLER) return RESULT_ROLE;
    if (!n) return RESULT_LENGTH;
    id = p[0];
    if (id == CMD_RELEASE) {
        if (n != 1) return RESULT_LENGTH;
        router_release(r);
        return RESULT_OK;
    }
    if (id < CMD_KEYBOARD || id > CMD_TOUCH) return RESULT_VALUE;
#if !HID_HAS_TOUCH
    if (id == CMD_TOUCH) return RESULT_VALUE;
#endif
    size = (id == CMD_KEYBOARD) ? 8 : 7;
    if (n != (uint16_t)size + 1) return RESULT_LENGTH;
    if (r->hid == NO_CONNECTION) return RESULT_NOT_READY;
    if (!(ready & (1u << (id - 1))))
        return RESULT_NOT_READY;
    if (id == CMD_KEYBOARD) {
        unsigned i;
        if (p[2] != 0) return RESULT_VALUE;
        for (i = 3; i < 9; ++i) if (p[i] > 101) return RESULT_VALUE;
    }
    if (id == CMD_MOUSE && (p[1] & 0xE0)) return RESULT_VALUE;
    if (id == CMD_MOUSE && (p[6] == 0x80 || p[7] == 0x80)) return RESULT_VALUE;
#if HID_HAS_TOUCH
    if (id == CMD_TOUCH) {
        uint16_t x = p[3] | ((uint16_t)p[4] << 8);
        uint16_t y = p[5] | ((uint16_t)p[6] << 8);
        if (p[1] > 1 || p[2] != 0 || x > 32767 || y > 32767 ||
            p[7] != 1) return RESULT_VALUE;
        if (r->count == REPORT_QUEUE_SIZE)
            return RESULT_BUSY;
        enqueue_touch(r, p);
        return RESULT_OK;
    }
#endif
    if (r->count == REPORT_QUEUE_SIZE) return RESULT_BUSY;
    enqueue(r, id, p + 1, size);
    return RESULT_OK;
}

const hid_report_t *router_peek(const router_t *r)
{
    return r->count ? &r->reports[r->head] : 0;
}

void router_pop(router_t *r)
{
    if (r->count) {
        r->head = (uint8_t)((r->head + 1) % REPORT_QUEUE_SIZE);
        --r->count;
    }
}

void router_sent(router_t *r)
{
    const hid_report_t *report = router_peek(r);
#if HID_HAS_TOUCH
    if (report) {
        if (report->id == CMD_TOUCH)
            memcpy(r->touch_sent, report->data, sizeof(r->touch_sent));
    }
#else
    (void)report;
#endif
    router_pop(r);
}
