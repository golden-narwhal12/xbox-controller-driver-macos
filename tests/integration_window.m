/*******************************************************************************
 * integration_window.m - Opt-in check of the real window tracker
 *
 * Run with `make test-window`. Not part of `make test`: it opens a real window
 * for a couple of seconds, takes focus, and needs a logged-in GUI session.
 *
 * Opens a Cocoa window, then moves and resizes it, and checks that
 * src/platform/window.c reports the same frame each time. This is the one
 * place the conversion between Cocoa's bottom-left coordinates and the
 * top-left global coordinates the driver uses is checked against the OS.
 *
 * With Accessibility permission granted to the terminal, the Accessibility
 * path is exercised; without it, the window-list fallback is.
 ******************************************************************************/

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include "../include/platform.h"
#include <unistd.h>

static int g_failures = 0;

/* Runs the app's event loop for a while (activation and window moves only
   take effect when events are processed). */
static void pump(double seconds) {
    NSDate *until = [NSDate dateWithTimeIntervalSinceNow:seconds];
    NSEvent *event;
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:until
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES])) {
        [NSApp sendEvent:event];
    }
}

/* Cocoa frame (bottom-left origin, primary screen) -> global top-left points */
static ScreenRect expected_frame(NSWindow *window) {
    NSRect frame = window.frame;
    CGFloat primary_height = NSScreen.screens.firstObject.frame.size.height;
    return (ScreenRect){frame.origin.x, primary_height - NSMaxY(frame),
                        frame.size.width, frame.size.height};
}

static bool close_enough(ScreenRect a, ScreenRect b) {
    return fabs(a.x - b.x) <= 1 && fabs(a.y - b.y) <= 1 &&
           fabs(a.w - b.w) <= 1 && fabs(a.h - b.h) <= 1;
}

static void check(NSWindow *window, const char *what, uint32_t *last_generation) {
    ScreenRect expected = expected_frame(window);
    WindowTarget target = {0};
    for (int attempt = 0; attempt < 30; attempt++) {     // up to 3 s
        pump(0.1);
        platform_target_window(&target);
        if (target.valid && target.pid == getpid() && close_enough(target.frame, expected)) break;
    }
    bool ok = target.valid && target.pid == getpid() && close_enough(target.frame, expected);
    bool generation_moved = target.generation != *last_generation;
    printf("  [%s] %-28s expected (%.0f, %.0f, %.0f x %.0f)  got (%.0f, %.0f, %.0f x %.0f)"
           " pid %d gen %u%s\n",
           ok && generation_moved ? "PASS" : "FAIL", what,
           expected.x, expected.y, expected.w, expected.h,
           target.frame.x, target.frame.y, target.frame.w, target.frame.h,
           target.pid, target.generation, generation_moved ? "" : " (generation did not change)");
    if (!ok || !generation_moved) g_failures++;
    *last_generation = target.generation;
}

/* Fallback when this process cannot become frontmost: the tracker's answer
   for the real frontmost app must match one of that app's on-screen windows
   as listed (independently) by the window server. */
static int observe_frontmost(void) {
    window_tracker_start();
    window_tracker_set_enabled(true);
    WindowTarget target = {0};
    for (int attempt = 0; attempt < 20 && target.generation == 0; attempt++) {
        pump(0.1);
        platform_target_window(&target);
    }
    window_tracker_stop();
    printf("  Not frontmost; observing the frontmost app instead.\n");
    if (!target.valid) {
        printf("  [PASS] tracker reports no focused window (pid %d)\n", target.pid);
        return 0;
    }

    bool matched = false;
    NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    for (NSDictionary *info in windows) {
        if ([info[(id)kCGWindowOwnerPID] intValue] != target.pid) continue;
        CGRect bounds;
        if (!CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)info[(id)kCGWindowBounds], &bounds)) continue;
        ScreenRect listed = {bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height};
        if (close_enough(listed, target.frame)) matched = true;
    }
    printf("  [%s] tracker frame (%.0f, %.0f, %.0f x %.0f) pid %d fullscreen %d %s\n",
           matched ? "PASS" : "FAIL", target.frame.x, target.frame.y, target.frame.w,
           target.frame.h, target.pid, target.fullscreen,
           matched ? "matches a window-server window of that app"
                   : "matches no on-screen window of that app");
    printf("\n%s\n", matched ? "Window tracking test passed (observe-only mode)"
                             : "Window tracking test FAILED");
    return matched ? 0 : 1;
}

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(200, 300, 480, 320)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"xbox driver window-tracking test";
        window.releasedWhenClosed = NO;      // ARC owns it
        [window makeKeyAndOrderFront:nil];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps:YES];
        [[NSRunningApplication currentApplication]
            activateWithOptions:NSApplicationActivateAllWindows];
        for (int i = 0; i < 30 && ![NSApp isActive]; i++) pump(0.1);

        printf("\n=== Integration: window tracker ===\n");
        printf("  Query path: %s\n", AXIsProcessTrusted()
               ? "Accessibility" : "window list (no Accessibility permission)");
        if (![NSApp isActive]) {
            // macOS can refuse to let a background-launched process take
            // focus. Then our own window is not "the focused window of the
            // frontmost app", so check the tracker against whatever is.
            int result = observe_frontmost();
            [window close];
            return result;
        }

        window_tracker_start();
        window_tracker_set_enabled(true);
        uint32_t generation = 0;
        check(window, "initial frame", &generation);

        [window setFrame:NSMakeRect(60, 80, 480, 320) display:YES];
        window_tracker_poke();
        check(window, "moved", &generation);

        [window setFrame:NSMakeRect(60, 80, 700, 500) display:YES];
        check(window, "grown", &generation);

        [window setFrame:NSMakeRect(350, 250, 260, 180) display:YES];
        check(window, "moved and shrunk", &generation);

        // Disabling the tracker withdraws the target
        window_tracker_set_enabled(false);
        pump(0.4);
        WindowTarget target;
        platform_target_window(&target);
        printf("  [%s] disabled tracker publishes no window\n", target.valid ? "FAIL" : "PASS");
        if (target.valid) g_failures++;

        window_tracker_stop();
        [window close];
        printf("\n%s\n", g_failures ? "Window tracking test FAILED" : "Window tracking test passed");
    }
    return g_failures ? 1 : 0;
}
