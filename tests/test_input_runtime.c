/*******************************************************************************
 * test_input_runtime.c - End to end: raw report bytes in, posted events out
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"

#define FULL 30000   // A firmly pushed stick

/* GIP reports stick +Y as up: pushing the left stick forward must press W. */
TEST(packet_left_stick_up_presses_w) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, 0, FULL, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.key_down[KEY_S], 0);
    TEST_PASS();
}

/* Pushing the right stick up must move the mouse up (negative screen dy). */
TEST(packet_right_stick_up_moves_mouse_up) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, FULL);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.moves, 1);
    ASSERT(rig.move_dy < 0.0f);
    ASSERT_FLOAT_EQ(rig.move_dx, 0.0f, 0.001f);
    ASSERT(fake_last(FAKE_MOVE)->dy < 0);
    ASSERT(fake.cursor.y < 500);
    TEST_PASS();
}

/* Fully down (-32768) must not overflow into "up" when Y is inverted. */
TEST(packet_extreme_y_does_not_overflow) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, 0, -32768, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_S], 1);
    ASSERT_EQ(fake.key_down[KEY_W], 0);

    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, -32768, 32767, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_A], 1);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.key_down[KEY_D], 0);
    TEST_PASS();
}

/* Default mapping: RT is the primary/left click, LT the right click. */
TEST(right_trigger_is_primary_click) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_RIGHT], 0);

    rig_report(&rig, 0, 1023, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_RIGHT], 1);
    TEST_PASS();
}

/* A slow squeeze of RT through its 10-bit range is one held click. */
TEST(trigger_holds_across_pressure_values) {
    Rig rig;
    rig_init(&rig);
    for (int pressure = 0; pressure <= 1023; pressure += 31) {
        rig_report(&rig, 0, 0, pressure, 0, 0, 0, 0);
    }
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 0);

    for (int pressure = 1023; pressure >= 0; pressure -= 31) {
        rig_report(&rig, 0, 0, pressure, 0, 0, 0, 0);
    }
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    TEST_PASS();
}

/* One 20 ms tick moves as far as two 10 ms ticks; stalls are clamped. */
TEST(motion_scales_with_elapsed_time) {
    Rig rig;
    rig_init(&rig);
    rig.config.mouse.smoothing = 0.0f;  // isolate the gain from the filter

    rig_report(&rig, 0, 0, 0, 0, 0, 20000, 0);
    rig_tick(&rig, 20.0f);
    float one_long_tick = rig.move_dx;

    rig_tick(&rig, 10.0f);
    float two_short_ticks = rig.move_dx;
    rig_tick(&rig, 10.0f);
    two_short_ticks += rig.move_dx;

    ASSERT(one_long_tick > 0.0f);
    ASSERT_FLOAT_EQ(one_long_tick, two_short_ticks, 0.001f);

    rig_tick(&rig, 1000.0f);
    float stalled = rig.move_dx;
    rig_tick(&rig, MOTION_MAX_DT_MS);
    ASSERT_FLOAT_EQ(stalled, rig.move_dx, 0.001f);

    // Zero, negative and NaN elapsed times do nothing
    fake_clear_log();
    rig_tick(&rig, 0.0f);
    rig_tick(&rig, -5.0f);
    rig_tick(&rig, NAN);
    ASSERT_EQ(fake.moves, 0);
    TEST_PASS();
}

/* Full tilt speed is sensitivity * MOUSE_PX_PER_TICK per tick: the deadzone
   rescale must not change top speed. */
TEST(full_tilt_speed_unchanged) {
    Rig rig;
    rig_init(&rig);
    rig.config.mouse.smoothing = 0.0f;
    rig_report(&rig, 0, 0, 0, 0, 0, 32767, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_FLOAT_EQ(rig.move_dx, DEFAULT_SENSITIVITY * MOUSE_PX_PER_TICK, 0.01f);
    TEST_PASS();
}

/* A full diagonal turns as fast as full straight-right (curve is radial). */
TEST(diagonal_speed_matches_cardinal) {
    Rig rig;
    rig_init(&rig);
    rig.config.mouse.smoothing = 0.0f;  // isolate the curve from the filter

    rig_report(&rig, 0, 0, 0, 0, 0, 28000, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    float straight = sqrtf(rig.move_dx * rig.move_dx + rig.move_dy * rig.move_dy);

    rig_report(&rig, 0, 0, 0, 0, 0, 19799, -19799);  // same distance, 45 degrees down-right
    rig_tick(&rig, MOTION_TICK_MS);
    float diagonal = sqrtf(rig.move_dx * rig.move_dx + rig.move_dy * rig.move_dy);

    ASSERT(straight > 0.0f);
    ASSERT_FLOAT_EQ(diagonal, straight, 0.01f * straight);
    ASSERT_FLOAT_EQ(rig.move_dx, rig.move_dy, 0.001f);
    TEST_PASS();
}

/* The smoothing filter settles the same amount per unit time, whatever the tick length. */
TEST(smoothing_is_time_consistent) {
    Rig one_tick, two_ticks;
    rig_init(&one_tick);
    rig_report(&one_tick, 0, 0, 0, 0, 0, 20000, 0);
    rig_tick(&one_tick, 20.0f);

    rig_init(&two_ticks);
    rig_report(&two_ticks, 0, 0, 0, 0, 0, 20000, 0);
    rig_tick(&two_ticks, 10.0f);
    rig_tick(&two_ticks, 10.0f);
    ASSERT(one_tick.state.smoothed[STICK_RIGHT].x > 0.0f);
    ASSERT_FLOAT_EQ(one_tick.state.smoothed[STICK_RIGHT].x,
                    two_ticks.state.smoothed[STICK_RIGHT].x, 0.0001f);
    TEST_PASS();
}

/* After the stick is released the smoothing tail ends: no events forever after. */
TEST(released_stick_stops_posting_motion) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, 0, 0, 32767, 0);
    for (int i = 0; i < 50; i++) rig_tick(&rig, MOTION_TICK_MS);
    ASSERT(fake.moves > 40);

    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < 100; i++) rig_tick(&rig, MOTION_TICK_MS);   // let the filter settle
    fake_clear_log();
    for (int i = 0; i < 1000; i++) rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.log_count, 0);
    TEST_PASS();
}

