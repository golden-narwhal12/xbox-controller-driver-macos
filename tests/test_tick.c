/*******************************************************************************
 * test_tick.c - Motion clock scheduling (K11) on a fake clock
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"
#include "../include/tick.h"

#define PERIOD 10000u   // 10 ms in microseconds

/* Runs the motion thread's loop body once on the fake clock: sleep to the
   deadline, wake `late_us` after it, and step. Returns the elapsed time. */
static uint64_t wake_and_step(TickSched *sched, uint64_t late_us) {
    platform_sleep_until_us(tick_sched_deadline(sched));
    fake.now_us += late_us;
    return tick_sched_step(sched, platform_now_us());
}

TEST(steady_period) {
    fake_reset();
    TickSched sched;
    tick_sched_init(&sched, platform_now_us(), PERIOD);
    uint64_t start = platform_now_us();
    for (int i = 1; i <= 1000; i++) {
        ASSERT_EQ(wake_and_step(&sched, 0), PERIOD);
        ASSERT_EQ(platform_now_us(), start + (uint64_t)i * PERIOD);
    }
    TEST_PASS();
}

/* Late wakeups do not accumulate: deadlines stay on the original grid, so
   1000 ticks that each wake 0-3 ms late still take 10 s, not 11.5 s. */
TEST(jitter_does_not_drift) {
    fake_reset();
    TickSched sched;
    tick_sched_init(&sched, platform_now_us(), PERIOD);
    uint64_t start = platform_now_us();
    uint32_t seed = 7;
    uint64_t total = 0, late = 0;
    for (int i = 1; i <= 1000; i++) {
        late = test_random(&seed) % 3000;
        uint64_t elapsed = wake_and_step(&sched, late);
        ASSERT(elapsed > PERIOD - 3000 && elapsed < PERIOD + 3000);
        total += elapsed;
    }
    ASSERT_EQ(platform_now_us(), start + 1000u * PERIOD + late);
    ASSERT_EQ(total, 1000u * PERIOD + late);     // every microsecond is reported once
    TEST_PASS();
}

/* After a stall the missed ticks are not replayed in a burst: one tick reports
   the whole gap, and the next deadline is in the future on the same grid. */
TEST(overrun_recovers_without_bursting) {
    fake_reset();
    TickSched sched;
    tick_sched_init(&sched, platform_now_us(), PERIOD);
    uint64_t start = platform_now_us();
    ASSERT_EQ(wake_and_step(&sched, 0), PERIOD);

    // Stall: wake 37 ms after the second deadline (3.7 periods late)
    ASSERT_EQ(wake_and_step(&sched, 37000), PERIOD + 37000);
    uint64_t now = platform_now_us();
    ASSERT_EQ(now, start + 2 * PERIOD + 37000);

    // Not due again immediately...
    ASSERT_EQ(tick_sched_step(&sched, now), 0);
    // ...the next deadline is the next grid point after now
    ASSERT_EQ(tick_sched_deadline(&sched), start + 6 * PERIOD);
    ASSERT_EQ(wake_and_step(&sched, 0), 3000);
    ASSERT_EQ(wake_and_step(&sched, 0), PERIOD);

    // Landing exactly on a later grid point also moves strictly forward
    fake.now_us = start + 20 * PERIOD;
    ASSERT(tick_sched_step(&sched, platform_now_us()) > 0);
    ASSERT_EQ(tick_sched_deadline(&sched), start + 21 * PERIOD);
    TEST_PASS();
}

TEST(no_double_processing) {
    fake_reset();
    TickSched sched;
    tick_sched_init(&sched, platform_now_us(), PERIOD);

    // Before the first deadline nothing is due, however often it is asked
    ASSERT_EQ(tick_sched_step(&sched, platform_now_us()), 0);
    ASSERT_EQ(tick_sched_step(&sched, platform_now_us() + PERIOD - 1), 0);

    ASSERT_EQ(wake_and_step(&sched, 0), PERIOD);
    uint64_t now = platform_now_us();
    for (int i = 0; i < 5; i++) ASSERT_EQ(tick_sched_step(&sched, now), 0);
    ASSERT_EQ(tick_sched_step(&sched, now + PERIOD - 1), 0);   // a spurious early wake
    ASSERT_EQ(tick_sched_step(&sched, now + PERIOD), PERIOD);
    TEST_PASS();
}

/* Scheduler and motion_tick together: a 1 s stall moves the cursor by at most
   MOTION_MAX_DT_MS worth of motion, then normal ticks resume. */
TEST(stall_is_clamped_to_max_dt) {
    Rig rig;
    rig_init(&rig);
    rig.config.mouse.smoothing = 0.0f;
    rig_report(&rig, 0, 0, 0, 0, 0, 32767, 0);

    TickSched sched;
    tick_sched_init(&sched, platform_now_us(), PERIOD);
    rig_tick(&rig, (float)wake_and_step(&sched, 0) / 1000.0f);
    float per_tick = rig.move_dx;
    ASSERT(per_tick > 0.0f);

    uint64_t elapsed = wake_and_step(&sched, 1000000);
    ASSERT_EQ(elapsed, PERIOD + 1000000u);
    OutQueue queue;
    out_queue_init(&queue);
    motion_tick(&rig.state, &rig.config, (float)elapsed / 1000.0f, &queue);
    ASSERT_EQ(queue.count, 1);
    ASSERT_FLOAT_EQ(queue.events[0].dx, per_tick * (MOTION_MAX_DT_MS / MOTION_TICK_MS), 0.01f);

    // Only one tick ran for the whole stall
    ASSERT_EQ(tick_sched_step(&sched, platform_now_us()), 0);
    TEST_PASS();
}

TEST(zero_period_is_safe) {
    TickSched sched;
    tick_sched_init(&sched, 100, 0);              // would divide by zero
    ASSERT(tick_sched_deadline(&sched) > 100);
    ASSERT(tick_sched_step(&sched, 5000) > 0);
    ASSERT(tick_sched_deadline(&sched) > 5000);
    TEST_PASS();
}

TEST(duration_stats) {
    DurationStats stats;
    duration_stats_reset(&stats);
    ASSERT_EQ(duration_stats_average(&stats), 0);   // empty: no divide by zero
    duration_stats_add(&stats, 10000);
    duration_stats_add(&stats, 14000);
    duration_stats_add(&stats, 9000);
    ASSERT_EQ(stats.count, 3);
    ASSERT_EQ(stats.min, 9000);
    ASSERT_EQ(stats.max, 14000);
    ASSERT_EQ(duration_stats_average(&stats), 11000);
    duration_stats_reset(&stats);
    duration_stats_add(&stats, 50);                 // min restarts after a reset
    ASSERT_EQ(stats.min, 50);
    ASSERT_EQ(stats.max, 50);
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Motion Clock");
    RUN_TEST(steady_period);
    RUN_TEST(jitter_does_not_drift);
    RUN_TEST(overrun_recovers_without_bursting);
    RUN_TEST(no_double_processing);
    RUN_TEST(stall_is_clamped_to_max_dt);
    RUN_TEST(zero_period_is_safe);
    RUN_TEST(duration_stats);
    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
