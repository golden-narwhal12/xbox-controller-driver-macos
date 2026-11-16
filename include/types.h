/*******************************************************************************
 * types.h - Common type definitions and enums for Xbox Controller Driver
 ******************************************************************************/

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

/*******************************************************************************
 * Stick Mode Enum
 ******************************************************************************/
typedef enum {
    STICK_MODE_WASD = 0,
    STICK_MODE_ARROWS,
    STICK_MODE_MOUSE,
    STICK_MODE_DISABLED
} StickMode;

/*******************************************************************************
 * Trigger Mode Enum
 ******************************************************************************/
typedef enum {
    TRIGGER_MODE_MOUSE = 0,
    TRIGGER_MODE_KEY,
    TRIGGER_MODE_DISABLED
} TriggerMode;

/*******************************************************************************
 * Log Level Enum
 ******************************************************************************/
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LogLevel;

/*******************************************************************************
 * Button Bit Masks
 ******************************************************************************/
#define XBOX_BTN_SYNC          0x0001
#define XBOX_BTN_DUMMY1        0x0002
#define XBOX_BTN_MENU          0x0004
#define XBOX_BTN_VIEW          0x0008
#define XBOX_BTN_A             0x0010
#define XBOX_BTN_B             0x0020
#define XBOX_BTN_X             0x0040
#define XBOX_BTN_Y             0x0080
#define XBOX_BTN_DPAD_UP       0x0100
#define XBOX_BTN_DPAD_DOWN     0x0200
#define XBOX_BTN_DPAD_LEFT     0x0400
#define XBOX_BTN_DPAD_RIGHT    0x0800
#define XBOX_BTN_LB            0x1000
#define XBOX_BTN_RB            0x2000
#define XBOX_BTN_LS            0x4000
#define XBOX_BTN_RS            0x8000

#define XBOX_BTN_COUNT         14

/*******************************************************************************
 * Input Sources: every physical control that can hold a binding
 ******************************************************************************/
enum {
    SOURCE_FIRST_BUTTON  = 0,                       // button i is source i
    SOURCE_LEFT_TRIGGER  = XBOX_BTN_COUNT,
    SOURCE_RIGHT_TRIGGER,
    SOURCE_LEFT_STICK,                              // up, down, left, right
    SOURCE_RIGHT_STICK   = SOURCE_LEFT_STICK + 4,   // up, down, left, right
    SOURCE_COUNT         = SOURCE_RIGHT_STICK + 4
};

/*******************************************************************************
 * USB Constants
 ******************************************************************************/
/* Supported controllers are listed in SUPPORTED_DEVICES[] (src/usb/usb.c) */
#define USB_BUFFER_SIZE        64
#define USB_TIMEOUT_MS         10
#define USB_INIT_TIMEOUT_MS    2000
#define USB_ACK_TIMEOUT_MS     1000
#define USB_OUTPUT_TIMEOUT_MS  100

/*******************************************************************************
 * Input Constants
 ******************************************************************************/
#define STICK_MAX              32767
#define STICK_MIN             -32768
#define TRIGGER_MAX            255
#define DEFAULT_DEADZONE       8000
#define DEFAULT_SENSITIVITY    1.5f
#define DEFAULT_CURVE          1.8f
#define DEFAULT_SMOOTHING      0.3f
#define DEFAULT_THRESHOLD      127

/*******************************************************************************
 * Motion Timing
 ******************************************************************************/
#define MOTION_TICK_MS         10.0f   // Nominal mouse-motion tick; gains are per tick
#define MOTION_MAX_DT_MS       50.0f   // Longest elapsed time applied in one tick
#define MOUSE_PX_PER_TICK      15.0f   // Cursor pixels per nominal tick at full tilt,
                                       // sensitivity 1 (1500 px/s)

/*******************************************************************************
 * Mouse and Scroll Bindings (buttons/triggers can bind to these like keys)
 ******************************************************************************/
#define BINDING_MOUSE_MIDDLE   0xFFFC
#define BINDING_MOUSE_RIGHT    0xFFFD
#define BINDING_MOUSE_LEFT     0xFFFE
#define BINDING_SCROLL_UP      0xFFFA  // Content moves up (classic wheel up)
#define BINDING_SCROLL_DOWN    0xFFFB  // Content moves down (two-finger swipe up
                                       // with macOS natural scrolling)
