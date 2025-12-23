// simulator.c
// Xbox Controller to Keyboard/Mouse Simulator
// Builds on phase3_gip_test.c with keyboard/mouse injection
// Compile: make simulator
// Run: sudo ./simulator

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <libusb.h>
#include <ApplicationServices/ApplicationServices.h>
#include "gip.h"
#include "keymapping.h"

#define XBOX_VENDOR_ID  0x045e
#define XBOX_PRODUCT_ID 0x02dd

static int running = 1;
static ControllerMapping config;

// State tracking for keys (prevent redundant events)
typedef struct {
    bool keys[256];           // Track which keys are currently pressed
    bool mouse_left;          // Left mouse button state
    bool mouse_right;         // Right mouse button state
    bool mouse_middle;        // Middle mouse button state
    
    // Previous controller state for change detection
    uint16_t prev_buttons;
    uint8_t prev_left_trigger;
    uint8_t prev_right_trigger;
    int16_t prev_left_stick_x;
    int16_t prev_left_stick_y;
    int16_t prev_right_stick_x;
    int16_t prev_right_stick_y;
    
    // Mouse delta accumulation
    float mouse_dx;
    float mouse_dy;
} InputState;

static InputState input_state = {0};

// ============================================================================
// Event Injection Functions
// ============================================================================

void send_key_event(uint16_t keycode, bool pressed) {
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)keycode, pressed);
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

