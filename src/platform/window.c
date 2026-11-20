/*******************************************************************************
 * window.c - Target window tracking (Accessibility side of platform.h)
 *
 * Accessibility calls are synchronous IPC to another app and can take
 * milliseconds (or time out if that app is hung), so they never run on the
 * input or motion path. A background thread works out "what is the focused
 * window of the frontmost app, and where is it?" a few times a second and
 * publishes the answer; platform_target_window() only copies the latest
 * snapshot. The thread itself asks the app as rarely as it can: see
 * "Finding the target" below.
 *
 * Coordinates: the Accessibility API and CGWindowList both report global
 * points with a top-left origin, the same space CGEvent uses, so no
 * conversion is needed (NSScreen / NSWindow, which are bottom-left, are not
 * used anywhere in the driver).
 ******************************************************************************/

#include <ApplicationServices/ApplicationServices.h>

#include "../../include/platform.h"
#include "../../include/log.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define TRACKER_INTERVAL_MS      150     // Polling period
#define AX_TIMEOUT_SECONDS       0.25f   // Give up on an unresponsive app
#define FULLSCREEN_TOLERANCE     1.0     // Points of slack matching a display
#define FRAME_TOLERANCE          1.0     // ...and matching one window to another
#define MAX_DISPLAYS             16

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   // Guards everything below
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static WindowTarget g_published;
static bool g_running = false;
static bool g_enabled = false;
static bool g_poked = false;
static pthread_t g_thread;

/*******************************************************************************
 * Queries (called without g_lock held)
 ******************************************************************************/
static bool covers_a_display(ScreenRect frame) {
    CGDirectDisplayID displays[MAX_DISPLAYS];
    uint32_t count = 0;
    if (CGGetActiveDisplayList(MAX_DISPLAYS, displays, &count) != kCGErrorSuccess) return false;
    for (uint32_t i = 0; i < count; i++) {
        CGRect bounds = CGDisplayBounds(displays[i]);
        if (fabs(bounds.origin.x - frame.x) <= FULLSCREEN_TOLERANCE &&
            fabs(bounds.origin.y - frame.y) <= FULLSCREEN_TOLERANCE &&
            fabs(bounds.size.width - frame.w) <= FULLSCREEN_TOLERANCE &&
            fabs(bounds.size.height - frame.h) <= FULLSCREEN_TOLERANCE) {
            return true;
        }
    }
    return false;
}

/* The frontmost app is the one that owns the element with keyboard focus.
   (The more obvious kAXFocusedApplicationAttribute fails with
   kAXErrorCannotComplete on current macOS; this query does not.) */
static bool frontmost_pid(pid_t *pid) {
    static AXUIElementRef system_wide = NULL;   // Only used by the tracker thread
    if (!system_wide) {
        system_wide = AXUIElementCreateSystemWide();
        if (!system_wide) return false;
        AXUIElementSetMessagingTimeout(system_wide, AX_TIMEOUT_SECONDS);
    }

    CFTypeRef focused = NULL;
    if (AXUIElementCopyAttributeValue(system_wide, kAXFocusedUIElementAttribute, &focused)
            != kAXErrorSuccess || !focused) {
        return false;
    }
    bool ok = AXUIElementGetPid((AXUIElementRef)focused, pid) == kAXErrorSuccess;
    CFRelease(focused);
    return ok;
}

typedef enum { AX_WINDOW_FOUND, AX_NO_WINDOW, AX_FAILED } AxResult;

