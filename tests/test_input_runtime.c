#include "test_framework.h"
#include <math.h>
#include "../include/config.h"
#include "../include/input.h"
#include "../include/event.h"

/* Packet layout is checked at compile time by _Static_asserts in gip_protocol.h. */

static int key_down[256], key_up[256];
static int mouse_down[3], mouse_up[3], moves;
static bool last_move_left, last_move_right;
static float last_dx, last_dy;
static int scroll_up_count, scroll_down_count;

void send_scroll_event(int lines) {
    if (lines > 0) scroll_up_count += lines;
    else scroll_down_count -= lines;
}

void send_key_event(uint16_t code, bool pressed) {
    if (code < 256) (pressed ? key_down : key_up)[code]++;
}

void send_mouse_button_event(int button, bool pressed) {
    (pressed ? mouse_down : mouse_up)[button]++;
}

void send_mouse_movement(float dx, float dy, bool streaming_mode,
                         bool left_down, bool right_down, bool middle_down) {
    (void)streaming_mode; (void)middle_down;
    moves++;
    last_dx = dx;
    last_dy = dy;
    last_move_left = left_down;
    last_move_right = right_down;
}

static GipInputPacket make_packet(void) {
    GipInputPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.command = GIP_CMD_INPUT;
    return packet;
}

static void setup(InputState *state, ControllerMapping *mapping) {
    memset(key_down, 0, sizeof(key_down));
    memset(key_up, 0, sizeof(key_up));
    memset(mouse_down, 0, sizeof(mouse_down));
    memset(mouse_up, 0, sizeof(mouse_up));
    moves = 0;
    last_dx = last_dy = 0.0f;
    scroll_up_count = scroll_down_count = 0;
    last_move_left = last_move_right = false;
    input_state_init(state);
    config_get_defaults(mapping);
    mapping->features.rumble.enabled = false;
}

TEST(trigger_holds_across_pressure_bytes) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    /* A slow squeeze of RT through its 10-bit range is one held click. */
    process_triggers(0, 512, &state, &mapping);
    process_triggers(0, 768, &state, &mapping);
    process_triggers(0, 1023, &state, &mapping);
    ASSERT_EQ(mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(mouse_up[MOUSE_BUTTON_LEFT], 0);
    ASSERT_TRUE(state.mouse_left);

    process_triggers(0, 0, &state, &mapping);
    ASSERT_EQ(mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_FALSE(state.mouse_left);
    TEST_PASS();
}

TEST(overlapping_key_sources) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);
    mapping.buttons.key_a = mapping.sticks.left_up;

    process_buttons(XBOX_BTN_A, &state, &mapping, NULL, NULL);
    process_sticks(0, -30000, 0, 0, &state, &mapping, false);
    ASSERT_EQ(key_down[mapping.buttons.key_a], 1);

    process_buttons(0, &state, &mapping, NULL, NULL);
    ASSERT_EQ(key_up[mapping.buttons.key_a], 0);
    process_sticks(0, 0, 0, 0, &state, &mapping, false);
    ASSERT_EQ(key_up[mapping.buttons.key_a], 1);
    TEST_PASS();
}

TEST(button_mouse_binding_and_release_all) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);
    mapping.buttons.key_a = BINDING_MOUSE_LEFT;

    process_buttons(XBOX_BTN_A, &state, &mapping, NULL, NULL);
    process_triggers(0, 1023, &state, &mapping);
    ASSERT_EQ(mouse_down[MOUSE_BUTTON_LEFT], 1);
    process_buttons(0, &state, &mapping, NULL, NULL);
    ASSERT_EQ(mouse_up[MOUSE_BUTTON_LEFT], 0);
    input_state_release_all(&state);
    ASSERT_EQ(mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_FALSE(state.mouse_left);
    TEST_PASS();
}

TEST(continuous_look_and_drag) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    process_triggers(0, 1023, &state, &mapping);
    process_sticks(0, 0, 28000, 0, &state, &mapping, true);
    ASSERT_EQ(moves, 0);
    generate_continuous_movement(&state, &mapping, true, MOTION_TICK_MS);
    generate_continuous_movement(&state, &mapping, true, MOTION_TICK_MS);
    ASSERT_EQ(moves, 2);
    ASSERT_TRUE(last_move_left);
    ASSERT_FALSE(last_move_right);

    process_triggers(0, 0, &state, &mapping);
    generate_continuous_movement(&state, &mapping, true, MOTION_TICK_MS);
    ASSERT_FALSE(last_move_left);
    TEST_PASS();
}

