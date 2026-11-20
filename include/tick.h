/*******************************************************************************
 * tick.h - Fixed-rate tick scheduling and timing statistics (pure logic)
 ******************************************************************************/

#ifndef TICK_H
#define TICK_H

#include <stdint.h>

/*******************************************************************************
 * Tick scheduler
 *
 * Deadlines sit on a fixed grid (start + n * period), so one late tick does
 * not push every later tick back. Usage:
 *
 *     sleep until tick_sched_deadline(&s);
 *     dt = tick_sched_step(&s, now);
 *     if (dt) run one tick with elapsed time dt;
 *
 * After an overrun the missed deadlines are skipped, not replayed: the next
 * tick reports the whole elapsed time once and the schedule rejoins the grid.
 ******************************************************************************/
typedef struct {
    uint64_t period_us;
    uint64_t deadline_us;    // Next time a tick is due
    uint64_t last_tick_us;   // When the previous tick ran
} TickSched;

void tick_sched_init(TickSched *sched, uint64_t now_us, uint64_t period_us);
uint64_t tick_sched_deadline(const TickSched *sched);

/* Returns the time since the previous tick if one is due at now_us, or 0 if
   the deadline has not arrived (so a tick can never be processed twice). */
uint64_t tick_sched_step(TickSched *sched, uint64_t now_us);

/*******************************************************************************
 * Min / average / max of a series of durations
 ******************************************************************************/
typedef struct {
    uint64_t count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;
} DurationStats;

void duration_stats_reset(DurationStats *stats);
void duration_stats_add(DurationStats *stats, uint64_t value);
uint64_t duration_stats_average(const DurationStats *stats);

#endif // TICK_H
