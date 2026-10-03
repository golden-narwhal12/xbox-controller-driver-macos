#include "test_framework.h"
#include "../include/config.h"
#include "../include/input.h"
#include "../include/usb.h"
#include <stddef.h>

_Static_assert(sizeof(GipInputPacket) == 18, "unexpected GIP input packet size");
_Static_assert(offsetof(GipInputPacket, left_trigger) == 6, "left trigger offset");
_Static_assert(offsetof(GipInputPacket, right_trigger) == 8, "right trigger offset");
_Static_assert(offsetof(GipInputPacket, left_stick_x) == 10, "left X offset");
_Static_assert(offsetof(GipInputPacket, left_stick_y) == 12, "left Y offset");
_Static_assert(sizeof(GipRumblePacket) == 13, "unexpected GIP rumble packet size");
_Static_assert(offsetof(GipRumblePacket, enable) == 5, "rumble enable offset");

static int key_down[256], key_up[256];
static int mouse_down[3], mouse_up[3], moves;
static bool last_move_left, last_move_right;

void send_key_event(uint16_t code, bool pressed) {
    if (code < 256) (pressed ? key_down : key_up)[code]++;
}

void send_mouse_button_event(int button, bool pressed) {
    (pressed ? mouse_down : mouse_up)[button]++;
}

void send_mouse_movement(float dx, float dy, bool streaming_mode,
                         bool left_down, bool right_down, bool middle_down) {
    (void)dx; (void)dy; (void)streaming_mode; (void)middle_down;
    moves++;
    last_move_left = left_down;
    last_move_right = right_down;
}

static void setup(InputState *state, ControllerMapping *mapping) {
    memset(key_down, 0, sizeof(key_down));
    memset(key_up, 0, sizeof(key_up));
    memset(mouse_down, 0, sizeof(mouse_down));
    memset(mouse_up, 0, sizeof(mouse_up));
    moves = 0;
    last_move_left = last_move_right = false;
    input_state_init(state);
    config_get_defaults(mapping);
    mapping->features.rumble.enabled = false;
}

TEST(trigger_holds_across_pressure_bytes) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    process_triggers(512, 0, &state, &mapping);
    process_triggers(768, 0, &state, &mapping);
    process_triggers(1023, 0, &state, &mapping);
    ASSERT_EQ(mouse_down[0], 1);
    ASSERT_EQ(mouse_up[0], 0);
    ASSERT_TRUE(state.mouse_left);

    process_triggers(0, 0, &state, &mapping);
    ASSERT_EQ(mouse_up[0], 1);
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
    mapping.buttons.key_a = 0xFFFE;

    process_buttons(XBOX_BTN_A, &state, &mapping, NULL, NULL);
    process_triggers(1023, 0, &state, &mapping);
    ASSERT_EQ(mouse_down[0], 1);
    process_buttons(0, &state, &mapping, NULL, NULL);
    ASSERT_EQ(mouse_up[0], 0);
    input_state_release_all(&state);
    ASSERT_EQ(mouse_up[0], 1);
    ASSERT_FALSE(state.mouse_left);
    ASSERT_EQ(state.prev_left_trigger, 0);
    TEST_PASS();
}

TEST(continuous_look_and_drag) {
    InputState state;
    ControllerMapping mapping;
    setup(&state, &mapping);

    process_triggers(1023, 0, &state, &mapping);
    process_sticks(0, 0, 28000, 0, &state, &mapping, true);
    ASSERT_EQ(moves, 0);
    generate_continuous_movement(&state, &mapping, true);
    generate_continuous_movement(&state, &mapping, true);
    ASSERT_EQ(moves, 2);
    ASSERT_TRUE(last_move_left);
    ASSERT_FALSE(last_move_right);

    process_triggers(0, 0, &state, &mapping);
    generate_continuous_movement(&state, &mapping, true);
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

int main(void) {
    TEST_SUITE("Runtime Input");
    RUN_TEST(trigger_holds_across_pressure_bytes);
    RUN_TEST(overlapping_key_sources);
    RUN_TEST(button_mouse_binding_and_release_all);
    RUN_TEST(continuous_look_and_drag);
    RUN_TEST(right_stick_uses_right_mapping);
    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