/* Slow motion is delivered in whole points without losing the fractions. */
TEST(slow_motion_accumulates_subpixels) {
    Rig rig;
    rig_init(&rig);
    rig.config.mouse.smoothing = 0.0f;
    rig_report(&rig, 0, 0, 0, 0, 0, 12000, 0);     // a slight tilt
    float requested = 0.0f;
    for (int i = 0; i < 400; i++) {
        rig_tick(&rig, MOTION_TICK_MS);
        requested += rig.move_dx;
    }
    ASSERT(rig.move_dx > 0.0f && rig.move_dx < 1.0f);   // under one point per tick
    int delivered = 0;
    for (int i = 0; i < fake.log_count; i++) {
        ASSERT(fake.log[i].dx != 0);                    // never an empty move event
        delivered += fake.log[i].dx;
    }
    ASSERT(fake.moves < 400);
    ASSERT_FLOAT_EQ((float)delivered, requested, 1.0f);
    ASSERT_FLOAT_EQ((float)(fake.cursor.x - 500), requested, 1.0f);
    TEST_PASS();
}

/* Default bumpers scroll: a tap is one notch, holding repeats, release stops. */
TEST(bumpers_scroll_tap_and_hold) {
    Rig rig;
    rig_init(&rig);

    rig_report(&rig, XBOX_BTN_RB, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.scroll_down, 1);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < 100; i++) rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.scroll_down, 1);           /* released: no repeats */

    /* Hold LB for 1 s in 10 ms ticks: 1 notch on press, repeats from 300 ms
       every 80 ms (300, 380, ..., 940) = 9 more */
    rig_report(&rig, XBOX_BTN_LB, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < 100; i++) rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.scroll_up, 10);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < 50; i++) rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.scroll_up, 10);
    ASSERT_EQ(fake.scroll_down, 1);

    /* The repeat rate does not depend on tick length: 1 s in 13 ms ticks */
    rig_init(&rig);
    rig_report(&rig, XBOX_BTN_LB, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < 77; i++) rig_tick(&rig, 13.0f);   // 1001 ms
    ASSERT_EQ(fake.scroll_up, 10);
    TEST_PASS();
}

/* Rumble is requested once per press: not while held, not on release. */
TEST(button_press_edges_once_per_press) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(rig.press_edges, 1);
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(rig.press_edges, 0);
    rig_report(&rig, XBOX_BTN_A | XBOX_BTN_B | XBOX_BTN_X, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(rig.press_edges, 2);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(rig.press_edges, 0);
    // Triggers and sticks are not buttons
    rig_report(&rig, 0, 1023, 1023, FULL, 0, 0, 0);
    ASSERT_EQ(rig.press_edges, 0);
    TEST_PASS();
}

/* Two sources on one key: the key stays down until both let go. */
TEST(overlapping_key_sources) {
    Rig rig;
    rig_init(&rig);
    rig.config.bindings[SOURCE_BTN_A] = KEY_W;

    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, FULL, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_W], 1);

    rig_report(&rig, 0, 0, 0, 0, FULL, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_W], 0);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    TEST_PASS();
}

TEST(button_mouse_binding_and_release_all) {
    Rig rig;
    rig_init(&rig);
    rig.config.bindings[SOURCE_BTN_A] = BINDING_MOUSE_LEFT;

    rig_report(&rig, XBOX_BTN_A, 0, 1023, 0, FULL, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    rig_report(&rig, 0, 0, 1023, 0, FULL, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 0);

    // Unplug / shutdown: every held key and button is released exactly once
    rig_release_all(&rig);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    fake_clear_log();
    rig_release_all(&rig);
    for (int i = 0; i < 20; i++) rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.log_count, 0);
    TEST_PASS();
}

