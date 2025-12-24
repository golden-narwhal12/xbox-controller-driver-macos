// keymapping.h
// Xbox Controller to Keyboard/Mouse Mapping Configuration
// 
// CUSTOMIZATION: Edit the values in this file to change your controller bindings
// Key codes are from Carbon/HIToolbox (standard macOS key codes)

#ifndef KEYMAPPING_H
#define KEYMAPPING_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CUSTOMIZATION SECTION - Edit these values to change your bindings!
// ============================================================================

// Button to Key Mappings
// Key codes: https://eastmanreference.com/complete-list-of-applescript-key-codes
typedef struct {
    uint16_t key_a;              // Default: Space (0x31)
    uint16_t key_b;              // Default: C (0x08)
    uint16_t key_x;              // Default: R (0x0F)
    uint16_t key_y;              // Default: F (0x03)
    uint16_t key_lb;             // Default: Q (0x0C)
    uint16_t key_rb;             // Default: E (0x0E)
    uint16_t key_ls;             // Default: Left Shift (0x38)
    uint16_t key_rs;             // Default: Left Ctrl (0x3B)
    uint16_t key_view;           // Default: Tab (0x30)
    uint16_t key_menu;           // Default: Escape (0x35)
    uint16_t key_dpad_up;        // Default: Up Arrow (0x7E)
    uint16_t key_dpad_down;      // Default: Down Arrow (0x7D)
    uint16_t key_dpad_left;      // Default: Left Arrow (0x7B)
    uint16_t key_dpad_right;     // Default: Right Arrow (0x7C)
} ButtonMapping;

// Stick behavior modes
typedef enum {
    STICK_MODE_WASD,        // Convert to WASD keys
    STICK_MODE_ARROWS,      // Convert to arrow keys
    STICK_MODE_MOUSE,       // Convert to mouse movement
    STICK_MODE_DISABLED     // Ignore this stick
} StickMode;

// Trigger behavior modes
typedef enum {
    TRIGGER_MODE_KEY,       // Act as a key press
    TRIGGER_MODE_MOUSE,     // Act as mouse button
    TRIGGER_MODE_DISABLED   // Ignore this trigger
} TriggerMode;

// Stick to Key/Mouse Mappings
typedef struct {
    // Left stick configuration
    StickMode left_stick_mode;   // Default: STICK_MODE_WASD
    uint16_t left_up;            // Default: W (0x0D)
    uint16_t left_down;          // Default: S (0x01)
    uint16_t left_left;          // Default: A (0x00)
    uint16_t left_right;         // Default: D (0x02)
    
    // Right stick configuration
    StickMode right_stick_mode;  // Default: STICK_MODE_MOUSE
    uint16_t right_up;           // Default: I (0x22) - only used if not MOUSE mode
    uint16_t right_down;         // Default: K (0x28)
    uint16_t right_left;         // Default: J (0x26)
    uint16_t right_right;        // Default: L (0x25)
    
    // Mouse sensitivity (only for MOUSE mode)
    float mouse_sensitivity;     // Default: 1.5
    float mouse_curve;           // Default: 1.8 (exponential curve, 1.0=linear)
    float mouse_smoothing;       // Default: 0.3 (0.0=no smoothing, 0.9=max smoothing)
    
    // Deadzone (0-32767, default: 8000 = ~24%)
    int16_t deadzone;
} StickMapping;

// Trigger Mappings
typedef struct {
    TriggerMode left_trigger_mode;    // Default: TRIGGER_MODE_MOUSE (left click)
    TriggerMode right_trigger_mode;   // Default: TRIGGER_MODE_MOUSE (right click)
    
    uint16_t left_trigger_key;        // Default: Z (0x06) - only used in KEY mode
    uint16_t right_trigger_key;       // Default: X (0x07) - only used in KEY mode
    
    uint8_t threshold;                // Default: 127 (50%)
} TriggerMapping;

// Complete mapping configuration
typedef struct {
    ButtonMapping buttons;
    StickMapping sticks;
    TriggerMapping triggers;
    
    bool console_output_enabled;      // Default: true (show input in console)
    bool streaming_mode;              // Default: false (true for Moonlight/Parsec)
} ControllerMapping;

// ============================================================================
// DEFAULT CONFIGURATION
// ============================================================================

static inline ControllerMapping get_default_mapping(void) {
    ControllerMapping mapping = {
        // Button mappings
        .buttons = {
            .key_a = 0x31,           // Space
            .key_b = 0x08,           // C
            .key_x = 0x0F,           // R
            .key_y = 0x03,           // F
            .key_lb = 0x0C,          // Q
            .key_rb = 0x0E,          // E
            .key_ls = 0x38,          // Left Shift
            .key_rs = 0x3B,          // Left Control
            .key_view = 0x30,        // Tab
            .key_menu = 0x35,        // Escape
            .key_dpad_up = 0x7E,     // Up Arrow
            .key_dpad_down = 0x7D,   // Down Arrow
            .key_dpad_left = 0x7B,   // Left Arrow
            .key_dpad_right = 0x7C   // Right Arrow
        },
        
        // Stick mappings
        .sticks = {
            .left_stick_mode = STICK_MODE_WASD,
            .left_up = 0x0D,         // W
            .left_down = 0x01,       // S
            .left_left = 0x00,       // A
            .left_right = 0x02,      // D
            
            .right_stick_mode = STICK_MODE_MOUSE,
            .right_up = 0x22,        // I (if not in mouse mode)
            .right_down = 0x28,      // K
            .right_left = 0x26,      // J
            .right_right = 0x25,     // L
            
            .mouse_sensitivity = 1.5,
            .mouse_curve = 1.8,
            .mouse_smoothing = 0.3,
            .deadzone = 8000
        },
        
        // Trigger mappings
        .triggers = {
            .left_trigger_mode = TRIGGER_MODE_MOUSE,
            .right_trigger_mode = TRIGGER_MODE_MOUSE,
            .left_trigger_key = 0x06,    // Z
            .right_trigger_key = 0x07,   // X
            .threshold = 127
        },
        
        .console_output_enabled = true,
        .streaming_mode = false
    };
    
    return mapping;
}

// ============================================================================
// COMMON KEY CODES REFERENCE
// ============================================================================
/*
Letters:
  A=0x00  B=0x0B  C=0x08  D=0x02  E=0x0E  F=0x03  G=0x05  H=0x04
  I=0x22  J=0x26  K=0x28  L=0x25  M=0x2E  N=0x2D  O=0x1F  P=0x23
  Q=0x0C  R=0x0F  S=0x01  T=0x11  U=0x20  V=0x09  W=0x0D  X=0x07
  Y=0x10  Z=0x06

Numbers:
  0=0x1D  1=0x12  2=0x13  3=0x14  4=0x15  5=0x17
  6=0x16  7=0x1A  8=0x1C  9=0x19

Special Keys:
  Space=0x31       Return=0x24      Tab=0x30         Escape=0x35
  Delete=0x33      Forward Delete=0x75
  
Modifiers:
  Shift=0x38       Control=0x3B     Option=0x3A      Command=0x37
  Right Shift=0x3C Right Control=0x3E Right Option=0x3D

Arrows:
  Up=0x7E          Down=0x7D        Left=0x7B        Right=0x7C

Function Keys:
  F1=0x7A   F2=0x78   F3=0x63   F4=0x76   F5=0x60   F6=0x61
  F7=0x62   F8=0x64   F9=0x65   F10=0x6D  F11=0x67  F12=0x6F
*/

#endif // KEYMAPPING_H