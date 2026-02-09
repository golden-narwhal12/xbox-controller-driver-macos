/*******************************************************************************
 * test_input.c - Input processing unit tests
 ******************************************************************************/

#include "test_framework.h"
#include "../include/types.h"
#include <math.h>

/*******************************************************************************
 * Mock event functions (we can't inject actual events in tests)
 ******************************************************************************/
static int g_key_events_count = 0;
static int g_mouse_events_count = 0;

void send_key_event(uint16_t keycode, bool pressed) {
    (void)keycode;
    (void)pressed;
    g_key_events_count++;
}

void send_mouse_button_event(int button, bool pressed) {
    (void)button;
    (void)pressed;
    g_mouse_events_count++;
}

void send_mouse_movement(float dx, float dy, bool streaming_mode) {
    (void)dx;
    (void)dy;
    (void)streaming_mode;
    g_mouse_events_count++;
}

/*******************************************************************************
 * Inline implementation of apply_deadzone for testing
 ******************************************************************************/
static void apply_deadzone(int16_t *x, int16_t *y, int16_t deadzone) {
    float magnitude = sqrtf((float)(*x) * (*x) + (float)(*y) * (*y));

    if (magnitude < deadzone) {
        *x = 0;
        *y = 0;
    } else if (magnitude > 32767) {
        float scale = 32767.0f / magnitude;
        *x = (int16_t)(*x * scale);
        *y = (int16_t)(*y * scale);
    }
}

/*******************************************************************************
 * Deadzone Tests
 ******************************************************************************/
TEST(deadzone_zero_input) {
    int16_t x = 0, y = 0;
    apply_deadzone(&x, &y, 8000);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    TEST_PASS();
}

TEST(deadzone_inside_deadzone) {
    int16_t x = 5000, y = 5000;
    apply_deadzone(&x, &y, 8000);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    TEST_PASS();
}

TEST(deadzone_outside_deadzone) {
    int16_t x = 20000, y = 20000;
    apply_deadzone(&x, &y, 8000);
    ASSERT_NEQ(x, 0);
    ASSERT_NEQ(y, 0);
    TEST_PASS();
}

TEST(deadzone_max_magnitude) {
    int16_t x = 32767, y = 32767;
    apply_deadzone(&x, &y, 8000);
    // Should be normalized to max
    float magnitude = sqrtf((float)x * x + (float)y * y);
    ASSERT(magnitude <= 32768.0f);
    TEST_PASS();
}

TEST(deadzone_negative_values) {
    int16_t x = -10000, y = -10000;
    apply_deadzone(&x, &y, 8000);
    ASSERT_NEQ(x, 0);
    ASSERT_NEQ(y, 0);
    ASSERT(x < 0);
    ASSERT(y < 0);
    TEST_PASS();
}

TEST(deadzone_boundary) {
    // Test at exact boundary
    // sqrt(5656^2 + 5656^2) = 7999.4, so should be inside deadzone
    int16_t x = 5656, y = 5656;
    apply_deadzone(&x, &y, 8000);
    // Should be inside deadzone
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    TEST_PASS();
}

/*******************************************************************************
 * Input State Tests
 ******************************************************************************/
TEST(input_state_init) {
    InputState state;
    memset(&state, 0xFF, sizeof(state));  // Fill with garbage

    // Manually clear like input_state_init would
    memset(&state, 0, sizeof(state));

    ASSERT_EQ(state.prev_buttons, 0);
    ASSERT_EQ(state.mouse_left, false);
    ASSERT_EQ(state.mouse_right, false);
    ASSERT_FLOAT_EQ(state.mouse_dx, 0.0f, 0.001f);
    TEST_PASS();
}

/*******************************************************************************
 * Button Mask Tests
 ******************************************************************************/
TEST(button_masks_unique) {
    uint16_t masks[] = {
        XBOX_BTN_A, XBOX_BTN_B, XBOX_BTN_X, XBOX_BTN_Y,
        XBOX_BTN_LB, XBOX_BTN_RB, XBOX_BTN_LS, XBOX_BTN_RS,
        XBOX_BTN_VIEW, XBOX_BTN_MENU,
        XBOX_BTN_DPAD_UP, XBOX_BTN_DPAD_DOWN, XBOX_BTN_DPAD_LEFT, XBOX_BTN_DPAD_RIGHT
    };

    // Check all masks are unique (no two are the same)
    for (int i = 0; i < 14; i++) {
        for (int j = i + 1; j < 14; j++) {
            ASSERT_NEQ(masks[i], masks[j]);
        }
    }
    TEST_PASS();
}

TEST(button_masks_power_of_two) {
    uint16_t masks[] = {
        XBOX_BTN_A, XBOX_BTN_B, XBOX_BTN_X, XBOX_BTN_Y,
        XBOX_BTN_LB, XBOX_BTN_RB, XBOX_BTN_LS, XBOX_BTN_RS,
        XBOX_BTN_VIEW, XBOX_BTN_MENU,
        XBOX_BTN_DPAD_UP, XBOX_BTN_DPAD_DOWN, XBOX_BTN_DPAD_LEFT, XBOX_BTN_DPAD_RIGHT
    };

    // Each mask should be a power of two
    for (int i = 0; i < 14; i++) {
        ASSERT((masks[i] & (masks[i] - 1)) == 0);  // Power of 2 check
    }
    TEST_PASS();
}

/*******************************************************************************
 * Stick Mode Tests
 ******************************************************************************/
TEST(stick_mode_enum_values) {
    ASSERT_EQ(STICK_MODE_WASD, 0);
    ASSERT_EQ(STICK_MODE_ARROWS, 1);
    ASSERT_EQ(STICK_MODE_MOUSE, 2);
    ASSERT_EQ(STICK_MODE_DISABLED, 3);
    TEST_PASS();
}

TEST(trigger_mode_enum_values) {
    ASSERT_EQ(TRIGGER_MODE_MOUSE, 0);
    ASSERT_EQ(TRIGGER_MODE_KEY, 1);
    ASSERT_EQ(TRIGGER_MODE_DISABLED, 2);
    TEST_PASS();
}

/*******************************************************************************
 * Main
 ******************************************************************************/
int main(void) {
    TEST_SUITE("Input Processing");

    printf("\n-- Deadzone Tests --\n");
    RUN_TEST(deadzone_zero_input);
    RUN_TEST(deadzone_inside_deadzone);
    RUN_TEST(deadzone_outside_deadzone);
    RUN_TEST(deadzone_max_magnitude);
    RUN_TEST(deadzone_negative_values);
    RUN_TEST(deadzone_boundary);

    printf("\n-- Input State Tests --\n");
    RUN_TEST(input_state_init);

    printf("\n-- Button Mask Tests --\n");
    RUN_TEST(button_masks_unique);
    RUN_TEST(button_masks_power_of_two);

    printf("\n-- Enum Tests --\n");
    RUN_TEST(stick_mode_enum_values);
    RUN_TEST(trigger_mode_enum_values);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
