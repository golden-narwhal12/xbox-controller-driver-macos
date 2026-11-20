/*******************************************************************************
 * gip_protocol.h - Microsoft GIP (Xbox One / Series) wire format
 *
 * Pure protocol definitions with no transport dependency, so both the USB
 * layer and the translation layer can include it.
 *
 * Conventions on the wire:
 *   - Multi-byte fields are little-endian. Input reports are decoded field by
 *     field with gip_parse_input() (src/input/input.c), so the host's byte
 *     order and struct padding never matter.
 *   - Triggers are 10-bit pressure values (0..1023) in 16-bit fields.
 *   - Sticks are signed 16-bit. Stick +Y is UP; the translation layer
 *     converts to screen convention (+Y down) when it processes a report.
 ******************************************************************************/

#ifndef GIP_PROTOCOL_H
#define GIP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************************************
 * GIP Commands and header option bits
 ******************************************************************************/
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

#define GIP_OPT_ACK            0x10    // Sender wants an acknowledgement
#define GIP_OPT_INTERNAL       0x20    // System (not vendor) message

/*******************************************************************************
 * Header (four single bytes: no padding or byte-order concerns)
 ******************************************************************************/
typedef struct {
    uint8_t command;
    uint8_t options;
    uint8_t sequence;
    uint8_t length;
} GipHeader;

_Static_assert(sizeof(GipHeader) == 4, "GIP header must be 4 bytes");

/*******************************************************************************
 * Input report (command 0x20): byte offsets on the wire, and the decoded form
 ******************************************************************************/
#define GIP_INPUT_OFF_BUTTONS        4
#define GIP_INPUT_OFF_LEFT_TRIGGER   6
#define GIP_INPUT_OFF_RIGHT_TRIGGER  8
#define GIP_INPUT_OFF_LEFT_X         10
#define GIP_INPUT_OFF_LEFT_Y         12
#define GIP_INPUT_OFF_RIGHT_X        14
#define GIP_INPUT_OFF_RIGHT_Y        16
#define GIP_INPUT_LENGTH             18

/* Button bits. Bit 0 is the sync button and bit 1 is unused; neither maps. */
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

typedef struct {
    uint16_t buttons;
    uint16_t left_trigger;     // 0..1023
    uint16_t right_trigger;    // 0..1023
    int16_t left_stick_x;
    int16_t left_stick_y;      // +Y up, as sent by the controller
    int16_t right_stick_x;
    int16_t right_stick_y;     // +Y up
} GipInput;

/*******************************************************************************
 * Rumble command (all single bytes)
 ******************************************************************************/
typedef struct {
    GipHeader header;
    uint8_t reserved;
    uint8_t enable;
    uint8_t magnitude_trigger_left;
    uint8_t magnitude_trigger_right;
    uint8_t magnitude_left;
    uint8_t magnitude_right;
    uint8_t duration;
    uint8_t delay;
    uint8_t repeat;
} GipRumblePacket;

_Static_assert(sizeof(GipRumblePacket) == 13, "GIP rumble packet must be 13 bytes");
_Static_assert(offsetof(GipRumblePacket, enable) == 5, "rumble enable offset");

#endif // GIP_PROTOCOL_H
