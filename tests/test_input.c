/*******************************************************************************
 * test_input.c - Input building blocks: deadzone, thresholds, report decoding
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"

static float magnitude(Vec2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

/*******************************************************************************
 * Scaled radial deadzone (K5)
 ******************************************************************************/
TEST(deadzone_zero_inside) {
    Vec2 v = stick_deadzone(0, 0, 8000);
    ASSERT_EQ(v.x, 0.0f);
    ASSERT_EQ(v.y, 0.0f);
    v = stick_deadzone(5000, 5000, 8000);        // 7071 from center
    ASSERT_EQ(v.x, 0.0f);
    ASSERT_EQ(v.y, 0.0f);
    v = stick_deadzone(5656, 5656, 8000);        // 7999 from center: still inside
    ASSERT_EQ(magnitude(v), 0.0f);
    TEST_PASS();
}

/* The old deadzone jumped from 0 to 24% at the edge. Scaled, it starts at 0. */
TEST(deadzone_is_continuous_at_edge) {
    Vec2 just_outside = stick_deadzone(8100, 0, 8000);
    ASSERT(just_outside.x > 0.0f);
    ASSERT(just_outside.x < 0.01f);

    // No step anywhere: sweep the whole axis in small raw steps
    float previous = 0.0f;
    for (int raw = 0; raw <= 32767; raw += 16) {
        float value = stick_deadzone((int16_t)raw, 0, 8000).x;
        ASSERT(value >= previous);
        ASSERT(value - previous < 0.002f);
        previous = value;
    }
    TEST_PASS();
}

TEST(deadzone_full_tilt_reaches_one) {
    ASSERT_FLOAT_EQ(stick_deadzone(32767, 0, 8000).x, 1.0f, 0.0001f);
    ASSERT_FLOAT_EQ(stick_deadzone(0, 32767, 8000).y, 1.0f, 0.0001f);
    ASSERT_FLOAT_EQ(stick_deadzone(-32767, 0, 8000).x, -1.0f, 0.0001f);
    // Midway between the edge and full tilt is half
    ASSERT_FLOAT_EQ(stick_deadzone((8000 + 32767) / 2, 0, 8000).x, 0.5f, 0.001f);
    TEST_PASS();
}

TEST(deadzone_handles_extremes) {
    // -32768 is one past -STICK_MAX; it must clamp to exactly -1, not wrap
    Vec2 v = stick_deadzone(-32768, 0, 8000);
    ASSERT_FLOAT_EQ(v.x, -1.0f, 0.0001f);
    ASSERT_EQ(v.y, 0.0f);

    // Corner of the square: magnitude clamps to 1, direction stays 45 degrees
    v = stick_deadzone(-32768, -32768, 8000);
    ASSERT_FLOAT_EQ(magnitude(v), 1.0f, 0.0001f);
    ASSERT_FLOAT_EQ(v.x, v.y, 0.0001f);
    ASSERT(v.x < 0.0f);

    // Degenerate deadzones never divide by zero or produce NaN
    v = stick_deadzone(32767, 0, 32767);
    ASSERT(v.x == v.x && v.x >= 0.0f && v.x <= 1.0f);
    v = stick_deadzone(1000, 0, 0);
    ASSERT_FLOAT_EQ(v.x, 1000.0f / 32767.0f, 0.0001f);
    v = stick_deadzone(20000, 0, -500);          // negative deadzone acts as 0
    ASSERT_FLOAT_EQ(v.x, 20000.0f / 32767.0f, 0.0001f);
    TEST_PASS();
}

TEST(deadzone_preserves_direction) {
    const int16_t points[][2] = {
        {20000, 10000}, {-12000, 25000}, {9000, -9000}, {-30000, -3000}, {3000, 31000},
    };
    for (int i = 0; i < 5; i++) {
        float x = points[i][0], y = points[i][1];
        Vec2 v = stick_deadzone(points[i][0], points[i][1], 8000);
        ASSERT(magnitude(v) > 0.0f);
        // Same angle in and out: cross product is zero, dot product positive
        ASSERT_FLOAT_EQ(v.x * y - v.y * x, 0.0f, 1.0f);
        ASSERT(v.x * x + v.y * y > 0.0f);
    }
    TEST_PASS();
}

