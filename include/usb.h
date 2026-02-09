/*******************************************************************************
 * usb.h - USB layer declarations
 ******************************************************************************/

#ifndef USB_H
#define USB_H

#include "types.h"
#include <libusb.h>

/*******************************************************************************
 * GIP Protocol Constants
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

/*******************************************************************************
 * GIP Packet Structures
 ******************************************************************************/
#pragma pack(push, 1)

typedef struct {
    uint8_t command;
    uint8_t options;
    uint8_t sequence;
    uint8_t length;
} GipHeader;

typedef struct {
    GipHeader header;
    uint16_t buttons;
    uint8_t left_trigger;
    uint8_t padding1;
    uint8_t right_trigger;
    uint8_t padding2;
    int16_t left_stick_y;
    int16_t left_stick_x;
    int16_t right_stick_y;
    int16_t right_stick_x;
} GipInputPacket;

typedef struct {
    GipHeader header;
    uint8_t enable;
    uint8_t magnitude_left;
    uint8_t magnitude_right;
    uint8_t magnitude_trigger_left;
    uint8_t magnitude_trigger_right;
    uint8_t duration;
    uint8_t delay;
    uint8_t repeat;
} GipRumblePacket;

#pragma pack(pop)

/*******************************************************************************
 * USB Context Management
 ******************************************************************************/
int usb_init(UsbContext *ctx);
void usb_cleanup(UsbContext *ctx);
int usb_open_device(UsbContext *ctx);
void usb_close_device(UsbContext *ctx);

/*******************************************************************************
 * USB Communication
 ******************************************************************************/
int usb_send_ack(UsbContext *ctx, uint8_t sequence, bool verbose);
int usb_initialize_controller(UsbContext *ctx, bool verbose);
int usb_read_packet(UsbContext *ctx, uint8_t *buffer, int size, int *transferred, int timeout_ms);
int usb_write_packet(UsbContext *ctx, uint8_t *buffer, int size);

/*******************************************************************************
 * Rumble/Haptic Feedback
 ******************************************************************************/
int usb_rumble_pulse(UsbContext *ctx, uint8_t intensity, uint16_t duration_ms);

/*******************************************************************************
 * Helpers
 ******************************************************************************/
const char* gip_command_name(uint8_t command);
void print_buttons(uint16_t buttons);

#endif // USB_H