#define SCROLL_REPEAT_DELAY_MS 300.0f  // Hold this long before repeating
#define SCROLL_REPEAT_MS       80.0f   // Then one notch per interval while held

/*******************************************************************************
 * Feature Constants
 ******************************************************************************/
#define RUMBLE_DURATION_MS     50
#define MAX_RECONNECT_ATTEMPTS 5
#define RECONNECT_DELAY_MS     5000

/*******************************************************************************
 * Button Mapping Structure
 ******************************************************************************/
typedef struct {
    uint16_t key_a, key_b, key_x, key_y;
    uint16_t key_lb, key_rb;
    uint16_t key_ls, key_rs;
    uint16_t key_view, key_menu;
    uint16_t key_dpad_up, key_dpad_down, key_dpad_left, key_dpad_right;
} ButtonMapping;

/*******************************************************************************
 * Stick Mapping Structure
 ******************************************************************************/
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

/*******************************************************************************
 * Trigger Mapping Structure
 ******************************************************************************/
typedef struct {
    TriggerMode left_trigger_mode;
    TriggerMode right_trigger_mode;
    uint16_t left_trigger_key;
    uint16_t right_trigger_key;
    uint8_t threshold;
} TriggerMapping;

/*******************************************************************************
 * Rumble Settings Structure
 ******************************************************************************/
typedef struct {
    bool enabled;
    bool button_feedback;
    uint8_t intensity;
    uint16_t duration_ms;
} RumbleSettings;

/*******************************************************************************
 * Feature Settings Structure
 ******************************************************************************/
typedef struct {
    RumbleSettings rumble;
} FeatureSettings;

/*******************************************************************************
 * Controller Mapping Structure (Main Config)
 ******************************************************************************/
typedef struct {
    ButtonMapping buttons;
    StickMapping sticks;
    TriggerMapping triggers;
    FeatureSettings features;
    bool console_output_enabled;
    bool streaming_mode;
    LogLevel log_level;
} ControllerMapping;

/*******************************************************************************
 * Input State Structure
 ******************************************************************************/
typedef struct {
    bool keys[256];
    bool mouse_left;
    bool mouse_right;
    bool mouse_middle;

    int16_t prev_left_stick_x;
    int16_t prev_left_stick_y;
    int16_t prev_right_stick_x;
    int16_t prev_right_stick_y;

    int16_t current_left_stick_x;
    int16_t current_left_stick_y;
    int16_t current_right_stick_x;
    int16_t current_right_stick_y;

    // Post-deadzone cached values (optimization)
    int16_t dz_left_stick_x;
    int16_t dz_left_stick_y;
    int16_t dz_right_stick_x;
    int16_t dz_right_stick_y;

    float smoothed_right_x;
    float smoothed_right_y;
    float smoothed_left_x;
    float smoothed_left_y;

    float mouse_dx;
    float mouse_dy;

    // One physical control owns each source; shared keys/buttons stay held.
    uint16_t source_binding[SOURCE_COUNT];
    bool source_pressed[SOURCE_COUNT];
    uint8_t key_refs[256];
    uint8_t mouse_refs[3];

    // Scroll bindings: [0] = up, [1] = down. One notch on press, then repeat while held.
    uint8_t scroll_refs[2];
    float scroll_held_ms[2];
} InputState;

/*******************************************************************************
 * USB Context Structure
 ******************************************************************************/
typedef struct libusb_context libusb_context;
typedef struct libusb_device_handle libusb_device_handle;
typedef struct libusb_transfer libusb_transfer;

typedef struct {
    libusb_context *ctx;
    libusb_device_handle *handle;
    uint8_t in_endpoint;
    uint8_t out_endpoint;
    uint8_t sequence;
    atomic_bool connected;
    int reconnect_attempts;

    // Async transfer (Phase 3)
    libusb_transfer *transfer;
    uint8_t buffer[USB_BUFFER_SIZE];
    bool transfer_pending;
} UsbContext;

#endif // TYPES_H
