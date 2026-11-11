#include "router.h"
#include "layout.h"
#include <string.h>

void router_init(router_t *r)
{
    memset(r, 0, sizeof(*r));
    r->hid = r->controller = NO_CONNECTION;
    r->macro_owner = NO_CONNECTION;
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
        r->macro_state = MACRO_EMPTY;
        r->macro_steps = r->macro_received = r->macro_index = 0;
        r->macro_delay = r->macro_loop_delay = r->macro_loops = 0;
        r->macro_owner = NO_CONNECTION;
        r->macro_held = 0;
#if HID_HAS_TOUCH
        memset(r->touch_sent, 0, sizeof(r->touch_sent));
#endif
    } else if (h == r->controller) {
        r->controller = NO_CONNECTION;
        if (r->macro_state == MACRO_UPLOADING && r->macro_owner == h) {
            r->macro_state = MACRO_EMPTY;
            r->macro_owner = NO_CONNECTION;
        }
        if (r->macro_state != MACRO_RUNNING && r->macro_state != MACRO_PAUSING)
            router_release(r);
    }
}

static uint16_t load_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint8_t control_role(const router_t *r, uint16_t h)
{
    uint8_t role = router_role(r, h);
    return role == ROLE_CONTROLLER || role == ROLE_HID;
}

static uint8_t macro_validate(uint8_t ready, const uint8_t *step)
{
    uint8_t id = step[2];
    const uint8_t *data = step + 3;
    if (!id) return RESULT_OK;
    if (id != CMD_KEYBOARD && id != CMD_MOUSE && id != CMD_TOUCH)
        return RESULT_VALUE;
#if !HID_HAS_TOUCH
    if (id == CMD_TOUCH) return RESULT_VALUE;
#endif
    if (!(ready & (1u << (id - 1)))) return RESULT_NOT_READY;
    if (id == CMD_KEYBOARD) {
        unsigned i;
        if (data[1] != 0) return RESULT_VALUE;
        for (i = 2; i < 8; ++i) if (data[i] > 101) return RESULT_VALUE;
    } else if (id == CMD_MOUSE) {
        if ((data[0] & 0xE0) || data[5] == 0x80 || data[6] == 0x80)
            return RESULT_VALUE;
#if HID_HAS_TOUCH
    } else {
        uint16_t x = load_le16(data + 2), y = load_le16(data + 4);
        if (data[0] > 1 || data[1] != 0 || x > 32767 || y > 32767 ||
            data[6] != 1) return RESULT_VALUE;
#endif
    }
    return RESULT_OK;
}

static uint8_t held_after(uint8_t held, uint8_t id, const uint8_t *data)
{
    uint8_t down = 0, mask;
    unsigned i;
    if (!id) return held;
    mask = (uint8_t)(1u << (id - 1));
    if (id == CMD_KEYBOARD) {
        for (i = 0; i < 8; ++i) down |= data[i];
    } else down = data[0];
    return down ? held | mask : held & (uint8_t)~mask;
}

static int16_t random_offset(router_t *r, int16_t low, int16_t high)
{
    uint32_t value = r->macro_random ? r->macro_random : 0x592F0301u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    r->macro_random = value;
    return (int16_t)(low + (int32_t)(value % ((int32_t)high - low + 1)));
}

static void macro_offsets(router_t *r)
{
    r->macro_dx = random_offset(r,r->macro_x_min,r->macro_x_max);
    r->macro_dy = random_offset(r,r->macro_y_min,r->macro_y_max);
}

static void macro_bounds(router_t *r, uint16_t radius_x, uint16_t radius_y)
{
    unsigned i;
    r->macro_x_min = -(int16_t)radius_x;
    r->macro_x_max = (int16_t)radius_x;
    r->macro_y_min = -(int16_t)radius_y;
    r->macro_y_max = (int16_t)radius_y;
    for (i = 0; i < r->macro_steps; ++i) {
        const uint8_t *step = r->macro_data + i * MACRO_STEP_SIZE;
        int16_t x, y;
        if (step[2] != CMD_TOUCH) continue;
        x = (int16_t)load_le16(step + 5);
        y = (int16_t)load_le16(step + 7);
        if (-x > r->macro_x_min) r->macro_x_min = -x;
        if (32767 - x < r->macro_x_max) r->macro_x_max = 32767 - x;
        if (-y > r->macro_y_min) r->macro_y_min = -y;
        if (32767 - y < r->macro_y_max) r->macro_y_max = 32767 - y;
    }
    macro_offsets(r);
}

