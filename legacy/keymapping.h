/*******************************************************************************
 * XBOX CONTROLLER CONFIGURATION FILE
 * 
 * This is the ONLY file you need to edit to customize your controller!
 * 
 * HOW TO USE:
 * 1. Find the section you want to customize below (buttons, sticks, triggers)
 * 2. Change the values using the key code reference at the bottom
 * 3. Save this file
 * 4. Rebuild: make simulator
 * 5. Run: sudo ./simulator
 * 
 * QUICK EXAMPLES:
 * - Change A button from Space to Enter:  key_a = 0x24 (instead of 0x31)
 * - Swap left stick to arrows:  left_stick_mode = STICK_MODE_ARROWS
 * - Make triggers keys instead of mouse clicks:  left_trigger_mode = TRIGGER_MODE_KEY
 * 
 ******************************************************************************/

#ifndef KEYMAPPING_H
#define KEYMAPPING_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * SECTION 1: STICK BEHAVIOR
 * 
 * Choose how each analog stick behaves:
 * - STICK_MODE_WASD:     Use stick as WASD keys (good for movement)
 * - STICK_MODE_ARROWS:   Use stick as arrow keys
 * - STICK_MODE_MOUSE:    Use stick to move mouse cursor (good for camera)
 * - STICK_MODE_DISABLED: Turn off this stick
 ******************************************************************************/
typedef enum {
    STICK_MODE_WASD,
    STICK_MODE_ARROWS,
    STICK_MODE_MOUSE,
    STICK_MODE_DISABLED
} StickMode;

/*******************************************************************************
 * SECTION 2: TRIGGER BEHAVIOR
 * 
 * Choose how triggers behave:
 * - TRIGGER_MODE_MOUSE: Trigger acts as mouse click (LT=left click, RT=right click)
 * - TRIGGER_MODE_KEY:   Trigger acts as keyboard key (set key below)
 * - TRIGGER_MODE_DISABLED: Turn off this trigger
 ******************************************************************************/
typedef enum {
    TRIGGER_MODE_MOUSE,
    TRIGGER_MODE_KEY,
    TRIGGER_MODE_DISABLED
} TriggerMode;

/*******************************************************************************
 * INTERNAL STRUCTURES (Don't modify these, edit the config below instead)
 ******************************************************************************/
typedef struct {
    uint16_t key_a, key_b, key_x, key_y;
    uint16_t key_lb, key_rb;
    uint16_t key_ls, key_rs;
    uint16_t key_view, key_menu;
    uint16_t key_dpad_up, key_dpad_down, key_dpad_left, key_dpad_right;
} ButtonMapping;

typedef struct {
    StickMode left_stick_mode;
    uint16_t left_up, left_down, left_left, left_right;
    
    StickMode right_stick_mode;
    uint16_t right_up, right_down, right_left, right_right;
    
    float mouse_sensitivity;
    float mouse_curve;
    float mouse_smoothing;
    int16_t deadzone;
} StickMapping;

typedef struct {
    TriggerMode left_trigger_mode;
    TriggerMode right_trigger_mode;
    uint16_t left_trigger_key;
    uint16_t right_trigger_key;
    uint8_t threshold;
} TriggerMapping;

typedef struct {
    ButtonMapping buttons;
    StickMapping sticks;
    TriggerMapping triggers;
    bool console_output_enabled;
    bool streaming_mode;
} ControllerMapping;

/*******************************************************************************
 * ============================================================================
 *                    🎮 YOUR CONFIGURATION STARTS HERE 🎮
 * ============================================================================
 * 
 * Edit the values below to customize your controller!
 * Look up key codes in the reference at the bottom of this file.
 * 
 ******************************************************************************/

