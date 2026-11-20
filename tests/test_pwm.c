/*******************************************************************************
 * test_pwm.c - Left-stick PWM (K2): a partly pushed stick partly holds its key
 *
 * Runs the real code on a fake clock for many seconds of 10 ms ticks, and of
 * ticks jittered between 10 and 14 ms (what the motion clock really delivers).
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"

/* Raw stick value that reads as `value` (0-1) after the default deadzone. */
static int raw_for(float value) {
    return (int)lroundf(DEFAULT_DEADZONE + value * (STICK_MAX - DEFAULT_DEADZONE));
}

static float expected_duty(float tilt) {
    float duty = (tilt - DEFAULT_STICK_THRESHOLD) / (DEFAULT_PWM_FULL_ON - DEFAULT_STICK_THRESHOLD);
    return duty < 0.0f ? 0.0f : (duty > 1.0f ? 1.0f : duty);
}

static void pwm_rig(Rig *rig) {
    rig_init(rig);
    rig->config.stick[STICK_LEFT].pwm = true;     // "mode": "wasd_pwm"
}

typedef enum { TICKS_STEADY, TICKS_JITTERED } TickPattern;

/* Runs motion ticks for `duration_ms`: exactly 10 ms apart, or 10-14 ms. */
static void run_ticks(Rig *rig, float duration_ms, TickPattern pattern, uint32_t seed) {
    float elapsed = 0.0f;
    while (elapsed < duration_ms) {
        float dt = pattern == TICKS_STEADY
                   ? MOTION_TICK_MS
                   : MOTION_TICK_MS + (float)(test_random(&seed) % 4001) / 1000.0f;
        rig_tick(rig, dt);
        elapsed += dt;
    }
}

/* What one key did, reconstructed from the posted events and their times. */
typedef struct {
    int downs, ups;
    double down_ms;          // Total time held
    double shortest_down_ms; // Shortest completed pulse
    double shortest_up_ms;   // Shortest gap between two pulses
} KeyTrace;

static KeyTrace trace_key(int key, uint64_t start_us, uint64_t end_us) {
    KeyTrace trace = {.shortest_down_ms = 1e9, .shortest_up_ms = 1e9};
    bool down = false;
    uint64_t changed_us = start_us;
    for (int i = 0; i < fake.log_count; i++) {
        const FakeEvent *event = &fake.log[i];
        if (event->type != FAKE_KEY || event->code != key) continue;
        double interval_ms = (double)(event->time_us - changed_us) / 1000.0;
        if (event->pressed) {
            if (trace.downs > 0 && interval_ms < trace.shortest_up_ms) {
                trace.shortest_up_ms = interval_ms;
            }
            trace.downs++;
        } else {
            trace.down_ms += interval_ms;
            if (interval_ms < trace.shortest_down_ms) trace.shortest_down_ms = interval_ms;
            trace.ups++;
        }
        down = event->pressed;
        changed_us = event->time_us;
    }
    if (down) trace.down_ms += (double)(end_us - changed_us) / 1000.0;
    return trace;
}

/* Holds the left stick forward at `tilt` for `seconds` and measures W. */
static KeyTrace measure_forward(Rig *rig, float tilt, float seconds, TickPattern pattern,
                                double *duty) {
    uint64_t start = fake.now_us;
    rig_report(rig, 0, 0, 0, 0, raw_for(tilt), 0, 0);
    run_ticks(rig, seconds * 1000.0f, pattern, 12345);
    KeyTrace trace = trace_key(KEY_W, start, fake.now_us);
    *duty = trace.down_ms / ((double)(fake.now_us - start) / 1000.0);
    return trace;
}

/*******************************************************************************
 * Duty accuracy
 ******************************************************************************/
static bool duty_within_5_percent(float tilt, TickPattern pattern) {
    Rig rig;
    pwm_rig(&rig);
    double duty;
    measure_forward(&rig, tilt, 30.0f, pattern, &duty);
    double expected = expected_duty(tilt);
    if (fabs(duty - expected) > 0.05 * expected) {
        printf("\n         tilt %.2f: duty %.4f, expected %.4f ", tilt, duty, expected);
        return false;
    }
    return rig.dropped == 0;
}

TEST(duty_tracks_tilt_steady_ticks) {
    ASSERT_TRUE(duty_within_5_percent(0.35f, TICKS_STEADY));
    ASSERT_TRUE(duty_within_5_percent(0.50f, TICKS_STEADY));
    ASSERT_TRUE(duty_within_5_percent(0.70f, TICKS_STEADY));
    TEST_PASS();
}