/* Focused window of one app via Accessibility. */
static AxResult focused_window(pid_t pid, WindowTarget *out) {
    AXUIElementRef app = AXUIElementCreateApplication(pid);
    if (!app) return AX_FAILED;
    AXUIElementSetMessagingTimeout(app, AX_TIMEOUT_SECONDS);

    AxResult result = AX_FAILED;
    CFTypeRef window = NULL, position_value = NULL, size_value = NULL, fullscreen = NULL;
    AXError error = AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute, &window);
    if (error == kAXErrorNoValue) {
        result = AX_NO_WINDOW;            // e.g. Finder with only the desktop
        goto done;
    }
    if (error != kAXErrorSuccess || !window) goto done;

    CGPoint origin;
    CGSize size;
    if (AXUIElementCopyAttributeValue((AXUIElementRef)window, kAXPositionAttribute,
                                      &position_value) != kAXErrorSuccess ||
        AXUIElementCopyAttributeValue((AXUIElementRef)window, kAXSizeAttribute,
                                      &size_value) != kAXErrorSuccess ||
        !AXValueGetValue((AXValueRef)position_value, kAXValueTypeCGPoint, &origin) ||
        !AXValueGetValue((AXValueRef)size_value, kAXValueTypeCGSize, &size)) {
        goto done;
    }

    out->valid = true;
    out->frame = (ScreenRect){origin.x, origin.y, size.width, size.height};
    out->window_id = ((uint64_t)(uint32_t)pid << 32) ^ (uint64_t)CFHash(window);
    if (AXUIElementCopyAttributeValue((AXUIElementRef)window, CFSTR("AXFullScreen"),
                                      &fullscreen) == kAXErrorSuccess && fullscreen &&
        CFGetTypeID(fullscreen) == CFBooleanGetTypeID()) {
        out->fullscreen = CFBooleanGetValue((CFBooleanRef)fullscreen);
    }
    // Games often use a borderless window covering the display instead
    out->fullscreen = out->fullscreen || covers_a_display(out->frame);
    result = AX_WINDOW_FOUND;

done:
    if (fullscreen) CFRelease(fullscreen);
    if (size_value) CFRelease(size_value);
    if (position_value) CFRelease(position_value);
    if (window) CFRelease(window);
    CFRelease(app);
    return result;
}

/*******************************************************************************
 * The window server's list (no cost to the app that owns the windows)
 ******************************************************************************/
typedef struct {
    int pid;
    int layer;           // 0 for normal windows; games' own fullscreen modes
                         // put their window on a higher layer
    int64_t number;      // CGWindowID
    ScreenRect bounds;
} ListedWindow;

static CFArrayRef copy_window_list(void) {
    return CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
}

static bool listed_window_at(CFArrayRef windows, CFIndex index, ListedWindow *out) {
    CFDictionaryRef info = CFArrayGetValueAtIndex(windows, index);
    CGRect bounds;
    CFNumberRef layer_ref = CFDictionaryGetValue(info, kCGWindowLayer);
    CFNumberRef pid_ref = CFDictionaryGetValue(info, kCGWindowOwnerPID);
    CFNumberRef number_ref = CFDictionaryGetValue(info, kCGWindowNumber);
    CFDictionaryRef bounds_ref = CFDictionaryGetValue(info, kCGWindowBounds);
    if (!layer_ref || !pid_ref || !number_ref || !bounds_ref) return false;
    if (!CFNumberGetValue(layer_ref, kCFNumberIntType, &out->layer)) return false;
    if (!CFNumberGetValue(pid_ref, kCFNumberIntType, &out->pid)) return false;
    if (!CFNumberGetValue(number_ref, kCFNumberSInt64Type, &out->number)) return false;
    if (!CGRectMakeWithDictionaryRepresentation(bounds_ref, &bounds)) return false;
    out->bounds = (ScreenRect){bounds.origin.x, bounds.origin.y,
                               bounds.size.width, bounds.size.height};
    return true;
}

typedef bool (*WindowMatch)(const ListedWindow *window, const void *context);

/* First window, front to back, that `match` accepts. */
static bool find_listed_window(CFArrayRef windows, WindowMatch match, const void *context,
                               ListedWindow *out) {
    if (!windows) return false;
    CFIndex count = CFArrayGetCount(windows);
    for (CFIndex i = 0; i < count; i++) {
        if (listed_window_at(windows, i, out) && match(out, context)) return true;
    }
    return false;
}

/* The list also holds the menu bar, Dock, overlays and so on, all on other
   layers; "the frontmost window" means the frontmost normal one. */
static bool match_normal(const ListedWindow *window, const void *context) {
    (void)context;
    return window->layer == 0;
}

static bool match_number(const ListedWindow *window, const void *context) {
    return window->number == *(const int64_t *)context;
}

