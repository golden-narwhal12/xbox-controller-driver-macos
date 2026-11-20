/*******************************************************************************
 * types.h - Shared constants, configuration and state types
 ******************************************************************************/

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Log Level
 ******************************************************************************/
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LogLevel;

/*******************************************************************************
 * Geometry. All points are global display coordinates in points with a
 * top-left origin (the convention CGEvent and the Accessibility API share).
 * Displays left of or above the main display have negative coordinates.
 ******************************************************************************/
typedef struct { float x, y; } Vec2;
typedef struct { double x, y; } ScreenPoint;
typedef struct { double x, y, w, h; } ScreenRect;

/*******************************************************************************
 * Input sources: every physical control that can hold a binding.
 * ControllerMapping.bindings[] and InputState.source_*[] are indexed by these.
 ******************************************************************************/
enum {
    SOURCE_BTN_A = 0,
    SOURCE_BTN_B,
    SOURCE_BTN_X,
    SOURCE_BTN_Y,
    SOURCE_BTN_LB,
    SOURCE_BTN_RB,
    SOURCE_BTN_LS,
    SOURCE_BTN_RS,
    SOURCE_BTN_VIEW,
    SOURCE_BTN_MENU,
    SOURCE_BTN_DPAD_UP,
    SOURCE_BTN_DPAD_DOWN,
    SOURCE_BTN_DPAD_LEFT,
    SOURCE_BTN_DPAD_RIGHT,
    SOURCE_BUTTON_COUNT,

    SOURCE_LEFT_TRIGGER  = SOURCE_BUTTON_COUNT,
    SOURCE_RIGHT_TRIGGER,
    SOURCE_LEFT_STICK,                                   // + STICK_DIR_*
    SOURCE_RIGHT_STICK   = SOURCE_LEFT_STICK + 4,        // + STICK_DIR_*
    SOURCE_COUNT         = SOURCE_RIGHT_STICK + 4
};

enum { STICK_LEFT = 0, STICK_RIGHT, STICK_COUNT };
enum { STICK_DIR_UP = 0, STICK_DIR_DOWN, STICK_DIR_LEFT, STICK_DIR_RIGHT, STICK_DIR_COUNT };
enum { TRIGGER_LEFT = 0, TRIGGER_RIGHT, TRIGGER_COUNT };

/*******************************************************************************
 * Bindings: a macOS virtual keycode (0-255) or one of the values below
 ******************************************************************************/
#define BINDING_NONE           0xFFFF  // Unbound / disabled / unknown key name
#define BINDING_MOUSE_LEFT     0xFFFE
#define BINDING_MOUSE_RIGHT    0xFFFD
#define BINDING_MOUSE_MIDDLE   0xFFFC
#define BINDING_SCROLL_DOWN    0xFFFB  // Wheel down: view moves toward the end of
                                       // the document (next hotbar slot in games)
#define BINDING_SCROLL_UP      0xFFFA  // Wheel up: view moves toward the top

#define MOUSE_BUTTON_LEFT      0       // Values match CGMouseButton
#define MOUSE_BUTTON_RIGHT     1
#define MOUSE_BUTTON_CENTER    2
#define MOUSE_BUTTON_COUNT     3
#define MOUSE_BUTTON_NONE      (-1)

/* macOS virtual keycodes used by the built-in defaults */
#define KEY_A             0x00
#define KEY_S             0x01
#define KEY_D             0x02
#define KEY_F             0x03
#define KEY_C             0x08
#define KEY_W             0x0D
#define KEY_R             0x0F
#define KEY_I             0x22
#define KEY_L             0x25
#define KEY_J             0x26
#define KEY_K             0x28
#define KEY_TAB           0x30
#define KEY_SPACE         0x31
#define KEY_ESCAPE        0x35
#define KEY_LEFT_SHIFT    0x38
#define KEY_LEFT_CONTROL  0x3B
#define KEY_LEFT_ARROW    0x7B
#define KEY_RIGHT_ARROW   0x7C
#define KEY_DOWN_ARROW    0x7D
#define KEY_UP_ARROW      0x7E
#define KEY_COUNT         256