static uint8_t macro_command(router_t *r, uint16_t h, uint8_t ready,
                             const uint8_t *p, uint16_t n)
{
    uint8_t status, held;
    unsigned i;
    uint16_t offset;
    switch (p[0]) {
    case CMD_MACRO_BEGIN:
        if (n != 6) return RESULT_LENGTH;
        if (r->macro_state >= MACRO_RUNNING || r->count) return RESULT_BUSY;
        if (r->macro_state == MACRO_UPLOADING && r->macro_owner != h) return RESULT_BUSY;
        if (!p[1] || p[1] > MACRO_MAX_STEPS) return RESULT_VALUE;
        if (r->hid == NO_CONNECTION) return RESULT_NOT_READY;
        r->macro_steps = p[1];
        r->macro_received = r->macro_index = 0;
        r->macro_delay = r->macro_loops = 0;
        r->macro_crc = load_le32(p + 2);
        r->macro_owner = h;
        r->macro_result = RESULT_OK;
        r->macro_required = r->macro_held = 0;
        r->macro_state = MACRO_UPLOADING;
        return RESULT_OK;
    case CMD_MACRO_DATA:
        if (n != MACRO_STEP_SIZE + 3u) return RESULT_LENGTH;
        if (r->macro_state != MACRO_UPLOADING) return RESULT_STATE;
        if (r->macro_owner != h) return RESULT_BUSY;
        offset = load_le16(p + 1);
        if (offset % MACRO_STEP_SIZE == 0 && offset < (uint16_t)r->macro_received * MACRO_STEP_SIZE)
            return memcmp(r->macro_data + offset,p + 3,MACRO_STEP_SIZE) ? RESULT_OFFSET : RESULT_OK;
        if (offset != (uint16_t)r->macro_received * MACRO_STEP_SIZE ||
            offset + MACRO_STEP_SIZE > (uint16_t)r->macro_steps * MACRO_STEP_SIZE)
            return RESULT_OFFSET;
        status = macro_validate(ready, p + 3);
        if (status != RESULT_OK) return status;
        memcpy(r->macro_data + offset, p + 3, MACRO_STEP_SIZE);
        ++r->macro_received;
        return RESULT_OK;
    case CMD_MACRO_COMMIT:
        if (n != 1) return RESULT_LENGTH;
        if (r->macro_state != MACRO_UPLOADING ||
            r->macro_received != r->macro_steps) return RESULT_STATE;
        if (r->macro_owner != h) return RESULT_BUSY;
        if (crc32(r->macro_data,
                  (uint32_t)r->macro_steps * MACRO_STEP_SIZE) != r->macro_crc)
            return RESULT_CRC;
        held = 0;
        for (i = 0; i < r->macro_steps; ++i) {
            const uint8_t *step = r->macro_data + i * MACRO_STEP_SIZE;
            held = held_after(held,step[2],step + 3);
            if (step[2]) r->macro_required |= (uint8_t)(1u << (step[2] - 1));
        }
        if (held || !r->macro_required) return RESULT_VALUE;
        r->macro_state = MACRO_READY;
        return RESULT_OK;
    case CMD_MACRO_START:
        if (n != 3 && n != 5 && n != 9 && n != 13) return RESULT_LENGTH;
        if (n == 9 && (load_le16(p + 5) > 32767 || load_le16(p + 7) > 32767))
            return RESULT_VALUE;
        if (n == 13 && (load_le16(p + 5) > 32767 || load_le16(p + 7) > 32767 ||
                        load_le32(p + 9) > 3600000u))
            return RESULT_VALUE;
        if (r->macro_state != MACRO_READY || r->hid == NO_CONNECTION)
            return RESULT_STATE;
        if ((ready & r->macro_required) != r->macro_required) return RESULT_NOT_READY;
        if (r->count) return RESULT_BUSY;
        r->macro_loops = load_le16(p + 1);
        r->macro_index = 0;
        r->macro_delay = n >= 5 ? load_le16(p + 3) : 0;
        r->macro_loop_delay = n == 13 ? load_le32(p + 9) : 0;
        macro_bounds(r,n >= 9 ? load_le16(p + 5) : 0,n >= 9 ? load_le16(p + 7) : 0);
        r->macro_result = RESULT_OK;
        r->macro_stall = r->macro_held = 0;
        r->macro_state = MACRO_RUNNING;
        return RESULT_OK;
    case CMD_MACRO_STOP:
        if (n != 1) return RESULT_LENGTH;
        router_macro_stop(r);
        return RESULT_OK;
    case CMD_MACRO_PAUSE:
        if (n != 1) return RESULT_LENGTH;
        if (r->macro_state != MACRO_RUNNING) return RESULT_STATE;
        r->macro_state = MACRO_PAUSING;
        return RESULT_OK;
    case CMD_MACRO_RESUME:
        if (n != 1) return RESULT_LENGTH;
        if (r->macro_state != MACRO_PAUSED) return RESULT_STATE;
        if ((ready & r->macro_required) != r->macro_required) return RESULT_NOT_READY;
        r->macro_state = MACRO_RUNNING;
        return RESULT_OK;
    default:
        return RESULT_VALUE;
    }
}