static inline ControllerMapping get_default_mapping(void) {
    ControllerMapping mapping;
    
    /***************************************************************************
     * BUTTON MAPPINGS
     * 
     * Map each Xbox button to a keyboard key.
     * Change the 0x__ values using the key code reference at the bottom.
     * 
     * Example: To make A button = Enter instead of Space:
     *   Change:  .key_a = 0x31,  (Space)
     *   To:      .key_a = 0x24,  (Enter)
     **************************************************************************/
    
    mapping.buttons.key_a          = 0x31;  // Space
    mapping.buttons.key_b          = 0x08;  // C
    mapping.buttons.key_x          = 0x0F;  // R
    mapping.buttons.key_y          = 0x03;  // F
    
    mapping.buttons.key_lb         = 0x0C;  // Q (Left Bumper)
    mapping.buttons.key_rb         = 0x0E;  // E (Right Bumper)
    
    mapping.buttons.key_ls         = 0x38;  // Left Shift (Left Stick Click)
    mapping.buttons.key_rs         = 0x3B;  // Left Control (Right Stick Click)
    
    mapping.buttons.key_view       = 0x30;  // Tab (View button)
    mapping.buttons.key_menu       = 0x35;  // Escape (Menu button)
    
    mapping.buttons.key_dpad_up    = 0x7E;  // Up Arrow
    mapping.buttons.key_dpad_down  = 0x7D;  // Down Arrow
    mapping.buttons.key_dpad_left  = 0x7B;  // Left Arrow
    mapping.buttons.key_dpad_right = 0x7C;  // Right Arrow
    
    
    /***************************************************************************
     * LEFT STICK CONFIGURATION
     * 
     * Choose behavior mode:
     *   STICK_MODE_WASD    - Use for movement (W=up, A=left, S=down, D=right)
     *   STICK_MODE_ARROWS  - Use arrow keys instead
     *   STICK_MODE_MOUSE   - Move mouse cursor
     *   STICK_MODE_DISABLED - Turn off left stick
     * 
     * If using WASD or ARROWS mode, set the keys below.
     * If using MOUSE mode, keys are ignored.
     **************************************************************************/
    
    mapping.sticks.left_stick_mode = STICK_MODE_WASD;  // ← CHANGE THIS
    
    mapping.sticks.left_up         = 0x0D;  // W
    mapping.sticks.left_down       = 0x01;  // S
    mapping.sticks.left_left       = 0x00;  // A
    mapping.sticks.left_right      = 0x02;  // D
    
    
    /***************************************************************************
     * RIGHT STICK CONFIGURATION
     * 
     * Choose behavior mode (same options as left stick):
     *   STICK_MODE_MOUSE   - Move mouse cursor (recommended for camera)
     *   STICK_MODE_WASD    - Use WASD keys
     *   STICK_MODE_ARROWS  - Use arrow keys
     *   STICK_MODE_DISABLED - Turn off right stick
     * 
     * If using MOUSE mode, adjust sensitivity/smoothing below.
     **************************************************************************/
    
    mapping.sticks.right_stick_mode = STICK_MODE_MOUSE;  // ← CHANGE THIS
    
    mapping.sticks.right_up        = 0x22;  // I (only used if not MOUSE mode)
    mapping.sticks.right_down      = 0x28;  // K
    mapping.sticks.right_left      = 0x26;  // J
    mapping.sticks.right_right     = 0x25;  // L
    
    
    /***************************************************************************
     * MOUSE SETTINGS (for sticks in MOUSE mode)
     * 
     * mouse_sensitivity: How fast the cursor moves
     *   - 0.5 = slow, precise
     *   - 1.5 = default (balanced)
     *   - 3.0 = fast
     * 
     * mouse_curve: Response curve (makes small movements more precise)
     *   - 1.0 = linear (no curve)
     *   - 1.8 = default (recommended)
     *   - 3.0 = very curved (very precise small movements)
     * 
     * mouse_smoothing: How smooth the movement is
     *   - 0.0 = no smoothing (instant response, may be jittery)
     *   - 0.3 = default (balanced)
     *   - 0.8 = very smooth (may feel laggy)
     **************************************************************************/
    
    mapping.sticks.mouse_sensitivity = 1.5;  // ← ADJUST FOR SPEED
    mapping.sticks.mouse_curve       = 1.8;  // ← ADJUST FOR PRECISION
    mapping.sticks.mouse_smoothing   = 0.3;  // ← ADJUST FOR SMOOTHNESS
    
    
    /***************************************************************************
     * DEADZONE (for both sticks)
     * 
     * How much you need to move the stick before it registers.
     * Prevents drift when you leeet go of the stick.
     * 
     * Range: 0 to 32767
     *   - 4000  = small deadzone (~12%)
     *   - 8000  = default (~24%)
     *   - 12000 = large deadzone (~36%)
     **************************************************************************/
    
    mapping.sticks.deadzone = 8000;  // ← ADJUST IF STICK DRIFTS
    
    
    /***************************************************************************
     * TRIGGER CONFIGURATION
     * 
     * Choose behavior mode for each trigger:
     *   TRIGGER_MODE_MOUSE - Act as mouse button (LT=left click, RT=right click)
     *   TRIGGER_MODE_KEY   - Act as keyboard key (set key below)
     *   TRIGGER_MODE_DISABLED - Turn off this trigger
     * 
     * If using KEY mode, set which keys below.
     **************************************************************************/
    
    mapping.triggers.left_trigger_mode  = TRIGGER_MODE_MOUSE;  // ← CHANGE THIS
    mapping.triggers.right_trigger_mode = TRIGGER_MODE_MOUSE;  // ← CHANGE THIS
    
    mapping.triggers.left_trigger_key   = 0x06;  // Z (only used in KEY mode)
    mapping.triggers.right_trigger_key  = 0x07;  // X (only used in KEY mode)
    
    
    /***************************************************************************
     * TRIGGER SENSITIVITY
     * 
     * How far you need to pull the trigger before it activates.
     * 
     * Range: 0 to 255
     *   - 64  = very sensitive (25% pull)
     *   - 127 = default (50% pull)
     *   - 192 = less sensitive (75% pull)
     **************************************************************************/
    
    mapping.triggers.threshold = 127;  // ← ADJUST SENSITIVITY
    
    
    /***************************************************************************
     * ADVANCED SETTINGS
     * 
     * console_output_enabled: Show controller input in terminal?
     *   - true  = See button presses and stick values (good for testing)
     *   - false = Clean output (better for actual gaming)
     * 
     * streaming_mode: Optimize for game streaming (Moonlight/Parsec)?
     *   - false = Local gaming (default)
     *   - true  = Streaming mode (use relative mouse movement)
     **************************************************************************/
    
    mapping.console_output_enabled = true;   // ← Set to false to hide debug output
    mapping.streaming_mode         = false;  // ← Set to true for Moonlight/Parsec
    
    
    return mapping;
}