/*******************************************************************************
 * Input ranges and defaults
 ******************************************************************************/
#define STICK_MAX                  32767   // Full tilt on one axis
#define TRIGGER_RAW_MAX            1023    // GIP triggers are 10-bit
#define TRIGGER_CONFIG_MAX         255     // Config scale for trigger thresholds

#define DEFAULT_DEADZONE           8000
#define MAX_DEADZONE               30000
#define DEFAULT_SENSITIVITY        1.5f
#define DEFAULT_CURVE              1.8f
#define DEFAULT_SMOOTHING          0.3f
#define MAX_SMOOTHING              0.95f   // 1.0 would freeze the cursor
#define DEFAULT_TRIGGER_THRESHOLD  127     // Midpoint: press ~55%, release ~45%
#define DEFAULT_TRIGGER_HYSTERESIS 26      // Width of the press/release band
#define DEFAULT_STICK_THRESHOLD    0.30f   // Stick-as-keys press point (0-1)
#define DEFAULT_STICK_HYSTERESIS   0.05f   // Release at threshold - hysteresis
#define DEFAULT_PWM_PERIOD_MS      100.0f
#define MIN_PWM_PERIOD_MS          30.0f
#define MAX_PWM_PERIOD_MS          500.0f
#define DEFAULT_PWM_FULL_ON        0.90f
#define DEFAULT_PWM_MIN_PULSE_MS   20.0f
#define MIN_PWM_MIN_PULSE_MS       5.0f
#define DEFAULT_CONFINE_MARGIN     12.0f   // Points inside the window frame: clear of
                                           // the resize border and of display edges
#define DEFAULT_CONFINE_TOP_INSET  28.0f   // Title bar height kept out of reach
#define DEFAULT_RUMBLE_INTENSITY   100
#define DEFAULT_RUMBLE_DURATION_MS 50

/*******************************************************************************
 * Motion timing
 ******************************************************************************/
#define MOTION_TICK_MS         10.0f   // Nominal motion tick; gains are per tick
#define MOTION_TICK_US         10000u
#define MOTION_MAX_DT_MS       50.0f   // Longest elapsed time applied in one tick
#define MOUSE_PX_PER_TICK      15.0f   // Cursor points per nominal tick at full
                                       // tilt, sensitivity 1 (1500 pt/s)
#define SCROLL_REPEAT_DELAY_MS 300.0f  // Hold this long before repeating
#define SCROLL_REPEAT_MS       80.0f   // Then one notch per interval while held

/*******************************************************************************
 * Configuration
 ******************************************************************************/
typedef enum {
    STICK_MODE_WASD = 0,   // Four keys from bindings[] (W/A/S/D by default)
    STICK_MODE_ARROWS,     // The four arrow keys
    STICK_MODE_MOUSE,
    STICK_MODE_DISABLED
} StickMode;

typedef struct {
    StickMode mode;
    bool pwm;                  // Key modes only: pulse keys in proportion to tilt
    int16_t deadzone;          // Raw units (0..MAX_DEADZONE), radial
    float threshold;           // Key press point on the post-deadzone 0-1 scale
    float hysteresis;          // Key releases at threshold - hysteresis
    float pwm_period_ms;
    float pwm_full_on;         // At or above this the key is held continuously
    float pwm_min_pulse_ms;    // Shortest key-down or key-up time ever emitted
} StickConfig;

typedef struct {
    float sensitivity;
    float curve;
    float smoothing;           // Fraction of the previous value kept per tick
} MouseConfig;

typedef struct {
    uint8_t threshold;         // Midpoint of the press/release band (0-255)
    uint8_t hysteresis;        // Band width: press above +half, release below -half
} TriggerConfig;

typedef struct {
    bool enabled;
    bool button_feedback;
    uint8_t intensity;
    uint16_t duration_ms;
} RumbleConfig;

