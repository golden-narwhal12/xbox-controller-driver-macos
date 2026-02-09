// hid_descriptor.h
// HID Report Descriptor for Xbox One Controller
// This tells macOS what our virtual gamepad looks like

#ifndef HID_DESCRIPTOR_H
#define HID_DESCRIPTOR_H

#include <stdint.h>

// HID Report Descriptor for a standard gamepad
// This matches the Xbox controller layout
static const uint8_t gamepad_hid_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    
    // Buttons (16 buttons: A, B, X, Y, LB, RB, View, Menu, LS, RS, DPad x4, Guide, Share)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x10,        //   Usage Maximum (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x10,        //   Report Count (16 buttons)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Left Trigger (0-255)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x32,        //   Usage (Z) - conventionally used for left trigger
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Right Trigger (0-255)
    0x09, 0x35,        //   Usage (Rz) - conventionally used for right trigger
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Left Stick X and Y (-32768 to 32767)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x02,        //   Report Count (2 axes)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Right Stick X and Y (-32768 to 32767)
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x02,        //   Report Count (2 axes)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    0xC0               // End Collection
};

// Size of the descriptor
#define GAMEPAD_HID_DESCRIPTOR_SIZE sizeof(gamepad_hid_descriptor)

// HID Report structure matching the descriptor above
// This is the data we'll send to macOS
#pragma pack(push, 1)
typedef struct {
    uint16_t buttons;      // 16 buttons (bit field)
    uint8_t left_trigger;  // 0-255
    uint8_t right_trigger; // 0-255
    int16_t left_stick_x;  // -32768 to 32767
    int16_t left_stick_y;  // -32768 to 32767
    int16_t right_stick_x; // -32768 to 32767
    int16_t right_stick_y; // -32768 to 32767
} GamepadReport;
#pragma pack(pop)

// Size check - should be 12 bytes
_Static_assert(sizeof(GamepadReport) == 12, "GamepadReport must be 12 bytes");

#endif // HID_DESCRIPTOR_H
