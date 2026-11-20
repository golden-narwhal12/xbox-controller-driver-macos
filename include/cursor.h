/*******************************************************************************
 * cursor.h - Cursor confinement policy and event dispatch
 *
 * Decides where controller-generated mouse motion and clicks land. The pure
 * functions here do the geometry; cursor_move / cursor_button combine them
 * with the platform seam (platform.h), which tests replace with fakes.
 *
 * Confinement applies only to events the controller generates. Nothing here
 * runs when only the physical mouse moves.
 ******************************************************************************/

#ifndef CURSOR_H
#define CURSOR_H

#include "types.h"
#include "platform.h"

#define DOUBLE_CLICK_INTERVAL_US  500000u   // Second click within this = double
#define DOUBLE_CLICK_DISTANCE     5.0       // ...and within this many points

typedef struct {
    int own_pid;                     // Windows of this process are never targets
    float remainder_x, remainder_y;  // Sub-point motion not yet delivered

    /* Where each held button went down, so its release can follow the window */
    struct {
        bool valid;
        uint64_t window_id;
        double origin_x, origin_y;   // Window origin when last in sync
    } anchor[MOUSE_BUTTON_COUNT];

    /* Multi-click detection */
    struct {
        uint64_t down_us;
        ScreenPoint position;
        int count;
    } click[MOUSE_BUTTON_COUNT];
} CursorState;

void cursor_state_init(CursorState *state, int own_pid);

/*******************************************************************************
 * Pure geometry
 ******************************************************************************/

/* The area controller motion and clicks are kept inside: the window frame
   inset by the margin on every side (fullscreen included), minus the title
   bar unless fullscreen. Returns false
   (no window confinement; fall back to displays) when confinement is not
   "window", there is no usable window, the window is our own, or the inset
   leaves nothing. */
bool cursor_confine_rect(const WindowTarget *window, const CursorConfig *config,
                         int own_pid, ScreenRect *out);

/* Nearest point inside the rect. */
ScreenPoint cursor_clamp_to_rect(ScreenPoint position, ScreenRect rect);

/* New cursor position for a requested motion: pull the start point into the
   rect, then move, clamping each axis on its own so motion along an edge
   continues when the other axis is blocked. */
ScreenPoint cursor_confine(ScreenPoint position, double dx, double dy, ScreenRect rect);

/*******************************************************************************
 * Actions (use the platform seam)
 ******************************************************************************/
void cursor_move(CursorState *state, const CursorConfig *config,
                 float dx, float dy, int drag_button);
void cursor_button(CursorState *state, const CursorConfig *config,
                   int button, bool pressed);

/* Posts every queued event in order. */
void cursor_dispatch(CursorState *state, const CursorConfig *config, const OutQueue *queue);

#endif // CURSOR_H