/*******************************************************************************
 * ============================================================================
 *                      📖 KEY CODE REFERENCE 📖
 * ============================================================================
 * 
 * Use these codes in your configuration above.
 * 
 * EXAMPLE: To make the A button = Enter key
 *   Find "Return" below → see it's 0x24
 *   Change: mapping.buttons.key_a = 0x24;
 * 
 ******************************************************************************/

/*------------------------------------------------------------------------------
  LETTERS (lowercase shown, but produces uppercase when Shift is held)
------------------------------------------------------------------------------*/
/*
    A = 0x00      J = 0x26      S = 0x01
    B = 0x0B      K = 0x28      T = 0x11
    C = 0x08      L = 0x25      U = 0x20
    D = 0x02      M = 0x2E      V = 0x09
    E = 0x0E      N = 0x2D      W = 0x0D
    F = 0x03      O = 0x1F      X = 0x07
    G = 0x05      P = 0x23      Y = 0x10
    H = 0x04      Q = 0x0C      Z = 0x06
    I = 0x22      R = 0x0F
*/

/*------------------------------------------------------------------------------
  NUMBERS (top row of keyboard)
------------------------------------------------------------------------------*/
/*
    1 = 0x12      6 = 0x16
    2 = 0x13      7 = 0x1A
    3 = 0x14      8 = 0x1C
    4 = 0x15      9 = 0x19
    5 = 0x17      0 = 0x1D
*/