/* Ticks land 10-14 ms apart in the real driver. Duty comes from accumulated
   time, not tick counts, so it must stay accurate. */
TEST(duty_tracks_tilt_jittered_ticks) {
    ASSERT_TRUE(duty_within_5_percent(0.35f, TICKS_JITTERED));
    ASSERT_TRUE(duty_within_5_percent(0.50f, TICKS_JITTERED));
    ASSERT_TRUE(duty_within_5_percent(0.70f, TICKS_JITTERED));
    // Awkward duties that do not divide the period into whole ticks
    ASSERT_TRUE(duty_within_5_percent(0.43f, TICKS_JITTERED));
    ASSERT_TRUE(duty_within_5_percent(0.61f, TICKS_STEADY));
    ASSERT_TRUE(duty_within_5_percent(0.83f, TICKS_JITTERED));
    TEST_PASS();
}

/* More tilt is never less walking. */
TEST(duty_is_monotonic_in_tilt) {
    double previous = -1.0;
    for (int step = 0; step <= 20; step++) {
        Rig rig;
        pwm_rig(&rig);
        double duty;
        measure_forward(&rig, 0.20f + 0.04f * (float)step, 10.0f, TICKS_JITTERED, &duty);
        ASSERT(duty >= previous - 0.01);
        previous = duty;
    }
    ASSERT(previous > 0.99);                 // all but the wait for the first tick
    TEST_PASS();
}

/*******************************************************************************
 * The ends of the range
 ******************************************************************************/
TEST(below_threshold_no_events) {
    Rig rig;
    pwm_rig(&rig);
    double duty;
    measure_forward(&rig, 0.29f, 10.0f, TICKS_JITTERED, &duty);
    ASSERT_EQ(fake.log_count, 0);
    // Inside the deadzone too
    rig_report(&rig, 0, 0, 0, 0, 5000, 0, 0);
    run_ticks(&rig, 2000.0f, TICKS_STEADY, 1);
    ASSERT_EQ(fake.log_count, 0);
    TEST_PASS();
}

TEST(full_tilt_held_continuously) {
    Rig rig;
    pwm_rig(&rig);
    double duty;
    KeyTrace trace = measure_forward(&rig, 1.0f, 10.0f, TICKS_JITTERED, &duty);
    ASSERT_EQ(trace.downs, 1);
    ASSERT_EQ(trace.ups, 0);                 // no release events at all
    ASSERT_EQ(fake.log_count, 1);

    // At exactly pwm_full_on and just above it as well
    pwm_rig(&rig);
    trace = measure_forward(&rig, 0.905f, 10.0f, TICKS_STEADY, &duty);
    ASSERT_EQ(trace.downs, 1);
    ASSERT_EQ(trace.ups, 0);

    // The first press does not wait for a period boundary
    pwm_rig(&rig);
    rig_report(&rig, 0, 0, 0, 0, raw_for(1.0f), 0, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    TEST_PASS();
}

/*******************************************************************************
 * Minimum pulse
 ******************************************************************************/
TEST(no_pulse_shorter_than_minimum) {
    for (int pattern = TICKS_STEADY; pattern <= TICKS_JITTERED; pattern++) {
        // Sweep tilts from just above the threshold to just below full-on:
        // both very short on-times and very short off-times get merged.
        for (int step = 0; step <= 60; step++) {
            Rig rig;
            pwm_rig(&rig);
            double duty;
            float tilt = 0.30f + 0.01f * (float)step;
            KeyTrace trace = measure_forward(&rig, tilt, 6.0f, (TickPattern)pattern, &duty);
            if (trace.ups > 0) ASSERT(trace.shortest_down_ms >= DEFAULT_PWM_MIN_PULSE_MS - 0.001);
            if (trace.downs > 1) ASSERT(trace.shortest_up_ms >= DEFAULT_PWM_MIN_PULSE_MS - 0.001);
        }
    }
    TEST_PASS();
}

/* A duty too small for one minimum pulse per period is not dropped: it pulses
   less often, and the average still matches. */
TEST(tiny_duty_skips_periods_instead_of_shrinking_pulses) {
    Rig rig;
    pwm_rig(&rig);
    double duty;
    KeyTrace trace = measure_forward(&rig, 0.33f, 60.0f, TICKS_STEADY, &duty);   // 5%
    ASSERT(trace.downs > 50);
    ASSERT(trace.downs < 200);               // far fewer than the 600 periods
    ASSERT(trace.shortest_down_ms >= DEFAULT_PWM_MIN_PULSE_MS - 0.001);
    ASSERT(fabs(duty - expected_duty(0.33f)) < 0.005);
    TEST_PASS();
}

TEST(configured_period_and_min_pulse_are_used) {
    Rig rig;
    pwm_rig(&rig);
    rig.config.stick[STICK_LEFT].pwm_period_ms = 200.0f;
    rig.config.stick[STICK_LEFT].pwm_min_pulse_ms = 50.0f;
    double duty;
    KeyTrace trace = measure_forward(&rig, 0.60f, 20.0f, TICKS_STEADY, &duty);   // 50%
    ASSERT(trace.downs >= 99 && trace.downs <= 101);     // one pulse per 200 ms
    ASSERT(trace.shortest_down_ms >= 50.0 - 0.001);
    ASSERT(trace.shortest_up_ms >= 50.0 - 0.001);
    ASSERT(fabs(duty - 0.5) < 0.025);
    TEST_PASS();
}

/*******************************************************************************
 * Hysteresis
 ******************************************************************************/
TEST(no_chatter_at_threshold) {
    Rig rig;
    pwm_rig(&rig);
    uint64_t start = fake.now_us;
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.40f), 0, 0);
    // The stick wobbles +/-2% around the threshold, a new report every tick
    for (int i = 0; i < 1000; i++) {
        rig_report(&rig, 0, 0, 0, 0, raw_for(i % 2 ? 0.32f : 0.28f), 0, 0);
        ASSERT_TRUE(rig.state.dir_active[STICK_LEFT][STICK_DIR_UP]);
        rig_tick(&rig, MOTION_TICK_MS);
    }
    // The key follows the (tiny) average duty: a few whole pulses, never a
    // press/release pair per wobble.
    KeyTrace trace = trace_key(KEY_W, start, fake.now_us);
    ASSERT(trace.downs < 20);
    if (trace.ups > 0) ASSERT(trace.shortest_down_ms >= DEFAULT_PWM_MIN_PULSE_MS - 0.001);
    TEST_PASS();
}