typedef enum {
    CONFINE_WINDOW = 0,        // Focused window of the frontmost app
    CONFINE_DISPLAY,           // Any display (pre-overhaul behavior)
    CONFINE_OFF
} ConfineMode;

typedef struct {
    ConfineMode confine;
    float margin;                  // Inset from the window frame, points (all windows)
    float top_inset;               // Extra top inset (title bar), windowed only
    bool recenter_when_captured;   // Warp to the window center at an edge
    bool streaming_mode;           // Post deltas only; never move the cursor
} CursorConfig;

typedef struct {
    /* Button and trigger bindings, plus each stick's four WASD-mode keys.
       BINDING_NONE disables a source. */
    uint16_t bindings[SOURCE_COUNT];
    StickConfig stick[STICK_COUNT];
    MouseConfig mouse;
    TriggerConfig triggers;
    RumbleConfig rumble;
    CursorConfig cursor;
    bool console_output_enabled;
    LogLevel log_level;
} ControllerMapping;

/*******************************************************************************
 * Output events. The input layer never touches the OS: it appends the events
 * it wants to a queue, and the caller posts them (cursor_dispatch).
 ******************************************************************************/
typedef enum { OUT_KEY, OUT_MOUSE_BUTTON, OUT_MOUSE_MOVE, OUT_SCROLL } OutEventType;

typedef struct {
    OutEventType type;
    uint16_t code;        // OUT_KEY: keycode. OUT_MOUSE_BUTTON: MOUSE_BUTTON_*
    bool pressed;         // OUT_KEY, OUT_MOUSE_BUTTON
    int lines;            // OUT_SCROLL: wheel notches, positive = wheel up
    float dx, dy;         // OUT_MOUSE_MOVE: requested motion in points
    int drag_button;      // OUT_MOUSE_MOVE: held button, or MOUSE_BUTTON_NONE
} OutEvent;

/* One call into the input layer changes each source at most twice (release the
   old binding, press the new one), so 4 * SOURCE_COUNT can never overflow. */
#define OUT_QUEUE_CAPACITY 128

typedef struct {
    OutEvent events[OUT_QUEUE_CAPACITY];
    int count;
    int dropped;          // Always 0 by construction; checked by tests
} OutQueue;

_Static_assert(OUT_QUEUE_CAPACITY >= 4 * SOURCE_COUNT, "output queue too small");

/*******************************************************************************
 * Input state
 ******************************************************************************/
typedef struct {
    bool running;
    float debt_ms;           // Key-down time owed (+) or overpaid (-) so far
    float plan_ms;           // Key-down time planned for the current period
    float on_ms;             // Key-down time spent in the current period
    float since_change_ms;   // Time since this key last changed
} PwmAxis;

typedef struct {
    bool running;
    float phase_ms;          // Position in the current period, shared by all
                             // four directions so diagonals pulse together
    PwmAxis dir[STICK_DIR_COUNT];
} PwmStick;

typedef struct {
    /* One physical control owns each source; shared keys/buttons are
       reference counted so they stay held until the last source lets go. */
    uint16_t source_binding[SOURCE_COUNT];
    bool source_pressed[SOURCE_COUNT];
    uint8_t key_refs[KEY_COUNT];
    uint8_t mouse_refs[MOUSE_BUTTON_COUNT];

    /* Scroll bindings: [0] = up, [1] = down. One notch on press, then repeat. */
    uint8_t scroll_refs[2];
    float scroll_wait_ms[2];                 // Time until the next repeat

    Vec2 stick[STICK_COUNT];                 // After deadzone, -1..1, +Y down
    Vec2 smoothed[STICK_COUNT];              // Mouse-mode filter state
    bool dir_active[STICK_COUNT][STICK_DIR_COUNT];   // Key threshold, with hysteresis
    PwmStick pwm[STICK_COUNT];
} InputState;

#endif // TYPES_H
