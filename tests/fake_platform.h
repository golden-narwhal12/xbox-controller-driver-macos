/*******************************************************************************
 * fake_platform.h - Test double for include/platform.h, plus a test rig
 *
 * Tests link the real input, cursor, tick and config sources. This header
 * supplies the one thing they cannot use in a test: the OS. Every platform
 * call is recorded so tests can assert on exactly what would have been posted,
 * and the cursor, window, displays and clock are scripted by the test.
 *
 * Include from exactly one .c file per test binary.
 ******************************************************************************/

#ifndef FAKE_PLATFORM_H
#define FAKE_PLATFORM_H

#include "../include/config.h"
#include "../include/cursor.h"
#include "../include/input.h"
#include "../include/platform.h"
#include <string.h>

#define UNUSED __attribute__((unused))

/*******************************************************************************
 * Recorded platform calls
 ******************************************************************************/
typedef enum { FAKE_KEY, FAKE_BUTTON, FAKE_MOVE, FAKE_SCROLL, FAKE_WARP } FakeEventType;

typedef struct {
    FakeEventType type;
    int code;               // keycode or mouse button
    bool pressed;
    ScreenPoint position;   // FAKE_BUTTON, FAKE_MOVE, FAKE_WARP
    int dx, dy;             // FAKE_MOVE delta fields
    int drag_button;        // FAKE_MOVE
    int clicks;             // FAKE_BUTTON click count
    int lines;              // FAKE_SCROLL
    uint64_t time_us;
} FakeEvent;

#define FAKE_LOG_CAPACITY 8192
#define FAKE_MAX_DISPLAYS 4

static struct {
    /* Scripted by the test */
    ScreenPoint cursor;
    bool cursor_frozen;             // A game holding the cursor still: posts don't move it
    WindowTarget window;
    ScreenRect displays[FAKE_MAX_DISPLAYS];
    int display_count;
    uint64_t now_us;

    /* Recorded */
    FakeEvent log[FAKE_LOG_CAPACITY];
    int log_count;
    int key_down[KEY_COUNT], key_up[KEY_COUNT];
    int mouse_down[MOUSE_BUTTON_COUNT], mouse_up[MOUSE_BUTTON_COUNT];
    int moves, warps, scroll_up, scroll_down;
    int window_queries;
} fake;

static void fake_record(FakeEvent event) {
    event.time_us = fake.now_us;
    if (fake.log_count < FAKE_LOG_CAPACITY) fake.log[fake.log_count++] = event;
}

static void fake_reset(void) {
    memset(&fake, 0, sizeof(fake));
    fake.cursor = (ScreenPoint){500, 500};
    fake.displays[0] = (ScreenRect){0, 0, 1920, 1080};
    fake.display_count = 1;
    fake.now_us = 1000000;
}

/* Forget what has been recorded so far; keep the scripted world as it is. */
UNUSED static void fake_clear_log(void) {
    fake.log_count = 0;
    memset(fake.key_down, 0, sizeof(fake.key_down));
    memset(fake.key_up, 0, sizeof(fake.key_up));
    memset(fake.mouse_down, 0, sizeof(fake.mouse_down));
    memset(fake.mouse_up, 0, sizeof(fake.mouse_up));
    fake.moves = fake.warps = fake.scroll_up = fake.scroll_down = 0;
}

/* Publish a new target window the way the tracker does: bump the generation. */
UNUSED static void fake_set_window(double x, double y, double w, double h,
                                   uint64_t window_id, int pid) {
    uint32_t generation = fake.window.generation + 1;
    fake.window = (WindowTarget){.valid = true, .frame = {x, y, w, h}, .pid = pid,
                                 .window_id = window_id, .generation = generation};
}

UNUSED static void fake_clear_window(void) {
    uint32_t generation = fake.window.generation + 1;
    fake.window = (WindowTarget){.valid = false, .generation = generation};
}

UNUSED static const FakeEvent *fake_last(FakeEventType type) {
    for (int i = fake.log_count - 1; i >= 0; i--) {
        if (fake.log[i].type == type) return &fake.log[i];
    }
    return NULL;
}

UNUSED static int fake_count(FakeEventType type) {
    int count = 0;
    for (int i = 0; i < fake.log_count; i++) count += fake.log[i].type == type;
    return count;
}

UNUSED static bool rect_contains(ScreenRect rect, ScreenPoint p) {
    return p.x >= rect.x && p.x <= rect.x + rect.w - 1 &&
           p.y >= rect.y && p.y <= rect.y + rect.h - 1;
}

/*******************************************************************************
 * platform.h implementation
 ******************************************************************************/
void platform_post_key(uint16_t keycode, bool pressed) {
    if (keycode < KEY_COUNT) (pressed ? fake.key_down : fake.key_up)[keycode]++;
    fake_record((FakeEvent){.type = FAKE_KEY, .code = keycode, .pressed = pressed});
}

void platform_post_mouse_button(int button, bool pressed, ScreenPoint position, int click_count) {
    (pressed ? fake.mouse_down : fake.mouse_up)[button]++;
    if (!fake.cursor_frozen) fake.cursor = position;
    fake_record((FakeEvent){.type = FAKE_BUTTON, .code = button, .pressed = pressed,
                            .position = position, .clicks = click_count});
}

void platform_post_mouse_move(ScreenPoint position, int delta_x, int delta_y, int drag_button) {
    fake.moves++;
    if (!fake.cursor_frozen) fake.cursor = position;
    fake_record((FakeEvent){.type = FAKE_MOVE, .position = position, .dx = delta_x,
                            .dy = delta_y, .drag_button = drag_button});
}

