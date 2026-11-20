/*******************************************************************************
 * usb.h - USB layer declarations
 ******************************************************************************/

#ifndef USB_H
#define USB_H

#include "types.h"
#include "gip_protocol.h"
#include <libusb.h>
#include <stdatomic.h>

/*******************************************************************************
 * USB Constants
 ******************************************************************************/
/* Supported controllers are listed in SUPPORTED_DEVICES[] (src/usb/usb.c) */
#define USB_BUFFER_SIZE        64
#define USB_READ_TIMEOUT_MS    100     // Also bounds how fast the loop sees "stop"
#define USB_INIT_TIMEOUT_MS    2000
#define USB_ACK_TIMEOUT_MS     1000
#define USB_OUTPUT_TIMEOUT_MS  100
#define USB_INIT_READ_ATTEMPTS 5
#define MAX_RECONNECT_ATTEMPTS 5
#define RECONNECT_DELAY_MS     5000

/*******************************************************************************
 * USB Context
 *
 * The libusb handle is only ever used from the driver's packet thread (reads,
 * acks and rumble writes). `connected` is the one field other threads read.
 ******************************************************************************/
typedef struct {
    libusb_context *ctx;
    libusb_device_handle *handle;
    uint8_t in_endpoint;
    uint8_t out_endpoint;
    uint8_t sequence;          // Next outgoing GIP sequence number
    atomic_bool connected;
} UsbContext;

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
int usb_ack_guide_button(UsbContext *ctx, uint8_t sequence);
int usb_initialize_controller(UsbContext *ctx, bool verbose);
/* Returns a libusb status. LIBUSB_ERROR_TIMEOUT is normal when the controller
   is idle; *transferred can still be non-zero then and must be honored. */
int usb_read_packet(UsbContext *ctx, uint8_t *buffer, int size, int *transferred, int timeout_ms);

/*******************************************************************************
 * Rumble/Haptic Feedback
 ******************************************************************************/
int usb_rumble_pulse(UsbContext *ctx, uint8_t intensity, uint16_t duration_ms);

/*******************************************************************************
 * Helpers
 ******************************************************************************/
void print_buttons(uint16_t buttons);

#endif // USB_H