static bool match_normal_of_pid(const ListedWindow *window, const void *context) {
    return window->layer == 0 && window->pid == *(const int *)context;
}

/* Any layer: this is how a game's own (non-layer-0) fullscreen window is found. */
static bool match_pid_and_frame(const ListedWindow *window, const void *context) {
    const WindowTarget *target = context;
    return window->pid == target->pid &&
           fabs(window->bounds.x - target->frame.x) <= FRAME_TOLERANCE &&
           fabs(window->bounds.y - target->frame.y) <= FRAME_TOLERANCE &&
           fabs(window->bounds.w - target->frame.w) <= FRAME_TOLERANCE &&
           fabs(window->bounds.h - target->frame.h) <= FRAME_TOLERANCE;
}

static void target_from_listed(const ListedWindow *window, WindowTarget *out) {
    out->valid = true;
    out->frame = window->bounds;
    out->pid = window->pid;
    out->window_id = (uint64_t)window->number;
    out->fullscreen = covers_a_display(out->frame);
}

/*******************************************************************************
 * Finding the target
 *
 * Accessibility requests are answered on the target app's main thread. For a
 * game that is the render thread, so asking several times a second makes the
 * game drop frames (seen in fullscreen Minecraft). So Accessibility is used
 * only to *identify* the window: when the frontmost window changes, when the
 * tracked window changes size (fullscreen toggles do), or on a poke. In
 * between, the tracked window's position is followed through the window
 * server's list, which costs the app nothing.
 ******************************************************************************/
static struct {
    bool valid;              // false: identify again on the next poll
    bool have_top;           // What the front of the list looked like when
    int top_pid;             // the target was identified
    int64_t top_number;
    int64_t tracked_number;  // CGWindowID followed between identifications; 0 = none
    WindowTarget target;
} g_cache;                   // Tracker thread only

/* Asks Accessibility (and the list) which window is the target. */
static void identify_target(CFArrayRef windows, WindowTarget *out) {
    static bool warned = false;
    memset(out, 0, sizeof(*out));
    g_cache.tracked_number = 0;
    g_cache.valid = true;

    ListedWindow listed;
    pid_t pid = 0;
    bool trusted = AXIsProcessTrusted();
    if (trusted && frontmost_pid(&pid)) {
        out->pid = pid;
        AxResult result = focused_window(pid, out);
        if (result == AX_WINDOW_FOUND) {
            // Tie the Accessibility window to its window-server twin so it
            // can be followed without asking the app again
            if (find_listed_window(windows, match_pid_and_frame, out, &listed)) {
                g_cache.tracked_number = listed.number;
                out->window_id = (uint64_t)listed.number;
            } else {
                g_cache.valid = false;   // cannot follow it: identify every poll
            }
        } else if (result == AX_FAILED) {
            // An app that does not answer Accessibility still has windows
            int owner = (int)pid;
            if (find_listed_window(windows, match_normal_of_pid, &owner, &listed)) {
                target_from_listed(&listed, out);
                g_cache.tracked_number = listed.number;
            }
        }
        return;   // AX_NO_WINDOW: frontmost app has no window (e.g. the desktop)
    }

    // Trusted but no answer just means nothing has keyboard focus right now
    if (!trusted && !warned) {
        warned = true;
        LOG_WARN("No Accessibility permission; finding the window to confine the "
                 "cursor to from the on-screen window list instead");
    }
    // Limitation of this path: if the frontmost app has no window at all, the
    // topmost window of another app is used.
    if (find_listed_window(windows, match_normal, NULL, &listed)) {
        target_from_listed(&listed, out);
        g_cache.tracked_number = listed.number;
    }
}

