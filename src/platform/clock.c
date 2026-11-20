/*******************************************************************************
 * clock.c - Monotonic clock and absolute-deadline sleep (mach time)
 ******************************************************************************/

#include "../../include/platform.h"
#include <mach/mach_time.h>
#include <time.h>

uint64_t platform_now_us(void) {
    // CLOCK_UPTIME_RAW is mach_absolute_time() expressed in nanoseconds
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000u;
}

void platform_sleep_until_us(uint64_t deadline_us) {
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) mach_timebase_info(&timebase);

    // Sleep the remaining time measured in mach ticks. Converting only the
    // (small) remainder avoids overflow when scaling a full uptime value.
    uint64_t now_us = platform_now_us();
    if (deadline_us <= now_us) return;
    uint64_t remaining_ticks = (deadline_us - now_us) * 1000u * timebase.denom / timebase.numer;
    mach_wait_until(mach_absolute_time() + remaining_ticks);
}