TEST(right_stick_uses_right_mapping) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);
    mapping.sticks.right_stick_mode = STICK_MODE_WASD;

    process_sticks(0, 0, 0, -30000, &state, &mapping, false);
    ASSERT_EQ(key_down[mapping.sticks.right_up], 1);
    ASSERT_EQ(key_down[mapping.sticks.left_up], 0);
    process_sticks(0, 0, 0, 0, &state, &mapping, false);
    ASSERT_EQ(key_up[mapping.sticks.right_up], 1);
    TEST_PASS();
}

static int rumbles;
static void count_rumble(void *ctx) { (void)ctx; rumbles++; }

/* Rumble fires once per press: not while held, not on release. */
TEST(button_rumble_once_per_press) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);
    mapping.features.rumble.enabled = true;
    mapping.features.rumble.button_feedback = true;
    rumbles = 0;

    process_buttons(XBOX_BTN_A, &state, &mapping, count_rumble, NULL);
    process_buttons(XBOX_BTN_A, &state, &mapping, count_rumble, NULL);
    ASSERT_EQ(rumbles, 1);
    process_buttons(XBOX_BTN_A | XBOX_BTN_B, &state, &mapping, count_rumble, NULL);
    ASSERT_EQ(rumbles, 2);
    process_buttons(0, &state, &mapping, count_rumble, NULL);
    ASSERT_EQ(rumbles, 2);
    TEST_PASS();
}

/* GIP reports stick +Y as up: pushing the left stick forward must press W. */
TEST(packet_left_stick_up_presses_w) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    GipInputPacket packet = make_packet();
    packet.left_stick_y = 30000;
    process_input_packet(&packet, &state, &mapping, NULL, NULL);
    ASSERT_EQ(key_down[mapping.sticks.left_up], 1);
    ASSERT_EQ(key_down[mapping.sticks.left_down], 0);
    TEST_PASS();
}

/* Pushing the right stick up must move the mouse up (negative screen dy). */
TEST(packet_right_stick_up_moves_mouse_up) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    GipInputPacket packet = make_packet();
    packet.right_stick_y = 30000;
    process_input_packet(&packet, &state, &mapping, NULL, NULL);
    generate_continuous_movement(&state, &mapping, false, MOTION_TICK_MS);
    ASSERT_EQ(moves, 1);
    ASSERT(last_dy < 0.0f);
    ASSERT_FLOAT_EQ(last_dx, 0.0f, 0.001f);
    TEST_PASS();
}

/* Fully down (-32768) must not overflow into "up" when Y is inverted. */
TEST(packet_extreme_y_does_not_overflow) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    GipInputPacket packet = make_packet();
    packet.left_stick_y = -32768;
    process_input_packet(&packet, &state, &mapping, NULL, NULL);
    ASSERT_EQ(key_down[mapping.sticks.left_down], 1);
    ASSERT_EQ(key_down[mapping.sticks.left_up], 0);
    TEST_PASS();
}

/* Default mapping: RT (bytes 8-9) is the primary/left click, LT the right click. */
TEST(right_trigger_is_primary_click) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    GipInputPacket packet = make_packet();
    packet.right_trigger = 1023;
    process_input_packet(&packet, &state, &mapping, NULL, NULL);
    ASSERT_EQ(mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(mouse_down[MOUSE_BUTTON_RIGHT], 0);

    packet = make_packet();
    packet.left_trigger = 1023;
    process_input_packet(&packet, &state, &mapping, NULL, NULL);
    ASSERT_EQ(mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(mouse_down[MOUSE_BUTTON_RIGHT], 1);
    TEST_PASS();
}

/* One 20 ms tick moves as far as two 10 ms ticks; stalls are clamped. */
TEST(motion_scales_with_elapsed_time) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);
    mapping.sticks.mouse_smoothing = 0.0f;  // isolate the gain from the filter

    process_sticks(0, 0, 20000, 0, &state, &mapping, false);
    generate_continuous_movement(&state, &mapping, false, 20.0f);
    float one_long_tick = last_dx;

    generate_continuous_movement(&state, &mapping, false, 10.0f);
    float two_short_ticks = last_dx;
    generate_continuous_movement(&state, &mapping, false, 10.0f);
    two_short_ticks += last_dx;

    ASSERT(one_long_tick > 0.0f);
    ASSERT_FLOAT_EQ(one_long_tick, two_short_ticks, 0.001f);

    generate_continuous_movement(&state, &mapping, false, 1000.0f);
    float stalled = last_dx;
    generate_continuous_movement(&state, &mapping, false, MOTION_MAX_DT_MS);
    ASSERT_FLOAT_EQ(stalled, last_dx, 0.001f);
    TEST_PASS();
}

