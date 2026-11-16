/*******************************************************************************
 * usb.h - USB layer declarations
 ******************************************************************************/

#ifndef USB_H
#define USB_H

#include "types.h"
#include "gip_protocol.h"
#include <libusb.h>

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
int usb_ack_guide_button(UsbContext *ctx, uint8_t sequence);
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