void send_mouse_button_event(CGMouseButton button, bool pressed) {
    CGPoint currentPos;
    CGEventRef getPos = CGEventCreate(NULL);
    currentPos = CGEventGetLocation(getPos);
    CFRelease(getPos);
    
    CGEventType eventType;
    switch (button) {
        case kCGMouseButtonLeft:
            eventType = pressed ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
            break;
        case kCGMouseButtonRight:
            eventType = pressed ? kCGEventRightMouseDown : kCGEventRightMouseUp;
            break;
        case kCGMouseButtonCenter:
            eventType = pressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
            break;
        default:
            return;
    }
    
    CGEventRef event = CGEventCreateMouseEvent(NULL, eventType, currentPos, button);
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

void send_mouse_movement(float dx, float dy) {
    if (fabs(dx) < 0.1 && fabs(dy) < 0.1) {
        return; // Too small to matter
    }
    
    CGPoint currentPos;
    CGEventRef getPos = CGEventCreate(NULL);
    currentPos = CGEventGetLocation(getPos);
    CFRelease(getPos);
    
    CGPoint newPos = CGPointMake(currentPos.x + dx, currentPos.y + dy);
    
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, newPos, 0);
    if (event) {
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

// ============================================================================
// Input Processing Functions
// ============================================================================

void apply_deadzone(int16_t *x, int16_t *y, int16_t deadzone) {
    float magnitude = sqrtf((float)(*x) * (*x) + (float)(*y) * (*y));
    
    if (magnitude < deadzone) {
        *x = 0;
        *y = 0;
    } else if (magnitude > 32767) {
        // Normalize if outside unit circle
        float scale = 32767.0f / magnitude;
        *x = (int16_t)(*x * scale);
        *y = (int16_t)(*y * scale);
    }
}

void process_buttons(uint16_t buttons) {
    // Check each button for state changes
    struct {
        uint16_t mask;
        uint16_t keycode;
    } button_map[] = {
        {XBOX_BTN_A, config.buttons.key_a},
        {XBOX_BTN_B, config.buttons.key_b},
        {XBOX_BTN_X, config.buttons.key_x},
        {XBOX_BTN_Y, config.buttons.key_y},
        {XBOX_BTN_LB, config.buttons.key_lb},
        {XBOX_BTN_RB, config.buttons.key_rb},
        {XBOX_BTN_LS, config.buttons.key_ls},
        {XBOX_BTN_RS, config.buttons.key_rs},
        {XBOX_BTN_VIEW, config.buttons.key_view},
        {XBOX_BTN_MENU, config.buttons.key_menu},
        {XBOX_BTN_DPAD_UP, config.buttons.key_dpad_up},
        {XBOX_BTN_DPAD_DOWN, config.buttons.key_dpad_down},
        {XBOX_BTN_DPAD_LEFT, config.buttons.key_dpad_left},
        {XBOX_BTN_DPAD_RIGHT, config.buttons.key_dpad_right}
    };
    
    for (int i = 0; i < 14; i++) {
        bool is_pressed = (buttons & button_map[i].mask) != 0;
        bool was_pressed = (input_state.prev_buttons & button_map[i].mask) != 0;
        
        if (is_pressed != was_pressed) {
            send_key_event(button_map[i].keycode, is_pressed);
            input_state.keys[button_map[i].keycode] = is_pressed;
        }
    }
    
    input_state.prev_buttons = buttons;
}

void process_triggers(uint8_t left_trigger, uint8_t right_trigger) {
    // Right trigger (swapped - GIP packet has them reversed)
    bool right_pressed = left_trigger > config.triggers.threshold;
    bool right_was_pressed = input_state.prev_right_trigger > config.triggers.threshold;
    
    if (right_pressed != right_was_pressed) {
        if (config.triggers.right_trigger_mode == TRIGGER_MODE_MOUSE) {
            send_mouse_button_event(kCGMouseButtonRight, right_pressed);
            input_state.mouse_right = right_pressed;
        } else if (config.triggers.right_trigger_mode == TRIGGER_MODE_KEY) {
            send_key_event(config.triggers.right_trigger_key, right_pressed);
            input_state.keys[config.triggers.right_trigger_key] = right_pressed;
        }
    }
    
    // Left trigger (swapped - GIP packet has them reversed)
    bool left_pressed = right_trigger > config.triggers.threshold;
    bool left_was_pressed = input_state.prev_left_trigger > config.triggers.threshold;
    
    if (left_pressed != left_was_pressed) {
        if (config.triggers.left_trigger_mode == TRIGGER_MODE_MOUSE) {
            send_mouse_button_event(kCGMouseButtonLeft, left_pressed);
            input_state.mouse_left = left_pressed;
        } else if (config.triggers.left_trigger_mode == TRIGGER_MODE_KEY) {
            send_key_event(config.triggers.left_trigger_key, left_pressed);
            input_state.keys[config.triggers.left_trigger_key] = left_pressed;
        }
    }
    
    input_state.prev_left_trigger = right_trigger;  // Swapped
    input_state.prev_right_trigger = left_trigger;  // Swapped
}

void process_stick_as_keys(int16_t x, int16_t y, uint16_t key_up, uint16_t key_down, 
                           uint16_t key_left, uint16_t key_right) {
    // Axes are swapped in the controller - swap them back
    // Physical up/down is reported in X, physical left/right is reported in Y
    int16_t temp = x;
    x = y;
    y = temp;
    
    // Normalize to -1.0 to 1.0
    float norm_x = x / 32767.0f;
    float norm_y = y / 32767.0f;
    
    // Determine which directions are active (with threshold)
    bool up = (norm_y > 0.3f);
    bool down = (norm_y < -0.3f);
    bool left = (norm_x < -0.3f);
    bool right = (norm_x > 0.3f);
    
    // Send key events for state changes
    if (up != input_state.keys[key_up]) {
        send_key_event(key_up, up);
        input_state.keys[key_up] = up;
    }
    if (down != input_state.keys[key_down]) {
        send_key_event(key_down, down);
        input_state.keys[key_down] = down;
    }
    if (left != input_state.keys[key_left]) {
        send_key_event(key_left, left);
        input_state.keys[key_left] = left;
    }
    if (right != input_state.keys[key_right]) {
        send_key_event(key_right, right);
        input_state.keys[key_right] = right;
    }
}

void process_stick_as_mouse(int16_t x, int16_t y) {
    // Axes are swapped in the controller - swap them back
    // Physical up/down is reported in X, physical left/right is reported in Y
    int16_t temp = x;
    x = y;
    y = temp;
    
    // Normalize to -1.0 to 1.0
    float norm_x = x / 32767.0f;
    float norm_y = -y / 32767.0f;  // Invert Y - pushing up should move cursor up
    
    // Apply exponential curve for better control
    float sign_x = (norm_x >= 0) ? 1.0f : -1.0f;
    float sign_y = (norm_y >= 0) ? 1.0f : -1.0f;
    
    float curved_x = sign_x * powf(fabsf(norm_x), config.sticks.mouse_curve);
    float curved_y = sign_y * powf(fabsf(norm_y), config.sticks.mouse_curve);
    
    // Scale by sensitivity
    float dx = curved_x * config.sticks.mouse_sensitivity * 15.0f;
    float dy = curved_y * config.sticks.mouse_sensitivity * 15.0f;
    
    // Accumulate deltas (sent in main loop)
    input_state.mouse_dx += dx;
    input_state.mouse_dy += dy;
}

void process_sticks(int16_t left_x, int16_t left_y, int16_t right_x, int16_t right_y) {
    // Apply deadzones
    apply_deadzone(&left_x, &left_y, config.sticks.deadzone);
    apply_deadzone(&right_x, &right_y, config.sticks.deadzone);
    
    // Process left stick
    switch (config.sticks.left_stick_mode) {
        case STICK_MODE_WASD:
            process_stick_as_keys(left_x, left_y, 
                                 config.sticks.left_up, config.sticks.left_down,
                                 config.sticks.left_left, config.sticks.left_right);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(left_x, left_y, 0x7E, 0x7D, 0x7B, 0x7C);
            break;
        case STICK_MODE_MOUSE:
            process_stick_as_mouse(left_x, left_y);
            break;
        case STICK_MODE_DISABLED:
        default:
            break;
    }
    
    // Process right stick
    switch (config.sticks.right_stick_mode) {
        case STICK_MODE_WASD:
            process_stick_as_keys(right_x, right_y, 
                                 config.sticks.left_up, config.sticks.left_down,
                                 config.sticks.left_left, config.sticks.left_right);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(right_x, right_y, 0x7E, 0x7D, 0x7B, 0x7C);
            break;
        case STICK_MODE_MOUSE:
            process_stick_as_mouse(right_x, right_y);
            break;
        case STICK_MODE_DISABLED:
        default:
            break;
    }
    
    // Always send accumulated mouse movement if any exists (no minimum threshold)
    if (input_state.mouse_dx != 0.0f || input_state.mouse_dy != 0.0f) {
        send_mouse_movement(input_state.mouse_dx, input_state.mouse_dy);
        input_state.mouse_dx = 0.0f;
        input_state.mouse_dy = 0.0f;
    }
    
    input_state.prev_left_stick_x = left_x;
    input_state.prev_left_stick_y = left_y;
    input_state.prev_right_stick_x = right_x;
    input_state.prev_right_stick_y = right_y;
}

// ============================================================================
// GIP Protocol Functions (from phase3)
// ============================================================================

void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nShutting down...\n");
}

int send_ack(libusb_device_handle *handle, uint8_t out_endpoint, uint8_t sequence) {
    uint8_t ack_packet[] = {
        GIP_CMD_ACKNOWLEDGE, 0x20, sequence, 0x09,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    int transferred;
    int result = libusb_interrupt_transfer(handle, out_endpoint, ack_packet,
                                          sizeof(ack_packet), &transferred, 1000);
    
    if (result == 0 && config.console_output_enabled) {
        printf("  → Sent ACK (seq=%d)\n", sequence);
    }
    return (result == 0) ? 0 : -1;
}

int initialize_controller(libusb_device_handle *handle, uint8_t in_endpoint, 
                         uint8_t out_endpoint) {
    uint8_t buffer[64];
    int transferred;
    int result;
    
    if (config.console_output_enabled) {
        printf("\n=== Initializing Controller ===\n");
        printf("Performing GIP handshake...\n\n");
    }
    
    for (int attempt = 0; attempt < 5; attempt++) {
        result = libusb_interrupt_transfer(handle, in_endpoint, buffer,
                                          sizeof(buffer), &transferred, 2000);
        
        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            GipHeader *header = (GipHeader *)buffer;
            
            if (config.console_output_enabled) {
                printf("  Received: %s (0x%02x), seq=%d\n",
                       gip_command_name(header->command), header->command, header->sequence);
            }
            
            if (header->command == GIP_CMD_ANNOUNCE) {
                send_ack(handle, out_endpoint, header->sequence);
            }
        } else if (result == LIBUSB_ERROR_TIMEOUT) {
            break;
        }
    }
    
    if (config.console_output_enabled) {
        printf("\n✅ Initialization complete!\n");
        printf("Sending POWER ON command...\n");
    }
    
    uint8_t power_on[] = {GIP_CMD_POWER, 0x20, 0x00, 0x01, 0x00};
    result = libusb_interrupt_transfer(handle, out_endpoint, power_on,
                                      sizeof(power_on), &transferred, 1000);
    
    if (result == 0 && config.console_output_enabled) {
        printf("✅ Controller powered on!\n\n");
    }
    
    usleep(500000);
    return 0;
}

void input_loop(libusb_device_handle *handle, uint8_t in_endpoint) {
    uint8_t buffer[64];
    int transferred;
    int result;
    int input_count = 0;
    
    printf("=== Xbox Controller Simulator Active ===\n");
    printf("Controller input is now being translated to keyboard/mouse\n");
    if (config.console_output_enabled) {
        printf("Console output: ENABLED (see input below)\n");
    } else {
        printf("Console output: DISABLED\n");
    }
    printf("Press Ctrl+C to exit\n\n");
    
    while (running) {
        result = libusb_interrupt_transfer(handle, in_endpoint, buffer,
                                          sizeof(buffer), &transferred, 10);  // 10ms timeout for smoother mouse
        
        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            GipHeader *header = (GipHeader *)buffer;
            
            if (header->command == GIP_CMD_INPUT && 
                transferred >= (int)sizeof(GipInputPacket)) {
                GipInputPacket *input = (GipInputPacket *)buffer;
                input_count++;
                
                // Process and inject input events
                process_buttons(input->buttons);
                process_triggers(input->left_trigger, input->right_trigger);
                process_sticks(input->left_stick_x, input->left_stick_y,
                             input->right_stick_x, input->right_stick_y);
                
                // Console output (if enabled)
                if (config.console_output_enabled) {
                    printf("\r[%04d] ", input_count);
                    printf("BTN: ");
                    if (input->buttons) {
                        print_buttons(input->buttons);
                    } else {
                        printf("none ");
                    }
                    printf("%-40s", "");
                    printf("\r[%04d] BTN: ", input_count);
                    print_buttons(input->buttons);
                    printf("| LT:%3d RT:%3d ", input->left_trigger, input->right_trigger);
                    printf("| LS:(%6d,%6d) RS:(%6d,%6d)  ",
                           input->left_stick_x, input->left_stick_y,
                           input->right_stick_x, input->right_stick_y);
                    fflush(stdout);
                }
                
            } else if (header->command == GIP_CMD_GUIDE_BUTTON && 
                      config.console_output_enabled) {
                printf("\n🎮 GUIDE BUTTON PRESSED\n");
            }
            
        } else if (result != LIBUSB_ERROR_TIMEOUT) {
            if (result == LIBUSB_ERROR_NO_DEVICE) {
                printf("\n❌ Controller disconnected!\n");
                break;
            }
        }
    }
    
    printf("\n\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int result;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Xbox Controller to Keyboard/Mouse Simulator\n");
    printf("============================================\n\n");
    
    // Load configuration
    config = get_default_mapping();
    
    printf("Configuration loaded:\n");
    printf("  Left stick: %s\n", 
           config.sticks.left_stick_mode == STICK_MODE_WASD ? "WASD" :
           config.sticks.left_stick_mode == STICK_MODE_ARROWS ? "Arrows" :
           config.sticks.left_stick_mode == STICK_MODE_MOUSE ? "Mouse" : "Disabled");
    printf("  Right stick: %s\n",
           config.sticks.right_stick_mode == STICK_MODE_WASD ? "WASD" :
           config.sticks.right_stick_mode == STICK_MODE_ARROWS ? "Arrows" :
           config.sticks.right_stick_mode == STICK_MODE_MOUSE ? "Mouse" : "Disabled");
    printf("  Left trigger: %s\n",
           config.triggers.left_trigger_mode == TRIGGER_MODE_MOUSE ? "Mouse Left" :
           config.triggers.left_trigger_mode == TRIGGER_MODE_KEY ? "Key" : "Disabled");
    printf("  Right trigger: %s\n",
           config.triggers.right_trigger_mode == TRIGGER_MODE_MOUSE ? "Mouse Right" :
           config.triggers.right_trigger_mode == TRIGGER_MODE_KEY ? "Key" : "Disabled");
    printf("  Deadzone: %d (%.1f%%)\n", config.sticks.deadzone,
           (config.sticks.deadzone / 32767.0f) * 100.0f);
    printf("\n");
    
    printf("⚠️  IMPORTANT: You may need to grant Accessibility permissions:\n");
    printf("   System Settings → Privacy & Security → Accessibility\n");
    printf("   Add Terminal (or your terminal app) to the list\n\n");
    
    // Initialize libusb
    result = libusb_init(&ctx);
    if (result < 0) {
        printf("❌ Failed to initialize libusb: %s\n", libusb_error_name(result));
        return 1;
    }
    
    // Find controller
    printf("Looking for Xbox controller...\n");
    handle = libusb_open_device_with_vid_pid(ctx, XBOX_VENDOR_ID, XBOX_PRODUCT_ID);
    if (!handle) {
        printf("❌ Controller not found\n");
        printf("   Make sure it's plugged in and you're running with sudo\n");
        libusb_exit(ctx);
        return 1;
    }
    printf("✅ Found controller\n");
    
    // Detach kernel driver
    if (libusb_kernel_driver_active(handle, 0) == 1) {
        libusb_detach_kernel_driver(handle, 0);
    }
    
    // Claim interface
    result = libusb_claim_interface(handle, 0);
    if (result < 0) {
        printf("❌ Failed to claim interface: %s\n", libusb_error_name(result));
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    printf("✅ Claimed interface\n");
    
    // Get endpoints
    struct libusb_config_descriptor *desc;
    libusb_get_active_config_descriptor(libusb_get_device(handle), &desc);
    
    uint8_t in_endpoint = 0;
    uint8_t out_endpoint = 0;
    
    const struct libusb_interface *inter = &desc->interface[0];
    const struct libusb_interface_descriptor *interdesc = &inter->altsetting[0];
    
    for (int i = 0; i < interdesc->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *ep = &interdesc->endpoint[i];
        if ((ep->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                in_endpoint = ep->bEndpointAddress;
            } else {
                out_endpoint = ep->bEndpointAddress;
            }
        }
    }
    
    libusb_free_config_descriptor(desc);
    
    if (in_endpoint == 0 || out_endpoint == 0) {
        printf("❌ Could not find interrupt endpoints\n");
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    
    // Initialize controller
    initialize_controller(handle, in_endpoint, out_endpoint);
    
    // Run simulator
    input_loop(handle, in_endpoint);
    
    // Cleanup - release all keys
    printf("Releasing all keys...\n");
    for (int i = 0; i < 256; i++) {
        if (input_state.keys[i]) {
            send_key_event(i, false);
        }
    }
    if (input_state.mouse_left) {
        send_mouse_button_event(kCGMouseButtonLeft, false);
    }
    if (input_state.mouse_right) {
        send_mouse_button_event(kCGMouseButtonRight, false);
    }
    
    printf("Cleaning up...\n");
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    
    printf("\n✅ Simulator stopped cleanly!\n");
    return 0;
}