/* A full diagonal turns as fast as full straight-right (curve is radial). */
TEST(diagonal_speed_matches_cardinal) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);
    mapping.sticks.mouse_smoothing = 0.0f;  // isolate the curve from the filter

    process_sticks(0, 0, 28000, 0, &state, &mapping, false);
    generate_continuous_movement(&state, &mapping, false, MOTION_TICK_MS);
    float straight = sqrtf(last_dx * last_dx + last_dy * last_dy);

    process_sticks(0, 0, 19799, 19799, &state, &mapping, false);  // same distance, 45 degrees
    generate_continuous_movement(&state, &mapping, false, MOTION_TICK_MS);
    float diagonal = sqrtf(last_dx * last_dx + last_dy * last_dy);

    ASSERT(straight > 0.0f);
    ASSERT_FLOAT_EQ(diagonal, straight, 0.01f * straight);
    ASSERT_FLOAT_EQ(last_dx, last_dy, 0.001f);
    TEST_PASS();
}

/* The smoothing filter settles the same amount per unit time, whatever the tick length. */
TEST(smoothing_is_time_consistent) {
    InputState one_tick, two_ticks;
    ControllerMapping mapping;
    setup(&one_tick, &mapping);
    input_state_init(&two_ticks);

    process_sticks(0, 0, 20000, 0, &one_tick, &mapping, false);
    process_sticks(0, 0, 20000, 0, &two_ticks, &mapping, false);
    generate_continuous_movement(&one_tick, &mapping, false, 20.0f);
    generate_continuous_movement(&two_ticks, &mapping, false, 10.0f);
    generate_continuous_movement(&two_ticks, &mapping, false, 10.0f);
    ASSERT_FLOAT_EQ(one_tick.smoothed_right_x, two_ticks.smoothed_right_x, 0.0001f);
    TEST_PASS();
}

/* Default bumpers scroll: a tap is one notch, holding repeats, release stops. */
TEST(bumpers_scroll_tap_and_hold) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    process_buttons(XBOX_BTN_RB, &state, &mapping, NULL, NULL);
    ASSERT_EQ(scroll_down_count, 1);
    process_buttons(0, &state, &mapping, NULL, NULL);
    generate_continuous_movement(&state, &mapping, false, 1000.0f);
    ASSERT_EQ(scroll_down_count, 1);           /* released: no repeats */

    /* Hold LB for 1 s in 10 ms ticks: 1 notch on press, repeats from 300 ms
       every 80 ms (300, 380, ..., 940) = 9 more */
    process_buttons(XBOX_BTN_LB, &state, &mapping, NULL, NULL);
    for (int i = 0; i < 100; i++)
        generate_continuous_movement(&state, &mapping, false, MOTION_TICK_MS);
    ASSERT_EQ(scroll_up_count, 10);
    process_buttons(0, &state, &mapping, NULL, NULL);
    for (int i = 0; i < 50; i++)
        generate_continuous_movement(&state, &mapping, false, MOTION_TICK_MS);
    ASSERT_EQ(scroll_up_count, 10);
    ASSERT_EQ(scroll_down_count, 1);
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Runtime Input");
    RUN_TEST(packet_left_stick_up_presses_w);
    RUN_TEST(packet_right_stick_up_moves_mouse_up);
    RUN_TEST(packet_extreme_y_does_not_overflow);
    RUN_TEST(right_trigger_is_primary_click);
    RUN_TEST(motion_scales_with_elapsed_time);
    RUN_TEST(smoothing_is_time_consistent);
    RUN_TEST(diagonal_speed_matches_cardinal);
    RUN_TEST(bumpers_scroll_tap_and_hold);
    RUN_TEST(button_rumble_once_per_press);
    RUN_TEST(trigger_holds_across_pressure_bytes);
    RUN_TEST(overlapping_key_sources);
    RUN_TEST(button_mouse_binding_and_release_all);
    RUN_TEST(continuous_look_and_drag);
    RUN_TEST(right_stick_uses_right_mapping);
    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
