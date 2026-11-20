/*******************************************************************************
 * event.c - Event posting and cursor queries (CoreGraphics side of platform.h)
 *
 * Posts exactly what it is told. Where the cursor may go and where clicks land
 * is decided in src/input/cursor.c.
 *
 * All functions here are called with the driver's output mutex held, so the
 * modifier state below is never touched by two threads at once.
 ******************************************************************************/

#include <ApplicationServices/ApplicationServices.h>

#include "../../include/platform.h"
#include "../../include/log.h"
#include <string.h>

/* macOS virtual keycodes of the modifier keys */
#define VK_COMMAND        0x37
#define VK_SHIFT          0x38
#define VK_OPTION         0x3A
#define VK_CONTROL        0x3B
#define VK_RIGHT_COMMAND  0x36
#define VK_RIGHT_SHIFT    0x3C
#define VK_RIGHT_OPTION   0x3D
#define VK_RIGHT_CONTROL  0x3E

static CGEventSourceRef g_source = NULL;
static bool g_modifier_down[KEY_COUNT];   // Modifiers this driver is holding

static CGEventSourceRef event_source(void) {
    if (!g_source) g_source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    return g_source;
}

static CGEventFlags modifier_flag(uint16_t keycode) {
    switch (keycode) {
        case VK_SHIFT:   case VK_RIGHT_SHIFT:   return kCGEventFlagMaskShift;
        case VK_CONTROL: case VK_RIGHT_CONTROL: return kCGEventFlagMaskControl;
        case VK_OPTION:  case VK_RIGHT_OPTION:  return kCGEventFlagMaskAlternate;
        case VK_COMMAND: case VK_RIGHT_COMMAND: return kCGEventFlagMaskCommand;
        default: return 0;
    }
}

/* Flags for a new event: modifiers physically held on the real keyboard plus
   the ones this driver is holding (which the HID state does not include). */
static CGEventFlags current_flags(void) {
    static const uint16_t MODIFIERS[] = {
        VK_SHIFT, VK_RIGHT_SHIFT, VK_CONTROL, VK_RIGHT_CONTROL,
        VK_OPTION, VK_RIGHT_OPTION, VK_COMMAND, VK_RIGHT_COMMAND,
    };
    CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
    for (size_t i = 0; i < sizeof(MODIFIERS) / sizeof(MODIFIERS[0]); i++) {
        if (g_modifier_down[MODIFIERS[i]]) flags |= modifier_flag(MODIFIERS[i]);
    }
    return flags;
}

static void post(CGEventRef event) {
    CGEventSetFlags(event, current_flags());
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

void platform_post_key(uint16_t keycode, bool pressed) {
    if (keycode >= KEY_COUNT) return;
    CGEventRef event = CGEventCreateKeyboardEvent(event_source(), (CGKeyCode)keycode, pressed);
    if (!event) {
        LOG_ERROR("Failed to create keyboard event for keycode 0x%02x", keycode);
        return;
    }
    if (modifier_flag(keycode)) {
        // Modifiers are reported as flag changes, not key down/up
        g_modifier_down[keycode] = pressed;
        CGEventSetType(event, kCGEventFlagsChanged);
    }
    post(event);
}

void platform_post_mouse_button(int button, bool pressed, ScreenPoint position, int click_count) {
    CGEventType type;
    switch (button) {
        case MOUSE_BUTTON_LEFT:
            type = pressed ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
            break;
        case MOUSE_BUTTON_RIGHT:
            type = pressed ? kCGEventRightMouseDown : kCGEventRightMouseUp;
            break;
        case MOUSE_BUTTON_CENTER:
            type = pressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
            break;
        default:
            LOG_WARN("Unknown mouse button: %d", button);
            return;
    }

    CGEventRef event = CGEventCreateMouseEvent(event_source(), type,
                                               CGPointMake(position.x, position.y),
                                               (CGMouseButton)button);
    if (!event) {
        LOG_ERROR("Failed to create mouse button event");
        return;
    }
    // Without a click count, a quick second click is not seen as a double-click
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, click_count);
    post(event);
}

void platform_post_mouse_move(ScreenPoint position, int delta_x, int delta_y, int drag_button) {
    CGEventType type = kCGEventMouseMoved;
    CGMouseButton button = kCGMouseButtonLeft;
    if (drag_button == MOUSE_BUTTON_LEFT) {
        type = kCGEventLeftMouseDragged;
    } else if (drag_button == MOUSE_BUTTON_RIGHT) {
        type = kCGEventRightMouseDragged;
        button = kCGMouseButtonRight;
    } else if (drag_button == MOUSE_BUTTON_CENTER) {
        type = kCGEventOtherMouseDragged;
        button = kCGMouseButtonCenter;
    }

    CGEventRef event = CGEventCreateMouseEvent(event_source(), type,
                                               CGPointMake(position.x, position.y), button);
    if (!event) {
        LOG_ERROR("Failed to create mouse move event");
        return;
    }
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, delta_x);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, delta_y);
    post(event);
}

void platform_post_scroll(int lines) {
    // Line units behave like wheel notches (one hotbar slot in games, a few
    // lines in documents). Positive is wheel up. Posted events are not
    // re-inverted by the "natural scrolling" setting, which applies to
    // physical devices only.
    CGEventRef event = CGEventCreateScrollWheelEvent(event_source(), kCGScrollEventUnitLine,
                                                     1, (int32_t)lines);
    if (!event) {
        LOG_ERROR("Failed to create scroll event");
        return;
    }
    post(event);
}

ScreenPoint platform_cursor_position(void) {
    CGPoint location = CGPointZero;
    CGEventRef event = CGEventCreate(NULL);
    if (event) {
        location = CGEventGetLocation(event);
        CFRelease(event);
    } else {
        LOG_ERROR("Failed to read mouse position");
    }
    return (ScreenPoint){location.x, location.y};
}

void platform_warp_cursor(ScreenPoint position) {
    // Deliberately no CGAssociateMouseAndMouseCursorPosition(true) here: that
    // setting is global, and a game that has captured the mouse relies on it
    // staying off.
    CGWarpMouseCursorPosition(CGPointMake(position.x, position.y));
}

bool platform_point_on_display(ScreenPoint position) {
    uint32_t count = 0;
    return CGGetDisplaysWithPoint(CGPointMake(position.x, position.y), 0, NULL, &count)
               == kCGErrorSuccess && count > 0;
}

void platform_events_cleanup(void) {
    if (g_source) {
        CFRelease(g_source);
        g_source = NULL;
    }
    memset(g_modifier_down, 0, sizeof(g_modifier_down));
}