void platform_post_scroll(int lines) {
    if (lines > 0) fake.scroll_up += lines;
    else fake.scroll_down -= lines;
    fake_record((FakeEvent){.type = FAKE_SCROLL, .lines = lines});
}

ScreenPoint platform_cursor_position(void) {
    return fake.cursor;
}

void platform_warp_cursor(ScreenPoint position) {
    fake.warps++;
    fake.cursor = position;
    fake_record((FakeEvent){.type = FAKE_WARP, .position = position});
}

bool platform_point_on_display(ScreenPoint position) {
    for (int i = 0; i < fake.display_count; i++) {
        if (rect_contains(fake.displays[i], position)) return true;
    }
    return false;
}

void platform_target_window(WindowTarget *out) {
    fake.window_queries++;
    *out = fake.window;
}

uint64_t platform_now_us(void) {
    return fake.now_us;
}

void platform_sleep_until_us(uint64_t deadline_us) {
    if (deadline_us > fake.now_us) fake.now_us = deadline_us;
}

/*******************************************************************************
 * Test rig: the driver's data path without threads or USB.
 *
 *   raw report bytes -> gip_parse_input -> process_input -> cursor_dispatch
 *   elapsed time     -> motion_tick     ->                  cursor_dispatch
 ******************************************************************************/
#define RIG_OWN_PID 4242

typedef struct {
    InputState state;
    ControllerMapping config;
    CursorState cursor;
    OutQueue last_queue;        // Events from the most recent report or tick
    int dropped;                // Queue overflows seen (must stay 0)
    int press_edges;            // From the most recent report
    float move_dx, move_dy;     // Requested motion from the most recent tick
} Rig;

/* Defaults, with rumble off and cursor confinement off so input tests see
   plain motion. Cursor tests switch confinement on explicitly. */
UNUSED static void rig_init(Rig *rig) {
    fake_reset();
    memset(rig, 0, sizeof(*rig));
    input_state_init(&rig->state);
    config_get_defaults(&rig->config);
    rig->config.rumble.enabled = false;
    rig->config.cursor.confine = CONFINE_OFF;
    cursor_state_init(&rig->cursor, RIG_OWN_PID);
}

UNUSED static void rig_flush(Rig *rig, const OutQueue *queue) {
    rig->last_queue = *queue;
    rig->dropped += queue->dropped;
    cursor_dispatch(&rig->cursor, &rig->config.cursor, queue);
}

static void put_le16(uint8_t *p, int value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
}

/* Builds the bytes of a GIP input report exactly as the controller sends it. */
UNUSED static void make_report(uint8_t report[GIP_INPUT_LENGTH], uint16_t buttons,
                               int left_trigger, int right_trigger,
                               int left_x, int left_y, int right_x, int right_y) {
    memset(report, 0, GIP_INPUT_LENGTH);
    report[0] = GIP_CMD_INPUT;
    report[3] = GIP_INPUT_LENGTH - sizeof(GipHeader);
    put_le16(report + GIP_INPUT_OFF_BUTTONS, buttons);
    put_le16(report + GIP_INPUT_OFF_LEFT_TRIGGER, left_trigger);
    put_le16(report + GIP_INPUT_OFF_RIGHT_TRIGGER, right_trigger);
    put_le16(report + GIP_INPUT_OFF_LEFT_X, left_x);
    put_le16(report + GIP_INPUT_OFF_LEFT_Y, left_y);
    put_le16(report + GIP_INPUT_OFF_RIGHT_X, right_x);
    put_le16(report + GIP_INPUT_OFF_RIGHT_Y, right_y);
}

/* Feed one report, raw bytes in. Stick Y is in controller convention (+up). */
UNUSED static bool rig_report(Rig *rig, uint16_t buttons, int left_trigger, int right_trigger,
                              int left_x, int left_y, int right_x, int right_y) {
    uint8_t bytes[GIP_INPUT_LENGTH];
    make_report(bytes, buttons, left_trigger, right_trigger, left_x, left_y, right_x, right_y);
    GipInput input;
    if (!gip_parse_input(bytes, sizeof(bytes), &input)) return false;

    OutQueue queue;
    out_queue_init(&queue);
    rig->press_edges = process_input(&input, &rig->state, &rig->config, &queue);
    rig_flush(rig, &queue);
    return true;
}

/* Advance the clock and run one motion tick. */
UNUSED static void rig_tick(Rig *rig, float dt_ms) {
    if (dt_ms > 0.0f) fake.now_us += (uint64_t)(dt_ms * 1000.0f);
    OutQueue queue;
    out_queue_init(&queue);
    motion_tick(&rig->state, &rig->config, dt_ms, &queue);
    rig->move_dx = rig->move_dy = 0.0f;
    for (int i = 0; i < queue.count; i++) {
        if (queue.events[i].type == OUT_MOUSE_MOVE) {
            rig->move_dx += queue.events[i].dx;
            rig->move_dy += queue.events[i].dy;
        }
    }
    rig_flush(rig, &queue);
}

UNUSED static void rig_release_all(Rig *rig) {
    OutQueue queue;
    out_queue_init(&queue);
    input_state_release_all(&rig->state, &queue);
    rig_flush(rig, &queue);
}

/* Small deterministic PRNG for jittered timing and fuzzing (not rand(): the
   sequence must be the same on every run and platform). */
UNUSED static uint32_t test_random(uint32_t *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed >> 8;
}

#endif // FAKE_PLATFORM_H
