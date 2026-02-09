// gip.h
// Game Input Protocol definitions for Xbox One controllers
// Based on reverse engineering from xow project

#ifndef GIP_H
#define GIP_H

#include <stdint.h>

#pragma pack(push, 1)

// GIP packet header
typedef struct {
    uint8_t command;
    uint8_t options;
    uint8_t sequence;
    uint8_t length;
} GipHeader;

// Input packet structure (command 0x20)
// Based on actual Model 1697 controller testing
typedef struct {
    GipHeader header;       // Bytes 0-3
    uint16_t buttons;       // Bytes 4-5
    uint8_t left_trigger;   // Byte 6
    uint8_t padding1;       // Byte 7 (unknown/padding)
    uint8_t right_trigger;  // Byte 8 (NOT byte 7!)
    uint8_t padding2;       // Byte 9 (unknown/padding)
    int16_t left_stick_y;   // Bytes 10-11 (Y before X!)
    int16_t left_stick_x;   // Bytes 12-13
    int16_t right_stick_y;  // Bytes 14-15
    int16_t right_stick_x;  // Bytes 16-17
} GipInputPacket;

// Rumble packet structure (command 0x09)
typedef struct {
    GipHeader header;
    uint8_t enable;
    uint8_t magnitude_left;    // Left motor (low frequency)
    uint8_t magnitude_right;   // Right motor (high frequency)
    uint8_t magnitude_trigger_left;
    uint8_t magnitude_trigger_right;
    uint8_t duration;
    uint8_t delay;
    uint8_t repeat;
} GipRumblePacket;

#pragma pack(pop)

// GIP Command Types
#define GIP_CMD_ACKNOWLEDGE    0x01
#define GIP_CMD_ANNOUNCE       0x02
#define GIP_CMD_STATUS         0x03
#define GIP_CMD_IDENTIFY       0x04
#define GIP_CMD_POWER          0x05
#define GIP_CMD_AUTHENTICATE   0x06
#define GIP_CMD_GUIDE_BUTTON   0x07
#define GIP_CMD_RUMBLE         0x09
#define GIP_CMD_LED            0x0A
#define GIP_CMD_SERIAL_NUM     0x1E
#define GIP_CMD_INPUT          0x20

// Button bit masks (from GipInputPacket.buttons)
#define XBOX_BTN_SYNC          0x0001
#define XBOX_BTN_DUMMY1        0x0002  // Unused
#define XBOX_BTN_MENU          0x0004  // Start button
#define XBOX_BTN_VIEW          0x0008  // Back button
#define XBOX_BTN_A             0x0010
#define XBOX_BTN_B             0x0020
#define XBOX_BTN_X             0x0040
#define XBOX_BTN_Y             0x0080
#define XBOX_BTN_DPAD_UP       0x0100
#define XBOX_BTN_DPAD_DOWN     0x0200
#define XBOX_BTN_DPAD_LEFT     0x0400
#define XBOX_BTN_DPAD_RIGHT    0x0800
#define XBOX_BTN_LB            0x1000  // Left bumper
#define XBOX_BTN_RB            0x2000  // Right bumper
#define XBOX_BTN_LS            0x4000  // Left stick button
#define XBOX_BTN_RS            0x8000  // Right stick button

// Helper function to print button state
static inline void print_buttons(uint16_t buttons) {
    if (buttons & XBOX_BTN_A) printf("A ");
    if (buttons & XBOX_BTN_B) printf("B ");
    if (buttons & XBOX_BTN_X) printf("X ");
    if (buttons & XBOX_BTN_Y) printf("Y ");
    if (buttons & XBOX_BTN_LB) printf("LB ");
    if (buttons & XBOX_BTN_RB) printf("RB ");
    if (buttons & XBOX_BTN_LS) printf("LS ");
    if (buttons & XBOX_BTN_RS) printf("RS ");
    if (buttons & XBOX_BTN_MENU) printf("MENU ");
    if (buttons & XBOX_BTN_VIEW) printf("VIEW ");
    if (buttons & XBOX_BTN_DPAD_UP) printf("UP ");
    if (buttons & XBOX_BTN_DPAD_DOWN) printf("DOWN ");
    if (buttons & XBOX_BTN_DPAD_LEFT) printf("LEFT ");
    if (buttons & XBOX_BTN_DPAD_RIGHT) printf("RIGHT ");
}

// Helper function to get command name
static inline const char* gip_command_name(uint8_t command) {
    switch (command) {
        case GIP_CMD_ACKNOWLEDGE: return "Acknowledge";
        case GIP_CMD_ANNOUNCE: return "Announce";
        case GIP_CMD_STATUS: return "Status";
        case GIP_CMD_IDENTIFY: return "Identify";
        case GIP_CMD_POWER: return "Power";
        case GIP_CMD_AUTHENTICATE: return "Authenticate";
        case GIP_CMD_GUIDE_BUTTON: return "Guide Button";
        case GIP_CMD_RUMBLE: return "Rumble";
        case GIP_CMD_LED: return "LED";
        case GIP_CMD_SERIAL_NUM: return "Serial Number";
        case GIP_CMD_INPUT: return "Input";
        default: return "Unknown";
    }
}

#endif // GIP_H
