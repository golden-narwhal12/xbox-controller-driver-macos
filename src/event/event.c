/*******************************************************************************
 * event.c - Event injection implementation (keyboard/mouse)
 ******************************************************************************/

// ApplicationServices includes CoreGraphics and handles include ordering properly
#include <ApplicationServices/ApplicationServices.h>

// Include our headers after system headers
#include "../../include/event.h"
#include "../../include/log.h"
#include <math.h>
#include <string.h>

static float g_delta_remainder_x = 0.0f;
static float g_delta_remainder_y = 0.0f;
static CGEventSourceRef g_source = NULL;
static bool g_modifier_down[256];
static CGEventFlags g_synthetic_flags = 0;

static CGEventSourceRef event_source(void) {
    if (!g_source) g_source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    return g_source;
}

static CGEventFlags modifier_flag(uint16_t keycode) {
    switch (keycode) {
        case 0x38: case 0x3C: return kCGEventFlagMaskShift;
        case 0x3B: case 0x3E: return kCGEventFlagMaskControl;
        case 0x3A: case 0x3D: return kCGEventFlagMaskAlternate;
        case 0x37: case 0x36: return kCGEventFlagMaskCommand;
        default: return 0;
    }
}

static CGEventFlags current_flags(void) {
    return CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState) | g_synthetic_flags;
}

static void update_modifier_flags(void) {
    g_synthetic_flags = 0;
    if (g_modifier_down[0x38] || g_modifier_down[0x3C]) g_synthetic_flags |= kCGEventFlagMaskShift;
    if (g_modifier_down[0x3B] || g_modifier_down[0x3E]) g_synthetic_flags |= kCGEventFlagMaskControl;
    if (g_modifier_down[0x3A] || g_modifier_down[0x3D]) g_synthetic_flags |= kCGEventFlagMaskAlternate;
    if (g_modifier_down[0x37] || g_modifier_down[0x36]) g_synthetic_flags |= kCGEventFlagMaskCommand;
}

void send_key_event(uint16_t keycode, bool pressed) {
    CGEventRef event = CGEventCreateKeyboardEvent(event_source(), (CGKeyCode)keycode, pressed);
    if (event) {
        if (modifier_flag(keycode)) {
            g_modifier_down[keycode] = pressed;
            update_modifier_flags();
            CGEventSetType(event, kCGEventFlagsChanged);
        }
        CGEventSetFlags(event, current_flags());
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        LOG_DEBUG("Key event: keycode=0x%02x pressed=%d", keycode, pressed);
    } else {
        LOG_ERROR("Failed to create keyboard event for keycode 0x%02x", keycode);
    }
}

void send_mouse_button_event(int button, bool pressed) {
    CGPoint currentPos;
    CGEventRef getPos = CGEventCreate(NULL);
    if (!getPos) {
        LOG_ERROR("Failed to read mouse position");
        return;
    }
    currentPos = CGEventGetLocation(getPos);
    CFRelease(getPos);

    CGEventType eventType;
    CGMouseButton cgButton = (CGMouseButton)button;

    switch (button) {
        case 0:  // MOUSE_BUTTON_LEFT / kCGMouseButtonLeft
            eventType = pressed ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
            break;
        case 1:  // MOUSE_BUTTON_RIGHT / kCGMouseButtonRight
            eventType = pressed ? kCGEventRightMouseDown : kCGEventRightMouseUp;
            break;
        case 2:  // MOUSE_BUTTON_CENTER / kCGMouseButtonCenter
            eventType = pressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
            break;
        default:
            LOG_WARN("Unknown mouse button: %d", button);
            return;
    }

    CGEventRef event = CGEventCreateMouseEvent(event_source(), eventType, currentPos, cgButton);
    if (event) {
        CGEventSetFlags(event, current_flags());
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        LOG_DEBUG("Mouse button event: button=%d pressed=%d", button, pressed);
    } else {
        LOG_ERROR("Failed to create mouse button event");
    }
}

void send_mouse_movement(float dx, float dy, bool streaming_mode,
                         bool left_down, bool right_down, bool middle_down) {
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    CGPoint currentPos;
    CGEventRef getPos = CGEventCreate(NULL);
    if (!getPos) {
        LOG_ERROR("Failed to read mouse position");
        return;
    }
    currentPos = CGEventGetLocation(getPos);
    CFRelease(getPos);

    CGEventType type = kCGEventMouseMoved;
    CGMouseButton button = kCGMouseButtonLeft;
    if (left_down) {
        type = kCGEventLeftMouseDragged;
    } else if (right_down) {
        type = kCGEventRightMouseDragged;
        button = kCGMouseButtonRight;
    } else if (middle_down) {
        type = kCGEventOtherMouseDragged;
        button = kCGMouseButtonCenter;
    }

    g_delta_remainder_x += dx;
    g_delta_remainder_y += dy;
    int64_t delta_x = (int64_t)lroundf(g_delta_remainder_x);
    int64_t delta_y = (int64_t)lroundf(g_delta_remainder_y);
    g_delta_remainder_x -= delta_x;
    g_delta_remainder_y -= delta_y;

    CGEventRef event;

    if (streaming_mode) {
        // Streaming mode: Use delta fields (for Moonlight, Parsec, etc.)
        event = CGEventCreateMouseEvent(event_source(), type, currentPos, button);
    } else {
        // Local mode: Use absolute positioning (for native macOS apps)
        CGPoint newPos = CGPointMake(currentPos.x + dx, currentPos.y + dy);
        event = CGEventCreateMouseEvent(event_source(), type, newPos, button);
    }

    if (event) {
        CGEventSetFlags(event, current_flags());
        CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, delta_x);
        CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, delta_y);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

void event_cleanup(void) {
    if (g_source) {
        CFRelease(g_source);
        g_source = NULL;
    }
    memset(g_modifier_down, 0, sizeof(g_modifier_down));
    g_synthetic_flags = 0;
    g_delta_remainder_x = g_delta_remainder_y = 0.0f;
}