/*******************************************************************************
 * Diagonals
 ******************************************************************************/
TEST(diagonal_keys_pulse_at_their_own_duty) {
    Rig rig;
    pwm_rig(&rig);
    // Mostly forward, a little right. After the deadzone and the circle-to-
    // square mapping the larger axis reads as the stick's magnitude.
    int raw_x = 14741, raw_y = 23586;        // reads as about (0.50, 0.80)
    float raw_magnitude = sqrtf((float)raw_x * raw_x + (float)raw_y * raw_y);
    float magnitude = (raw_magnitude - DEFAULT_DEADZONE) / (STICK_MAX - DEFAULT_DEADZONE);
    float forward = magnitude, right = magnitude * (float)raw_x / (float)raw_y;

    uint64_t start = fake.now_us;
    rig_report(&rig, 0, 0, 0, raw_x, raw_y, 0, 0);
    run_ticks(&rig, 30000.0f, TICKS_JITTERED, 99);
    double total_ms = (double)(fake.now_us - start) / 1000.0;
    KeyTrace w = trace_key(KEY_W, start, fake.now_us);
    KeyTrace d = trace_key(KEY_D, start, fake.now_us);

    ASSERT(expected_duty(forward) > expected_duty(right) + 0.2f);   // genuinely different
    ASSERT(fabs(w.down_ms / total_ms - expected_duty(forward)) < 0.05 * expected_duty(forward));
    ASSERT(fabs(d.down_ms / total_ms - expected_duty(right)) < 0.05 * expected_duty(right));
    ASSERT_EQ(fake.key_down[KEY_S] + fake.key_down[KEY_A], 0);

    // Same phase: whenever D goes down, W is down too -- already, on the same
    // tick, or one tick later (the minimum-pulse rule can hold W back a tick
    // when its previous gap was cut short). The keys never alternate.
    const uint64_t one_tick_us = 14000;
    int d_presses = 0;
    for (int i = 0; i < fake.log_count; i++) {
        const FakeEvent *event = &fake.log[i];
        if (event->type != FAKE_KEY || event->code != KEY_D || !event->pressed) continue;
        d_presses++;
        bool w_down = false;
        for (int j = 0; j < fake.log_count &&
                        fake.log[j].time_us <= event->time_us + one_tick_us; j++) {
            if (fake.log[j].type == FAKE_KEY && fake.log[j].code == KEY_W) {
                w_down = fake.log[j].pressed;
            }
        }
        ASSERT_TRUE(w_down);
    }
    ASSERT(d_presses > 50);
    TEST_PASS();
}

