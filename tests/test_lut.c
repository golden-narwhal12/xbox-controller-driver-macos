/*******************************************************************************
 * test_lut.c - Response-curve lookup table accuracy tests
 *
 * Tests the real curve_lookup() from src/input/input.c against powf().
 ******************************************************************************/

#include "test_framework.h"
#include "../include/input.h"
#include <math.h>

/*******************************************************************************
 * Event layer stubs (input.c links against these; nothing is injected here)
 ******************************************************************************/
void send_key_event(uint16_t keycode, bool pressed) { (void)keycode; (void)pressed; }
void send_mouse_button_event(int button, bool pressed) { (void)button; (void)pressed; }
void send_scroll_event(int lines) { (void)lines; }
void send_mouse_movement(float dx, float dy, bool streaming_mode,
                         bool left_down, bool right_down, bool middle_down) {
    (void)dx; (void)dy; (void)streaming_mode;
    (void)left_down; (void)right_down; (void)middle_down;
}

/*******************************************************************************
 * Curve Lookup Tests
 ******************************************************************************/
TEST(curve_endpoints_exact) {
    ASSERT_FLOAT_EQ(curve_lookup(0.0f, 1.8f), 0.0f, 0.0001f);
    ASSERT_FLOAT_EQ(curve_lookup(1.0f, 1.8f), 1.0f, 0.0001f);
    TEST_PASS();
}

TEST(curve_matches_powf_default_curve) {
    // Truncating to 1/256 steps: error is at most slope * 1/256 (< 0.01 for 1.8)
    float test_values[] = {0.1f, 0.25f, 0.33f, 0.5f, 0.75f, 0.9f, 0.99f};
    for (int i = 0; i < 7; i++) {
        float error = fabsf(curve_lookup(test_values[i], 1.8f) - powf(test_values[i], 1.8f));
        ASSERT(error < 0.01f);
    }
    TEST_PASS();
}

TEST(curve_rebuilds_for_other_curves) {
    float curves[] = {1.0f, 1.5f, 2.0f, 2.5f, 3.0f};
    for (int c = 0; c < 5; c++) {
        float error = fabsf(curve_lookup(0.6f, curves[c]) - powf(0.6f, curves[c]));
        ASSERT(error < 0.02f);
    }
    TEST_PASS();
}

TEST(curve_symmetric_for_negative_input) {
    ASSERT_FLOAT_EQ(curve_lookup(-0.5f, 1.8f), curve_lookup(0.5f, 1.8f), 0.0001f);
    TEST_PASS();
}

TEST(curve_clamps_beyond_full_tilt) {
    ASSERT_FLOAT_EQ(curve_lookup(1.5f, 1.8f), 1.0f, 0.0001f);
    TEST_PASS();
}

TEST(curve_is_monotonic) {
    float prev = -1.0f;
    for (int i = 0; i <= 100; i++) {
        float value = curve_lookup(i / 100.0f, 1.8f);
        ASSERT(value >= prev);
        prev = value;
    }
    TEST_PASS();
}

/*******************************************************************************
 * Main
 ******************************************************************************/
int main(void) {
    TEST_SUITE("Response Curve Lookup");

    RUN_TEST(curve_endpoints_exact);
    RUN_TEST(curve_matches_powf_default_curve);
    RUN_TEST(curve_rebuilds_for_other_curves);
    RUN_TEST(curve_symmetric_for_negative_input);
    RUN_TEST(curve_clamps_beyond_full_tilt);
    RUN_TEST(curve_is_monotonic);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
