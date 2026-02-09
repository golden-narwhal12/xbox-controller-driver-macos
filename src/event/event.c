/*******************************************************************************
 * event.c - Event injection implementation (keyboard/mouse)
 ******************************************************************************/

// ApplicationServices includes CoreGraphics and handles include ordering properly
#include <ApplicationServices/ApplicationServices.h>

// Include our headers after system headers
#include "../../include/event.h"
#include "../../include/log.h"

void send_key_event(uint16_t keycode, bool pressed) {
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)keycode, pressed);
    if (event) {
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

    CGEventRef event = CGEventCreateMouseEvent(NULL, eventType, currentPos, cgButton);
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        LOG_DEBUG("Mouse button event: button=%d pressed=%d", button, pressed);
    } else {
        LOG_ERROR("Failed to create mouse button event");
    }
}

void send_mouse_movement(float dx, float dy, bool streaming_mode) {
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    CGPoint currentPos;
    CGEventRef getPos = CGEventCreate(NULL);
    currentPos = CGEventGetLocation(getPos);
    CFRelease(getPos);

    CGEventRef event;

    if (streaming_mode) {
        // Streaming mode: Use delta fields (for Moonlight, Parsec, etc.)
        event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, currentPos, 0);
        if (event) {
            CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, (int64_t)dx);
            CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, (int64_t)dy);
        }
    } else {
        // Local mode: Use absolute positioning (for native macOS apps)
        CGPoint newPos = CGPointMake(currentPos.x + dx, currentPos.y + dy);
        event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, newPos, 0);
    }

    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}