TEST(full_diagonal_holds_both_keys) {
    Rig rig;
    pwm_rig(&rig);
    rig_report(&rig, 0, 0, 0, 23170, 23170, 0, 0);       // full tilt at 45 degrees
    run_ticks(&rig, 5000.0f, TICKS_JITTERED, 5);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.key_down[KEY_D], 1);
    ASSERT_EQ(fake.key_up[KEY_W] + fake.key_up[KEY_D], 0);
    TEST_PASS();
}

/*******************************************************************************
 * Shared keys
 ******************************************************************************/

/* A held + stick PWM on the same key: the key must never be released while A
   is down (reference counting), and pulses resume when A lets go. */
TEST(shared_key_with_held_button_never_releases) {
    Rig rig;
    pwm_rig(&rig);
    rig.config.bindings[SOURCE_BTN_A] = KEY_W;

    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, raw_for(0.50f), 0, 0);
    run_ticks(&rig, 5000.0f, TICKS_JITTERED, 3);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.key_up[KEY_W], 0);

    // The stick kept pulsing its own source the whole time
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.50f), 0, 0);
    run_ticks(&rig, 5000.0f, TICKS_JITTERED, 4);
    ASSERT(fake.key_up[KEY_W] > 20);

    // And the other way round: stick released while A holds -> still down
    pwm_rig(&rig);
    rig.config.bindings[SOURCE_BTN_A] = KEY_W;
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, raw_for(1.0f), 0, 0);
    run_ticks(&rig, 200.0f, TICKS_STEADY, 1);
    rig_report(&rig, XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    run_ticks(&rig, 200.0f, TICKS_STEADY, 1);
    ASSERT_EQ(fake.key_up[KEY_W], 0);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    TEST_PASS();
}

/*******************************************************************************
 * Changes in the middle of a period
 ******************************************************************************/
TEST(center_releases_immediately) {
    Rig rig;
    pwm_rig(&rig);
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.80f), 0, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    ASSERT_EQ(fake.key_up[KEY_W], 0);

    // Back to center 20 ms into the pulse: released by the report itself,
    // without waiting for a tick or the end of the pulse
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    fake_clear_log();
    run_ticks(&rig, 1000.0f, TICKS_STEADY, 1);
    ASSERT_EQ(fake.log_count, 0);

    // Pushing again presses on the very next tick (a fresh period)
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.80f), 0, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    ASSERT_EQ(fake.key_down[KEY_W], 1);
    TEST_PASS();
}

TEST(tilt_change_applies_by_next_period) {
    Rig rig;
    pwm_rig(&rig);
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.40f), 0, 0);
    run_ticks(&rig, 1030.0f, TICKS_STEADY, 1);          // partway through a period

    // Slam to full tilt: held within one period, and stays held
    rig_report(&rig, 0, 0, 0, 0, raw_for(1.0f), 0, 0);
    run_ticks(&rig, DEFAULT_PWM_PERIOD_MS, TICKS_STEADY, 1);
    ASSERT_TRUE(rig.state.source_pressed[SOURCE_LEFT_STICK + STICK_DIR_UP]);
    fake_clear_log();
    run_ticks(&rig, 2000.0f, TICKS_STEADY, 1);
    ASSERT_EQ(fake.log_count, 0);

    // Ease back to half tilt: pulsing again within one period
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.60f), 0, 0);
    run_ticks(&rig, DEFAULT_PWM_PERIOD_MS + MOTION_TICK_MS, TICKS_STEADY, 1);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    TEST_PASS();
}

/* Reload, unplug and mode changes release everything (input_state_release_all). */
TEST(release_all_while_pulsing) {
    Rig rig;
    pwm_rig(&rig);
    rig_report(&rig, 0, 0, 0, raw_for(0.60f), raw_for(1.0f), 0, 0);
    run_ticks(&rig, 520.0f, TICKS_STEADY, 1);
    int w_down = fake.key_down[KEY_W], d_down = fake.key_down[KEY_D];

    rig_release_all(&rig);
    ASSERT_EQ(fake.key_up[KEY_W], w_down);
    ASSERT_EQ(fake.key_up[KEY_D], d_down);
    fake_clear_log();
    run_ticks(&rig, 1000.0f, TICKS_STEADY, 1);          // no report since: nothing happens
    ASSERT_EQ(fake.log_count, 0);

    // Switching the stick to mouse mode mid-pulse releases its keys
    pwm_rig(&rig);
    rig_report(&rig, 0, 0, 0, 0, raw_for(1.0f), 0, 0);
    rig_tick(&rig, MOTION_TICK_MS);
    rig.config.stick[STICK_LEFT].mode = STICK_MODE_MOUSE;
    rig_report(&rig, 0, 0, 0, 0, raw_for(1.0f), 0, 0);
    ASSERT_EQ(fake.key_up[KEY_W], 1);
    TEST_PASS();
}