uint8_t router_command(router_t *r, uint16_t h, uint8_t ready,
                       const uint8_t *p, uint16_t n)
{
    uint8_t id, size;
    if (!control_role(r, h)) return RESULT_ROLE;
    if (!n) return RESULT_LENGTH;
    id = p[0];
    if (id >= CMD_MACRO_BEGIN && id <= CMD_MACRO_RESUME) {
        uint8_t result = macro_command(r,h,ready,p,n);
        r->macro_result = result;
        return result;
    }
    if (id == CMD_RELEASE) {
        if (n != 1) return RESULT_LENGTH;
        router_macro_stop(r);
        router_release(r);
        return RESULT_OK;
    }
    if (r->macro_state >= MACRO_RUNNING) return RESULT_BUSY;
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

void router_macro_stop(router_t *r)
{
    if (r->macro_state != MACRO_EMPTY) {
        r->macro_state = r->macro_state == MACRO_UPLOADING ? MACRO_EMPTY : MACRO_READY;
        r->macro_index = 0;
        r->macro_delay = 0;
        router_release(r);
    }
}

void router_macro_tick(router_t *r, uint8_t ready, uint8_t elapsed_ms)
{
    uint8_t *step, id, size, status;
    uint16_t delay;
    if (r->macro_state != MACRO_RUNNING && r->macro_state != MACRO_PAUSING) return;
    if ((ready & r->macro_required) != r->macro_required) {
        router_macro_stop(r);
        r->macro_result = RESULT_NOT_READY;
        return;
    }
    if (r->hid == NO_CONNECTION) {
        r->macro_state = MACRO_EMPTY;
        r->macro_steps = r->macro_received = r->macro_index = 0;
        r->macro_delay = 0;
        return;
    }
    if (r->count) {
        r->macro_stall += elapsed_ms;
        if (r->macro_stall >= 5000) {
            router_macro_stop(r);
            r->macro_result = RESULT_BUSY;
        }
        return;
    }
    r->macro_stall = 0;
    if (r->macro_state == MACRO_PAUSING && !r->macro_held) {
        r->macro_state = MACRO_PAUSED;
        return;
    }
    if (r->macro_delay) {
        if (elapsed_ms >= r->macro_delay) r->macro_delay = 0;
        else {
            r->macro_delay -= elapsed_ms;
            return;
        }
    }
    if (r->macro_index == r->macro_steps) {
        if (!r->macro_loops || r->macro_loops > 1) {
            if (r->macro_loops) --r->macro_loops;
            r->macro_index = 0;
            macro_offsets(r);
            r->macro_delay = r->macro_loop_delay;
            return;
        }
        r->macro_state = MACRO_READY;
        r->macro_index = 0;
        router_release(r);
        return;
    }
    step = r->macro_data + (uint16_t)r->macro_index * MACRO_STEP_SIZE;
    delay = load_le16(step);
    id = step[2];
    if (id) {
        status = macro_validate(ready, step);
        if (status != RESULT_OK) {
            r->macro_state = MACRO_READY;
            r->macro_index = 0;
            router_release(r);
            return;
        }
        size = id == CMD_KEYBOARD ? 8 : 7;
        hid_report_t *report = enqueue(r, id, step + 3, size);
        if (id == CMD_TOUCH) {
            uint16_t x = (uint16_t)(load_le16(step + 5) + r->macro_dx);
            uint16_t y = (uint16_t)(load_le16(step + 7) + r->macro_dy);
            report->data[2] = (uint8_t)x;
            report->data[3] = (uint8_t)(x >> 8);
            report->data[4] = (uint8_t)y;
            report->data[5] = (uint8_t)(y >> 8);
        }
    }
    r->macro_delay = delay;
    ++r->macro_index;
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
    if (report) r->macro_held = held_after(r->macro_held,report->id,report->data);
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
