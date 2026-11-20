/*******************************************************************************
 * platform.h - The seam between driver logic and macOS
 *
 * Everything the logic needs from the OS goes through these functions.
 * Production links src/event/event.c (CoreGraphics), src/platform/window.c
 * (Accessibility) and src/platform/clock.c (mach time). Tests link fakes
 * (tests/fake_platform.h) and run the real logic against them.
 ******************************************************************************/

#ifndef PLATFORM_H
#define PLATFORM_H

#include "types.h"

/*******************************************************************************
 * Posting events. These post exactly what they are told; every decision about
 * where and whether to post is made by the caller (src/input/cursor.c).
 ******************************************************************************/
void platform_post_key(uint16_t keycode, bool pressed);
void platform_post_mouse_button(int button, bool pressed, ScreenPoint position, int click_count);
/* Posts a move (or a drag when drag_button is not MOUSE_BUTTON_NONE) at
   `position`, carrying (delta_x, delta_y) in the event's relative fields. */
void platform_post_mouse_move(ScreenPoint position, int delta_x, int delta_y, int drag_button);
/* Whole wheel notches; positive is wheel up. */
void platform_post_scroll(int lines);

/*******************************************************************************
 * Cursor and displays
 ******************************************************************************/
ScreenPoint platform_cursor_position(void);
void platform_warp_cursor(ScreenPoint position);
bool platform_point_on_display(ScreenPoint position);

/*******************************************************************************
 * Target window: the focused window of the frontmost app.
 *
 * Returns the most recently published snapshot and never blocks on the window
 * server, so it is safe on the motion path. `generation` changes whenever the
 * window, its frame or its validity changes.
 ******************************************************************************/
typedef struct {
    bool valid;            // false: no focused window (desktop, no windows)
    ScreenRect frame;            // Whole window, including its title bar
    bool fullscreen;
    int pid;               // Owning process
    uint64_t window_id;    // Stable while the same window stays focused
    uint32_t generation;
} WindowTarget;

void platform_target_window(WindowTarget *out);

/*******************************************************************************
 * Monotonic clock
 ******************************************************************************/
uint64_t platform_now_us(void);
void platform_sleep_until_us(uint64_t deadline_us);

/*******************************************************************************
 * Production-only lifecycle (not called by the logic under test)
 ******************************************************************************/
void platform_events_cleanup(void);

/* Background thread that keeps the target window snapshot fresh. */
int window_tracker_start(void);
void window_tracker_stop(void);
void window_tracker_set_enabled(bool enabled);
void window_tracker_poke(void);    // Refresh now (e.g. on app activation)

#endif // PLATFORM_H
