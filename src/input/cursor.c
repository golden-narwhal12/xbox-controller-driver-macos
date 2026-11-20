/*******************************************************************************
 * cursor.c - Cursor confinement policy and event dispatch
 ******************************************************************************/

#include "../../include/cursor.h"
#include <math.h>
#include <string.h>

void cursor_state_init(CursorState *state, int own_pid) {
    memset(state, 0, sizeof(*state));
    state->own_pid = own_pid;
}

/*******************************************************************************
 * Pure geometry
 ******************************************************************************/
bool cursor_confine_rect(const WindowTarget *window, const CursorConfig *config,
                         int own_pid, ScreenRect *out) {
    if (config->confine != CONFINE_WINDOW) return false;
    if (!window->valid || window->pid == own_pid) return false;

    // The margin applies to every window. It keeps clicks out of the frame's
    // resize border (a click there starts a resize, not a click in the app),
    // and for a fullscreen window it keeps the cursor off the display edge,
    // where macOS reveals the menu bar and Dock over the game.
    // Only the title bar inset is skipped for fullscreen: there is none.
    double margin = config->margin;
    double top = window->fullscreen ? 0.0 : config->top_inset;
    ScreenRect rect = {
        .x = window->frame.x + margin,
        .y = window->frame.y + margin + top,
        .w = window->frame.w - 2.0 * margin,
        .h = window->frame.h - 2.0 * margin - top,
    };
    // Also rejects zero, negative and NaN sizes (written so NaN fails)
    if (!(rect.w >= 1.0) || !(rect.h >= 1.0)) return false;
    *out = rect;
    return true;
}

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

ScreenPoint cursor_clamp_to_rect(ScreenPoint position, ScreenRect rect) {
    // The last point inside a rect of width w starting at x is x + w - 1
    return (ScreenPoint){
        clampd(position.x, rect.x, rect.x + rect.w - 1.0),
        clampd(position.y, rect.y, rect.y + rect.h - 1.0),
    };
}

ScreenPoint cursor_confine(ScreenPoint position, double dx, double dy, ScreenRect rect) {
    ScreenPoint start = cursor_clamp_to_rect(position, rect);
    return cursor_clamp_to_rect((ScreenPoint){start.x + dx, start.y + dy}, rect);
}

static bool same_point(ScreenPoint a, ScreenPoint b) {
    return a.x == b.x && a.y == b.y;
}

/*******************************************************************************
 * Helpers that consult the platform
 ******************************************************************************/
static bool current_confine_rect(const CursorState *state, const CursorConfig *config,
                                 WindowTarget *window, ScreenRect *rect) {
    memset(window, 0, sizeof(*window));
    if (config->confine != CONFINE_WINDOW) return false;
    platform_target_window(window);   // cached snapshot; never blocks
    return cursor_confine_rect(window, config, state->own_pid, rect);
}

/* Displays can form any shape (side by side, offset, L-shaped), so there is
   no rectangle to clamp to: test points instead. Moves from `start` toward
   `target` one axis at a time, each as far as it stays on a display, so the
   cursor reaches the very edge and still slides along it. `start` is expected
   to be on a display; if it is not, the OS gets the final say on `target`. */
static double advance_on_display(ScreenPoint from, double delta, bool x_axis) {
    double origin = x_axis ? from.x : from.y;
    ScreenPoint probe = from;
    double reachable = 0.0, blocked = fabs(delta);   // whole points along the axis
    double sign = delta < 0.0 ? -1.0 : 1.0;

    if (x_axis) probe.x = origin + delta; else probe.y = origin + delta;
    if (platform_point_on_display(probe)) return origin + delta;

    while (blocked - reachable > 1.0) {
        double middle = floor((reachable + blocked) / 2.0);
        if (x_axis) probe.x = origin + sign * middle; else probe.y = origin + sign * middle;
        if (platform_point_on_display(probe)) reachable = middle;
        else blocked = middle;
    }
    return origin + sign * reachable;
}

static ScreenPoint slide_on_displays(ScreenPoint start, ScreenPoint target) {
    if (platform_point_on_display(target) || !platform_point_on_display(start)) return target;
    ScreenPoint result = start;
    result.x = advance_on_display(result, target.x - start.x, true);
    result.y = advance_on_display(result, target.y - start.y, false);
    return result;
}

/*******************************************************************************
 * Motion
 ******************************************************************************/