/* A full diagonal must count as full tilt on both key axes. */
TEST(square_mapping) {
    Vec2 diagonal = stick_to_square((Vec2){0.70710678f, 0.70710678f});
    ASSERT_FLOAT_EQ(diagonal.x, 1.0f, 0.0001f);
    ASSERT_FLOAT_EQ(diagonal.y, 1.0f, 0.0001f);

    Vec2 cardinal = stick_to_square((Vec2){0.6f, 0.0f});   // unchanged on an axis
    ASSERT_FLOAT_EQ(cardinal.x, 0.6f, 0.0001f);
    ASSERT_EQ(cardinal.y, 0.0f);

    Vec2 mixed = stick_to_square((Vec2){-0.3f, 0.4f});     // major axis = magnitude
    ASSERT_FLOAT_EQ(mixed.y, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(mixed.x, -0.375f, 0.0001f);

    Vec2 zero = stick_to_square((Vec2){0.0f, 0.0f});
    ASSERT_EQ(zero.x, 0.0f);
    ASSERT_EQ(zero.y, 0.0f);
    TEST_PASS();
}

/*******************************************************************************
 * Report decoding (K15)
 ******************************************************************************/
TEST(parse_reads_little_endian_fields) {
    const uint8_t bytes[GIP_INPUT_LENGTH] = {
        0x20, 0x00, 0x07, 0x0E,     // header: input, seq 7, 14 payload bytes
        0x10, 0x20,                 // buttons 0x2010 = A | RB
        0xFF, 0x03,                 // left trigger 1023
        0x01, 0x00,                 // right trigger 1
        0x00, 0x80,                 // left X -32768
        0xFF, 0x7F,                 // left Y 32767
        0x34, 0x12,                 // right X 0x1234
        0xFE, 0xFF,                 // right Y -2
    };
    GipInput input;
    ASSERT_TRUE(gip_parse_input(bytes, sizeof(bytes), &input));
    ASSERT_EQ(input.buttons, XBOX_BTN_A | XBOX_BTN_RB);
    ASSERT_EQ(input.left_trigger, 1023);
    ASSERT_EQ(input.right_trigger, 1);
    ASSERT_EQ(input.left_stick_x, -32768);
    ASSERT_EQ(input.left_stick_y, 32767);
    ASSERT_EQ(input.right_stick_x, 0x1234);
    ASSERT_EQ(input.right_stick_y, -2);
    TEST_PASS();
}

TEST(parse_rejects_short_and_foreign_packets) {
    uint8_t bytes[GIP_INPUT_LENGTH];
    make_report(bytes, 0, 0, 0, 0, 0, 0, 0);
    GipInput input;
    ASSERT_TRUE(gip_parse_input(bytes, GIP_INPUT_LENGTH, &input));
    ASSERT_FALSE(gip_parse_input(bytes, GIP_INPUT_LENGTH - 1, &input));
    ASSERT_FALSE(gip_parse_input(bytes, 0, &input));
    bytes[0] = GIP_CMD_GUIDE_BUTTON;
    ASSERT_FALSE(gip_parse_input(bytes, GIP_INPUT_LENGTH, &input));
    TEST_PASS();
}

/*******************************************************************************
 * Trigger hysteresis (K3)
 ******************************************************************************/
TEST(trigger_press_and_release_points) {
    Rig rig;
    rig_init(&rig);
    // Defaults: band centered on 127/255 (50%), 26/255 wide: press above ~55%,
    // release at ~45% of the 10-bit range.
    rig_report(&rig, 0, 0, 550, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 0);
    rig_report(&rig, 0, 0, 570, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    rig_report(&rig, 0, 0, 470, 0, 0, 0, 0);      // inside the band: stays down
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 0);
    rig_report(&rig, 0, 0, 450, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    rig_report(&rig, 0, 0, 550, 0, 0, 0, 0);      // inside the band: stays up
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    TEST_PASS();
}

/* A finger resting on the threshold: +/-2% wobble is one press, no chatter. */
TEST(trigger_no_chatter_at_threshold) {
    Rig rig;
    rig_init(&rig);
    int center = 127 * 1023 / 255;
    int wobble = 1023 * 2 / 100;

    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);     // squeeze past the band once
    for (int i = 0; i < 200; i++) {
        rig_report(&rig, 0, 0, center + (i % 2 ? wobble : -wobble), 0, 0, 0, 0);
    }
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 0);

    // Same wobble approached from below never presses at all
    rig_init(&rig);
    for (int i = 0; i < 200; i++) {
        rig_report(&rig, 0, 0, center + (i % 2 ? wobble : -wobble), 0, 0, 0, 0);
    }
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 0);

    // Without hysteresis the same input chatters, which is what the band prevents
    rig_init(&rig);
    rig.config.triggers.hysteresis = 0;
    for (int i = 0; i < 200; i++) {
        rig_report(&rig, 0, 0, center + (i % 2 ? wobble : -wobble), 0, 0, 0, 0);
    }
    ASSERT(fake.mouse_down[MOUSE_BUTTON_LEFT] > 50);
    TEST_PASS();
}