/*******************************************************************************
 * Other configurations
 ******************************************************************************/
TEST(plain_wasd_is_not_pulsed) {
    Rig rig;
    rig_init(&rig);                                      // "mode": "wasd"
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.50f), 0, 0);
    ASSERT_EQ(fake.key_down[KEY_W], 1);                  // pressed by the report itself
    run_ticks(&rig, 5000.0f, TICKS_JITTERED, 8);
    ASSERT_EQ(fake.log_count, 1);
    TEST_PASS();
}

TEST(right_stick_and_arrows_can_pulse_too) {
    Rig rig;
    rig_init(&rig);
    rig.config.stick[STICK_RIGHT].mode = STICK_MODE_ARROWS;
    rig.config.stick[STICK_RIGHT].pwm = true;
    uint64_t start = fake.now_us;
    rig_report(&rig, 0, 0, 0, 0, 0, raw_for(0.60f), 0);
    run_ticks(&rig, 20000.0f, TICKS_JITTERED, 21);
    KeyTrace trace = trace_key(KEY_RIGHT_ARROW, start, fake.now_us);
    double duty = trace.down_ms / ((double)(fake.now_us - start) / 1000.0);
    ASSERT(fabs(duty - 0.5) < 0.025);
    ASSERT_EQ(fake.moves, 0);
    TEST_PASS();
}

/* A stalled tick (clamped to MOTION_MAX_DT_MS) must not break the state. */
TEST(survives_long_and_odd_ticks) {
    Rig rig;
    pwm_rig(&rig);
    rig.config.stick[STICK_LEFT].pwm_period_ms = MIN_PWM_PERIOD_MS;   // shorter than a stall
    rig.config.stick[STICK_LEFT].pwm_min_pulse_ms = MIN_PWM_MIN_PULSE_MS;
    rig_report(&rig, 0, 0, 0, 0, raw_for(0.60f), 0, 0);
    uint32_t seed = 77;
    for (int i = 0; i < 5000; i++) {
        float dt = (float)(test_random(&seed) % 120000) / 1000.0f;    // 0-120 ms
        rig_tick(&rig, dt);
        const PwmStick *pwm = &rig.state.pwm[STICK_LEFT];
        ASSERT(pwm->phase_ms >= 0.0f && pwm->phase_ms < MIN_PWM_PERIOD_MS);
        ASSERT(fabsf(pwm->dir[STICK_DIR_UP].debt_ms) <= MIN_PWM_PERIOD_MS);
    }
    ASSERT_EQ(rig.dropped, 0);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.key_down[KEY_W], fake.key_up[KEY_W]);
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Stick PWM");

    printf("\n-- Duty --\n");
    RUN_TEST(duty_tracks_tilt_steady_ticks);
    RUN_TEST(duty_tracks_tilt_jittered_ticks);
    RUN_TEST(duty_is_monotonic_in_tilt);
    RUN_TEST(below_threshold_no_events);
    RUN_TEST(full_tilt_held_continuously);

    printf("\n-- Pulse shape --\n");
    RUN_TEST(no_pulse_shorter_than_minimum);
    RUN_TEST(tiny_duty_skips_periods_instead_of_shrinking_pulses);
    RUN_TEST(configured_period_and_min_pulse_are_used);
    RUN_TEST(no_chatter_at_threshold);

    printf("\n-- Diagonals and shared keys --\n");
    RUN_TEST(diagonal_keys_pulse_at_their_own_duty);
    RUN_TEST(full_diagonal_holds_both_keys);
    RUN_TEST(shared_key_with_held_button_never_releases);

    printf("\n-- Mid-period changes --\n");
    RUN_TEST(center_releases_immediately);
    RUN_TEST(tilt_change_applies_by_next_period);
    RUN_TEST(release_all_while_pulsing);

    printf("\n-- Other configurations --\n");
    RUN_TEST(plain_wasd_is_not_pulsed);
    RUN_TEST(right_stick_and_arrows_can_pulse_too);
    RUN_TEST(survives_long_and_odd_ticks);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