/*------------------------------------------------------------------------------
  SPECIAL KEYS (commonly used)
------------------------------------------------------------------------------*/
/*
    Space               = 0x31
    Return (Enter)      = 0x24
    Tab                 = 0x30
    Escape              = 0x35
    Delete (Backspace)  = 0x33
    Forward Delete      = 0x75
*/

/*------------------------------------------------------------------------------
  MODIFIER KEYS (Shift, Control, etc.)
------------------------------------------------------------------------------*/
/*
    Left Shift      = 0x38      Right Shift     = 0x3C
    Left Control    = 0x3B      Right Control   = 0x3E
    Left Option/Alt = 0x3A      Right Option    = 0x3D
    Left Command    = 0x37      Right Command   = 0x36
*/

/*------------------------------------------------------------------------------
  ARROW KEYS
------------------------------------------------------------------------------*/
/*
    Up Arrow    = 0x7E
    Down Arrow  = 0x7D
    Left Arrow  = 0x7B
    Right Arrow = 0x7C
*/

/*------------------------------------------------------------------------------
  FUNCTION KEYS
------------------------------------------------------------------------------*/
/*
    F1  = 0x7A      F7  = 0x62
    F2  = 0x78      F8  = 0x64
    F3  = 0x63      F9  = 0x65
    F4  = 0x76      F10 = 0x6D
    F5  = 0x60      F11 = 0x67
    F6  = 0x61      F12 = 0x6F
*/

/*------------------------------------------------------------------------------
  PUNCTUATION & SYMBOLS (may require Shift for some symbols)
------------------------------------------------------------------------------*/
/*
    Minus/Underscore   = 0x1B      [  = 0x21
    Equals/Plus        = 0x18      ]  = 0x1E
    Left Bracket       = 0x21      \  = 0x2A
    Right Bracket      = 0x1E      ;  = 0x29
    Semicolon/Colon    = 0x29      '  = 0x27
    Quote/Double Quote = 0x27      ,  = 0x2B
    Backslash/Pipe     = 0x2A      .  = 0x2F
    Comma/Less Than    = 0x2B      /  = 0x2C
    Period/Greater Than= 0x2F      `  = 0x32
    Slash/Question     = 0x2C
    Grave/Tilde        = 0x32
*/

/*------------------------------------------------------------------------------
  NUMPAD (if your keyboard has one)
------------------------------------------------------------------------------*/
/*
    Numpad 0 = 0x52      Numpad 7 = 0x59      Numpad Clear   = 0x47
    Numpad 1 = 0x53      Numpad 8 = 0x5B      Numpad Equals  = 0x51
    Numpad 2 = 0x54      Numpad 9 = 0x5C      Numpad Divide  = 0x4B
    Numpad 3 = 0x55      Numpad . = 0x41      Numpad Multiply= 0x43
    Numpad 4 = 0x56      Numpad + = 0x45      Numpad Minus   = 0x4E
    Numpad 5 = 0x57      Numpad - = 0x4E      Numpad Plus    = 0x45
    Numpad 6 = 0x58      Numpad * = 0x43      Numpad Enter   = 0x4C
*/

/*------------------------------------------------------------------------------
  TIPS & TRICKS
------------------------------------------------------------------------------*/
/*
    • Can't find a key? Try it in a text editor and note what appears
    • Most games recognize standard keys (letters, numbers, Space, Shift)
    • Arrow keys good for UI navigation
    • F-keys rarely used in games (safe for custom bindings)
    • Modifier keys (Shift, Control) useful for ability combinations
    
    QUICK PRESETS:
    • FPS games:     WASD movement, Space jump, Shift sprint, C crouch
    • MOBA games:    QWER abilities, Space center camera, Tab scoreboard
    • Racing games:  Arrows steer, Space brake/handbrake
    • Platformers:   Arrows move, Space jump, Shift dash
*/

#endif // KEYMAPPING_H