TEST(trigger_extreme_thresholds_still_work) {
    Rig rig;
    rig_init(&rig);
    rig.config.triggers.threshold = 255;          // band pushed off the top
    rig.config.triggers.hysteresis = 255;
    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);     // a full squeeze still presses
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);

    rig_init(&rig);
    rig.config.triggers.threshold = 0;            // band pushed off the bottom
    rig.config.triggers.hysteresis = 0;
    rig_report(&rig, 0, 0, 1, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);        // fully released always releases
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);

    rig_init(&rig);
    rig_report(&rig, 0, 0, 0xFFFF, 0, 0, 0, 0);   // out-of-range raw value: treated as full
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    TEST_PASS();
}

/* K10: a trigger is disabled by having no binding; no separate mode. */
TEST(trigger_disabled_by_binding) {
    Rig rig;
    rig_init(&rig);
    rig.config.bindings[SOURCE_RIGHT_TRIGGER] = BINDING_NONE;
    rig.config.bindings[SOURCE_LEFT_TRIGGER] = KEY_SPACE;
    rig_report(&rig, 0, 1023, 1023, 0, 0, 0, 0);
    ASSERT_EQ(fake.log_count, 1);                 // only the key
    ASSERT_EQ(fake.key_down[KEY_SPACE], 1);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_SPACE], 1);
    ASSERT_EQ(fake.log_count, 2);
    TEST_PASS();
}

/*******************************************************************************
 * Stick key threshold and hysteresis (K3, K4)
 ******************************************************************************/

/* Raw stick value that reads as `value` (0-1) after the default deadzone. */
static int raw_for(float value) {
    return (int)lroundf(DEFAULT_DEADZONE + value * (STICK_MAX - DEFAULT_DEADZONE));
}

TEST(stick_key_threshold_is_configurable) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, raw_for(0.28f), 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_D], 0);
    rig_report(&rig, 0, 0, 0, raw_for(0.32f), 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_D], 1);

    rig_init(&rig);
    rig.config.stick[STICK_LEFT].threshold = 0.60f;
    rig_report(&rig, 0, 0, 0, raw_for(0.55f), 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_D], 0);
    rig_report(&rig, 0, 0, 0, raw_for(0.65f), 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_D], 1);

    // The right stick has its own threshold
    rig_init(&rig);
    rig.config.stick[STICK_RIGHT].mode = STICK_MODE_WASD;
    rig.config.stick[STICK_RIGHT].threshold = 0.10f;
    rig_report(&rig, 0, 0, 0, raw_for(0.15f), 0, raw_for(0.15f), 0);
    ASSERT_EQ(fake.key_down[KEY_D], 0);           // left still needs 0.30
    ASSERT_EQ(fake.key_down[KEY_L], 1);
    TEST_PASS();
}

TEST(stick_key_no_chatter_at_threshold) {
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0, 0, 0, raw_for(0.40f), 0, 0, 0);
    for (int i = 0; i < 200; i++) {
        rig_report(&rig, 0, 0, 0, raw_for(i % 2 ? 0.32f : 0.28f), 0, 0, 0);
    }
    ASSERT_EQ(fake.key_down[KEY_D], 1);
    ASSERT_EQ(fake.key_up[KEY_D], 0);

    // Released only below threshold - hysteresis (0.25)
    rig_report(&rig, 0, 0, 0, raw_for(0.26f), 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_D], 0);
    rig_report(&rig, 0, 0, 0, raw_for(0.24f), 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_D], 1);

    // Without hysteresis the same wobble chatters
    rig_init(&rig);
    rig.config.stick[STICK_LEFT].hysteresis = 0.0f;
    for (int i = 0; i < 200; i++) {
        rig_report(&rig, 0, 0, 0, raw_for(i % 2 ? 0.32f : 0.28f), 0, 0, 0);
    }
    ASSERT(fake.key_down[KEY_D] > 50);
    TEST_PASS();
}

/* Hysteresis wider than the threshold must still release at center. */
TEST(stick_key_releases_at_center_with_wide_hysteresis) {
    Rig rig;
    rig_init(&rig);
    rig.config.stick[STICK_LEFT].threshold = 0.10f;
    rig.config.stick[STICK_LEFT].hysteresis = 0.50f;
    rig_report(&rig, 0, 0, 0, 30000, 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_D], 1);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_D], 1);
    TEST_PASS();
}

TEST(stick_full_diagonal_presses_both_keys) {
    Rig rig;
    rig_init(&rig);
    rig.config.stick[STICK_LEFT].threshold = 0.90f;   // only reachable via square mapping
    rig_report(&rig, 0, 0, 0, 23170, 23170, 0, 0);    // full tilt at 45 degrees (up-right)
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.key_down[KEY_D], 1);
    ASSERT_EQ(fake.key_down[KEY_S], 0);
    ASSERT_EQ(fake.key_down[KEY_A], 0);
    TEST_PASS();
}