void cursor_move(CursorState *state, const CursorConfig *config,
                 float dx, float dy, int drag_button) {
    // Deliver whole points; carry the fraction so slow motion still adds up.
    // The same whole-point step moves the cursor and fills the delta fields.
    state->remainder_x += dx;
    state->remainder_y += dy;
    int step_x = (int)lroundf(state->remainder_x);
    int step_y = (int)lroundf(state->remainder_y);
    state->remainder_x -= (float)step_x;
    state->remainder_y -= (float)step_y;
    if (step_x == 0 && step_y == 0) return;

    ScreenPoint position = platform_cursor_position();
    ScreenPoint target = position;
    WindowTarget window;
    ScreenRect rect;
    bool confined = current_confine_rect(state, config, &window, &rect);

    if (config->streaming_mode) {
        // Deltas only: the receiving app turns them into motion itself.
    } else if (confined) {
        ScreenPoint start = cursor_clamp_to_rect(position, rect);
        target = cursor_confine(position, step_x, step_y, rect);
        bool blocked = target.x != start.x + step_x || target.y != start.y + step_y;
        if (blocked && config->recenter_when_captured) {
            target = (ScreenPoint){floor(rect.x + rect.w / 2.0), floor(rect.y + rect.h / 2.0)};
            platform_warp_cursor(target);
        } else {
            // A window can hang off the edge of the displays
            target = slide_on_displays(start, target);
        }
    } else if (config->confine == CONFINE_OFF) {
        target = (ScreenPoint){position.x + step_x, position.y + step_y};
    } else {
        target = slide_on_displays(position, (ScreenPoint){position.x + step_x,
                                                           position.y + step_y});
    }

    // Held buttons are now positioned relative to the window as it is now
    for (int button = 0; button < MOUSE_BUTTON_COUNT; button++) {
        if (state->anchor[button].valid && confined &&
            state->anchor[button].window_id == window.window_id) {
            state->anchor[button].origin_x = window.frame.x;
            state->anchor[button].origin_y = window.frame.y;
        }
    }

    // The delta fields always carry the full step, even when the position is
    // clamped: apps that capture the mouse read these and keep turning.
    platform_post_mouse_move(target, step_x, step_y, drag_button);
}

/*******************************************************************************
 * Buttons
 ******************************************************************************/
static int click_count_for_press(CursorState *state, int button, ScreenPoint position) {
    uint64_t now = platform_now_us();
    double dx = position.x - state->click[button].position.x;
    double dy = position.y - state->click[button].position.y;
    bool repeat = state->click[button].count > 0 &&
                  now - state->click[button].down_us <= DOUBLE_CLICK_INTERVAL_US &&
                  fabs(dx) <= DOUBLE_CLICK_DISTANCE && fabs(dy) <= DOUBLE_CLICK_DISTANCE;
    state->click[button].count = repeat ? state->click[button].count + 1 : 1;
    state->click[button].down_us = now;
    state->click[button].position = position;
    return state->click[button].count;
}

void cursor_button(CursorState *state, const CursorConfig *config,
                   int button, bool pressed) {
    if (button < 0 || button >= MOUSE_BUTTON_COUNT) return;

    ScreenPoint position = platform_cursor_position();
    WindowTarget window;
    ScreenRect rect;
    bool confined = current_confine_rect(state, config, &window, &rect);

    if (pressed) {
        if (confined) {
            // A click must land in the target window. If the cursor is outside
            // (the real mouse moved it, or the window shrank), move it in first.
            ScreenPoint inside = cursor_clamp_to_rect(position, rect);
            if (!same_point(inside, position)) {
                platform_warp_cursor(inside);
                position = inside;
            }
            state->anchor[button].window_id = window.window_id;
            state->anchor[button].origin_x = window.frame.x;
            state->anchor[button].origin_y = window.frame.y;
        }
        state->anchor[button].valid = confined;
        platform_post_mouse_button(button, true, position,
                                   click_count_for_press(state, button, position));
        return;
    }

    if (confined) {
        // Same window, but it moved while the button was held: release at the
        // same spot in the window. A different window (app switch): release
        // where the cursor is now, never at the old window's coordinates.
        if (state->anchor[button].valid &&
            state->anchor[button].window_id == window.window_id) {
            position.x += window.frame.x - state->anchor[button].origin_x;
            position.y += window.frame.y - state->anchor[button].origin_y;
        }
        position = cursor_clamp_to_rect(position, rect);
    }
    state->anchor[button].valid = false;
    int count = state->click[button].count > 0 ? state->click[button].count : 1;
    platform_post_mouse_button(button, false, position, count);
}

/*******************************************************************************
 * Dispatch
 ******************************************************************************/
void cursor_dispatch(CursorState *state, const CursorConfig *config, const OutQueue *queue) {
    for (int i = 0; i < queue->count; i++) {
        const OutEvent *event = &queue->events[i];
        switch (event->type) {
            case OUT_KEY:
                platform_post_key(event->code, event->pressed);
                break;
            case OUT_MOUSE_BUTTON:
                cursor_button(state, config, event->code, event->pressed);
                break;
            case OUT_MOUSE_MOVE:
                cursor_move(state, config, event->dx, event->dy, event->drag_button);
                break;
            case OUT_SCROLL:
                platform_post_scroll(event->lines);
                break;
        }
    }
}