static void query_target(WindowTarget *out, bool force) {
    CFArrayRef windows = copy_window_list();
    ListedWindow top, tracked;
    bool have_top = find_listed_window(windows, match_normal, NULL, &top);

    bool same_front = g_cache.valid && !force && have_top == g_cache.have_top &&
                      (!have_top || (top.pid == g_cache.top_pid &&
                                     top.number == g_cache.top_number));
    bool follow = false;
    if (same_front && g_cache.tracked_number) {
        // Same window in front: just read where it is now. A new size means
        // something more than a move happened, so identify again.
        follow = find_listed_window(windows, match_number, &g_cache.tracked_number, &tracked) &&
                 fabs(tracked.bounds.w - g_cache.target.frame.w) <= FRAME_TOLERANCE &&
                 fabs(tracked.bounds.h - g_cache.target.frame.h) <= FRAME_TOLERANCE;
        if (follow) g_cache.target.frame = tracked.bounds;
    } else if (same_front) {
        follow = true;       // still "no window"
    }

    if (!follow) {
        identify_target(windows, &g_cache.target);
        LOG_DEBUG("Window target identified: pid %d, %s, %.0fx%.0f at (%.0f, %.0f)%s",
                  g_cache.target.pid, g_cache.target.valid ? "window" : "no window",
                  g_cache.target.frame.w, g_cache.target.frame.h,
                  g_cache.target.frame.x, g_cache.target.frame.y,
                  g_cache.target.fullscreen ? ", fullscreen" : "");
        g_cache.have_top = have_top;
        g_cache.top_pid = have_top ? top.pid : 0;
        g_cache.top_number = have_top ? top.number : 0;
    }
    *out = g_cache.target;
    if (windows) CFRelease(windows);
}

/*******************************************************************************
 * Publishing
 ******************************************************************************/
static bool same_target(const WindowTarget *a, const WindowTarget *b) {
    return a->valid == b->valid && a->fullscreen == b->fullscreen &&
           a->pid == b->pid && a->window_id == b->window_id &&
           a->frame.x == b->frame.x && a->frame.y == b->frame.y &&
           a->frame.w == b->frame.w && a->frame.h == b->frame.h;
}

/* Call with g_lock held. */
static void publish_locked(const WindowTarget *target) {
    if (same_target(&g_published, target)) return;
    uint32_t generation = g_published.generation + 1;
    g_published = *target;
    g_published.generation = generation;
}

void platform_target_window(WindowTarget *out) {
    pthread_mutex_lock(&g_lock);
    *out = g_published;
    pthread_mutex_unlock(&g_lock);
}

/*******************************************************************************
 * Tracker thread
 ******************************************************************************/
static void *tracker_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_lock);
    while (g_running) {
        WindowTarget target;
        memset(&target, 0, sizeof(target));
        bool force = g_poked;                 // poked or (re)enabled: identify afresh
        g_poked = false;
        if (g_enabled) {
            pthread_mutex_unlock(&g_lock);
            query_target(&target, force);     // Slow part: no lock held
            pthread_mutex_lock(&g_lock);
        }
        // Disabled mid-query: don't publish a window nobody asked for
        if (!g_enabled) {
            memset(&target, 0, sizeof(target));
            g_cache.valid = false;
        }
        publish_locked(&target);

        if (g_running && !g_poked) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long nsec = now.tv_usec * 1000L + TRACKER_INTERVAL_MS * 1000000L;
            struct timespec until = {now.tv_sec + nsec / 1000000000L, nsec % 1000000000L};
            pthread_cond_timedwait(&g_wake, &g_lock, &until);
        }
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

int window_tracker_start(void) {
    pthread_mutex_lock(&g_lock);
    bool already = g_running;
    g_running = true;
    pthread_mutex_unlock(&g_lock);
    if (already) return 0;

    int result = pthread_create(&g_thread, NULL, tracker_thread, NULL);
    if (result != 0) {
        pthread_mutex_lock(&g_lock);
        g_running = false;
        pthread_mutex_unlock(&g_lock);
        LOG_WARN("Could not start the window tracker (%s); cursor confinement "
                 "falls back to displays", strerror(result));
    }
    return result;
}

void window_tracker_stop(void) {
    pthread_mutex_lock(&g_lock);
    bool was_running = g_running;
    g_running = false;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
    if (was_running) pthread_join(g_thread, NULL);
}

void window_tracker_set_enabled(bool enabled) {
    pthread_mutex_lock(&g_lock);
    g_enabled = enabled;
    g_poked = true;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}

void window_tracker_poke(void) {
    pthread_mutex_lock(&g_lock);
    g_poked = true;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}