/*******************************************************************************
 * State
 ******************************************************************************/
TEST(input_state_init_clears_everything) {
    InputState state;
    memset(&state, 0xFF, sizeof(state));
    input_state_init(&state);
    InputState zero;
    memset(&zero, 0, sizeof(zero));
    ASSERT_EQ(memcmp(&state, &zero, sizeof(state)), 0);
    TEST_PASS();
}

/* The static bound on the queue holds in the worst case: every source held,
   then every binding swapped, then everything released. */
TEST(output_queue_never_overflows) {
    Rig rig;
    rig_init(&rig);
    rig.config.stick[STICK_RIGHT].mode = STICK_MODE_WASD;
    for (int i = 0; i < SOURCE_COUNT; i++) rig.config.bindings[i] = (uint16_t)(0x40 + i);
    uint16_t all_buttons = 0xFFFC;

    rig_report(&rig, all_buttons, 1023, 1023, 23170, 23170, 23170, 23170);
    ASSERT_EQ(rig.last_queue.count, SOURCE_BUTTON_COUNT + 2 + 4);
    for (int i = 0; i < SOURCE_COUNT; i++) rig.config.bindings[i] = (uint16_t)(0x80 + i);
    rig_report(&rig, all_buttons, 1023, 1023, -23170, -23170, -23170, -23170);
    ASSERT(rig.last_queue.count <= 2 * SOURCE_COUNT);
    rig_release_all(&rig);
    ASSERT_EQ(rig.dropped, 0);

    // Every down has its up
    int down = 0, up = 0;
    for (int key = 0; key < KEY_COUNT; key++) {
        down += fake.key_down[key];
        up += fake.key_up[key];
    }
    ASSERT(down > 0);
    ASSERT_EQ(down, up);
    TEST_PASS();
}

TEST(button_masks_cover_distinct_bits) {
    // One report with each button alone presses exactly that button's binding
    const uint16_t masks[SOURCE_BUTTON_COUNT] = {
        XBOX_BTN_A, XBOX_BTN_B, XBOX_BTN_X, XBOX_BTN_Y, XBOX_BTN_LB, XBOX_BTN_RB,
        XBOX_BTN_LS, XBOX_BTN_RS, XBOX_BTN_VIEW, XBOX_BTN_MENU,
        XBOX_BTN_DPAD_UP, XBOX_BTN_DPAD_DOWN, XBOX_BTN_DPAD_LEFT, XBOX_BTN_DPAD_RIGHT,
    };
    for (int i = 0; i < SOURCE_BUTTON_COUNT; i++) {
        Rig rig;
        rig_init(&rig);
        for (int s = 0; s < SOURCE_BUTTON_COUNT; s++) rig.config.bindings[s] = (uint16_t)(0x50 + s);
        rig_report(&rig, masks[i], 0, 0, 0, 0, 0, 0);
        ASSERT_EQ(fake.log_count, 1);
        ASSERT_EQ(fake.key_down[0x50 + i], 1);
    }
    // Bits 0 and 1 (sync, unused) map to nothing
    Rig rig;
    rig_init(&rig);
    rig_report(&rig, 0x0003, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.log_count, 0);
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Input Building Blocks");

    printf("\n-- Deadzone --\n");
    RUN_TEST(deadzone_zero_inside);
    RUN_TEST(deadzone_is_continuous_at_edge);
    RUN_TEST(deadzone_full_tilt_reaches_one);
    RUN_TEST(deadzone_handles_extremes);
    RUN_TEST(deadzone_preserves_direction);
    RUN_TEST(square_mapping);

    printf("\n-- Report decoding --\n");
    RUN_TEST(parse_reads_little_endian_fields);
    RUN_TEST(parse_rejects_short_and_foreign_packets);

    printf("\n-- Triggers --\n");
    RUN_TEST(trigger_press_and_release_points);
    RUN_TEST(trigger_no_chatter_at_threshold);
    RUN_TEST(trigger_extreme_thresholds_still_work);
    RUN_TEST(trigger_disabled_by_binding);

    printf("\n-- Stick keys --\n");
    RUN_TEST(stick_key_threshold_is_configurable);
    RUN_TEST(stick_key_no_chatter_at_threshold);
    RUN_TEST(stick_key_releases_at_center_with_wide_hysteresis);
    RUN_TEST(stick_full_diagonal_presses_both_keys);

    printf("\n-- State --\n");
    RUN_TEST(input_state_init_clears_everything);
    RUN_TEST(output_queue_never_overflows);
    RUN_TEST(button_masks_cover_distinct_bits);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