/* Motion while a mouse button is held is a drag with that button. */
TEST(continuous_look_and_drag) {
    Rig rig;
    rig_init(&rig);

    rig_report(&rig, 0, 0, 1023, 0, 0, 28000, 0);
    ASSERT_EQ(fake.moves, 0);                 // motion comes from the clock, not the report
    rig_tick(&rig, MOTION_TICK_MS);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.moves, 2);
    ASSERT_EQ(fake_last(FAKE_MOVE)->drag_button, MOUSE_BUTTON_LEFT);

    rig_report(&rig, 0, 1023, 1023, 0, 0, 28000, 0);   // both: left wins
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake_last(FAKE_MOVE)->drag_button, MOUSE_BUTTON_LEFT);

    rig_report(&rig, 0, 1023, 0, 0, 0, 28000, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake_last(FAKE_MOVE)->drag_button, MOUSE_BUTTON_RIGHT);

    rig_report(&rig, 0, 0, 0, 0, 0, 28000, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake_last(FAKE_MOVE)->drag_button, MOUSE_BUTTON_NONE);
    TEST_PASS();
}

TEST(right_stick_uses_right_mapping) {
    Rig rig;
    rig_init(&rig);
    rig.config.stick[STICK_RIGHT].mode = STICK_MODE_WASD;

    rig_report(&rig, 0, 0, 0, 0, 0, 0, FULL);
    ASSERT_EQ(fake.key_down[KEY_I], 1);
    ASSERT_EQ(fake.key_down[KEY_W], 0);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_I], 1);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.moves, 0);                 // not a mouse any more
    TEST_PASS();
}

TEST(arrows_mode_and_left_stick_mouse) {
    Rig rig;
    rig_init(&rig);
    rig.config.stick[STICK_LEFT].mode = STICK_MODE_ARROWS;
    rig_report(&rig, 0, 0, 0, -FULL, 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_LEFT_ARROW], 1);
    ASSERT_EQ(fake.key_down[KEY_A], 0);

    rig_init(&rig);
    rig.config.stick[STICK_LEFT].mode = STICK_MODE_MOUSE;
    rig.config.stick[STICK_RIGHT].mode = STICK_MODE_DISABLED;
    rig_report(&rig, 0, 0, 0, FULL, 0, FULL, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.moves, 1);
    ASSERT(rig.move_dx > 0.0f);
    ASSERT_EQ(fake_count(FAKE_KEY), 0);
    TEST_PASS();
}

/* Config reload while things are held, as the driver does it: release under
   the old bindings, then apply the new bindings to what is still held. */
TEST(reload_while_held_releases_and_rebinds) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, XBOX_BTN_A, 0, 1023, 0, FULL, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_SPACE], 1);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);

    rig_release_all(&rig);
    ASSERT_EQ(fake.key_up[KEY_SPACE], 1);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);

    rig.config.bindings[SOURCE_BTN_A] = KEY_F;
    rig.config.stick[STICK_LEFT].mode = STICK_MODE_ARROWS;
    rig.config.bindings[SOURCE_RIGHT_TRIGGER] = BINDING_NONE;
    rig_report(&rig, XBOX_BTN_A, 0, 1023, 0, FULL, 0, 0);   // the same physical state
    ASSERT_EQ(fake.key_down[KEY_F], 1);
    ASSERT_EQ(fake.key_down[KEY_UP_ARROW], 1);
    ASSERT_EQ(fake.key_down[KEY_SPACE], 1);                 // not pressed again
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);

    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_F], 1);
    ASSERT_EQ(fake.key_up[KEY_UP_ARROW], 1);
    TEST_PASS();
}

/* Changing a binding in place (no release_all) still releases the old key. */
TEST(binding_change_while_held_releases_old_key) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    rig.config.bindings[SOURCE_BTN_A] = KEY_F;
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_SPACE], 1);
    ASSERT_EQ(fake.key_down[KEY_F], 1);
    ASSERT_EQ(rig.press_edges, 0);            // not a new press: no rumble
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Runtime Input (end to end)");
    RUN_TEST(packet_left_stick_up_presses_w);
    RUN_TEST(packet_right_stick_up_moves_mouse_up);
    RUN_TEST(packet_extreme_y_does_not_overflow);
    RUN_TEST(right_trigger_is_primary_click);
    RUN_TEST(trigger_holds_across_pressure_values);
    RUN_TEST(motion_scales_with_elapsed_time);
    RUN_TEST(full_tilt_speed_unchanged);
    RUN_TEST(diagonal_speed_matches_cardinal);
    RUN_TEST(smoothing_is_time_consistent);
    RUN_TEST(released_stick_stops_posting_motion);
    RUN_TEST(slow_motion_accumulates_subpixels);
    RUN_TEST(bumpers_scroll_tap_and_hold);
    RUN_TEST(button_press_edges_once_per_press);
    RUN_TEST(overlapping_key_sources);
    RUN_TEST(button_mouse_binding_and_release_all);
    RUN_TEST(continuous_look_and_drag);
    RUN_TEST(right_stick_uses_right_mapping);
    RUN_TEST(arrows_mode_and_left_stick_mouse);
    RUN_TEST(reload_while_held_releases_and_rebinds);
    RUN_TEST(binding_change_while_held_releases_old_key);
    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
