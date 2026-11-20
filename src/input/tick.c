/*******************************************************************************
 * tick.c - Fixed-rate tick scheduling and timing statistics (pure logic)
 ******************************************************************************/

#include "../../include/tick.h"

void tick_sched_init(TickSched *sched, uint64_t now_us, uint64_t period_us) {
    sched->period_us = period_us ? period_us : 1;
    sched->last_tick_us = now_us;
    sched->deadline_us = now_us + sched->period_us;
}

uint64_t tick_sched_deadline(const TickSched *sched) {
    return sched->deadline_us;
}

uint64_t tick_sched_step(TickSched *sched, uint64_t now_us) {
    if (now_us < sched->deadline_us) return 0;

    uint64_t elapsed = now_us - sched->last_tick_us;
    sched->last_tick_us = now_us;

    // Skip any deadlines that already passed; the next one is strictly in the
    // future and still on the original grid.
    uint64_t missed = (now_us - sched->deadline_us) / sched->period_us;
    sched->deadline_us += (missed + 1) * sched->period_us;
    return elapsed;
}

void duration_stats_reset(DurationStats *stats) {
    stats->count = 0;
    stats->sum = 0;
    stats->min = 0;
    stats->max = 0;
}

void duration_stats_add(DurationStats *stats, uint64_t value) {
    if (stats->count == 0 || value < stats->min) stats->min = value;
    if (value > stats->max) stats->max = value;
    stats->sum += value;
    stats->count++;
}

uint64_t duration_stats_average(const DurationStats *stats) {
    return stats->count ? stats->sum / stats->count : 0;
}
