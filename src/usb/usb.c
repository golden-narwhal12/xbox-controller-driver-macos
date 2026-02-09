/*******************************************************************************
 * usb.c - USB layer implementation
 ******************************************************************************/

#include "../../include/usb.h"
#include "../../include/log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*******************************************************************************
 * USB Context Management
 ******************************************************************************/
int usb_init(UsbContext *ctx) {
    memset(ctx, 0, sizeof(UsbContext));

    int result = libusb_init(&ctx->ctx);
    if (result < 0) {
        LOG_ERROR("Failed to initialize libusb: %s", libusb_error_name(result));
        return -1;
    }

    LOG_DEBUG("libusb initialized successfully");
    return 0;
}

void usb_cleanup(UsbContext *ctx) {
    if (ctx->handle) {
        usb_close_device(ctx);
    }
    if (ctx->ctx) {
        libusb_exit(ctx->ctx);
        ctx->ctx = NULL;
    }
    LOG_DEBUG("USB context cleaned up");
}

int usb_open_device(UsbContext *ctx) {
    LOG_INFO("Looking for Xbox controller...");

    ctx->handle = libusb_open_device_with_vid_pid(ctx->ctx, XBOX_VENDOR_ID, XBOX_PRODUCT_ID);
    if (!ctx->handle) {
        LOG_ERROR("Controller not found. Make sure it's plugged in and you're running with sudo");
        return -1;
    }
    LOG_INFO("Found controller");

    // Detach kernel driver if attached
    if (libusb_kernel_driver_active(ctx->handle, 0) == 1) {
        int result = libusb_detach_kernel_driver(ctx->handle, 0);
        if (result < 0) {
            LOG_WARN("Could not detach kernel driver: %s", libusb_error_name(result));
        } else {
            LOG_DEBUG("Detached kernel driver");
        }
    }

    // Claim interface
    int result = libusb_claim_interface(ctx->handle, 0);
    if (result < 0) {
        LOG_ERROR("Failed to claim interface: %s", libusb_error_name(result));
        libusb_close(ctx->handle);
        ctx->handle = NULL;
        return -1;
    }
    LOG_INFO("Claimed interface");

    // Get endpoints
    struct libusb_config_descriptor *desc;
    result = libusb_get_active_config_descriptor(libusb_get_device(ctx->handle), &desc);
    if (result < 0) {
        LOG_ERROR("Failed to get config descriptor: %s", libusb_error_name(result));
        libusb_release_interface(ctx->handle, 0);
        libusb_close(ctx->handle);
        ctx->handle = NULL;
        return -1;
    }

    ctx->in_endpoint = 0;
    ctx->out_endpoint = 0;

    const struct libusb_interface *inter = &desc->interface[0];
    const struct libusb_interface_descriptor *interdesc = &inter->altsetting[0];

    for (int i = 0; i < interdesc->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *ep = &interdesc->endpoint[i];
        if ((ep->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                ctx->in_endpoint = ep->bEndpointAddress;
            } else {
                ctx->out_endpoint = ep->bEndpointAddress;
            }
        }
    }

    libusb_free_config_descriptor(desc);

    if (ctx->in_endpoint == 0 || ctx->out_endpoint == 0) {
        LOG_ERROR("Could not find interrupt endpoints");
        libusb_release_interface(ctx->handle, 0);
        libusb_close(ctx->handle);
        ctx->handle = NULL;
        return -1;
    }

    LOG_DEBUG("Found endpoints: IN=0x%02x, OUT=0x%02x", ctx->in_endpoint, ctx->out_endpoint);
    ctx->connected = true;
    ctx->reconnect_attempts = 0;

    return 0;
}

void usb_close_device(UsbContext *ctx) {
    if (ctx->handle) {
        libusb_release_interface(ctx->handle, 0);
        libusb_close(ctx->handle);
        ctx->handle = NULL;
    }
    ctx->connected = false;
    ctx->in_endpoint = 0;
    ctx->out_endpoint = 0;
    LOG_DEBUG("Device closed");
}

/*******************************************************************************
 * USB Communication
 ******************************************************************************/
int usb_send_ack(UsbContext *ctx, uint8_t sequence, bool verbose) {
    uint8_t ack_packet[] = {
        GIP_CMD_ACKNOWLEDGE, 0x20, sequence, 0x09,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    int transferred;
    int result = libusb_interrupt_transfer(ctx->handle, ctx->out_endpoint, ack_packet,
                                            sizeof(ack_packet), &transferred, USB_ACK_TIMEOUT_MS);

    if (result == 0 && verbose) {
        LOG_DEBUG("Sent ACK (seq=%d)", sequence);
    }
    return (result == 0) ? 0 : -1;
}

int usb_initialize_controller(UsbContext *ctx, bool verbose) {
    uint8_t buffer[USB_BUFFER_SIZE];
    int transferred;
    int result;

    if (verbose) {
        LOG_INFO("Initializing Controller - Performing GIP handshake...");
    }

    for (int attempt = 0; attempt < 5; attempt++) {
        result = libusb_interrupt_transfer(ctx->handle, ctx->in_endpoint, buffer,
                                            sizeof(buffer), &transferred, USB_INIT_TIMEOUT_MS);

        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            GipHeader *header = (GipHeader *)buffer;

            if (verbose) {
                LOG_DEBUG("Received: %s (0x%02x), seq=%d",
                          gip_command_name(header->command), header->command, header->sequence);
            }

            if (header->command == GIP_CMD_ANNOUNCE) {
                usb_send_ack(ctx, header->sequence, verbose);
            }
        } else if (result == LIBUSB_ERROR_TIMEOUT) {
            break;
        }
    }

    if (verbose) {
        LOG_INFO("Initialization complete! Sending POWER ON command...");
    }

    uint8_t power_on[] = {GIP_CMD_POWER, 0x20, 0x00, 0x01, 0x00};
    result = libusb_interrupt_transfer(ctx->handle, ctx->out_endpoint, power_on,
                                        sizeof(power_on), &transferred, USB_ACK_TIMEOUT_MS);

    if (result == 0 && verbose) {
        LOG_INFO("Controller powered on!");
    }

    usleep(500000);
    return 0;
}

int usb_read_packet(UsbContext *ctx, uint8_t *buffer, int size, int *transferred, int timeout_ms) {
    return libusb_interrupt_transfer(ctx->handle, ctx->in_endpoint, buffer,
                                      size, transferred, timeout_ms);
}

int usb_write_packet(UsbContext *ctx, uint8_t *buffer, int size) {
    int transferred;
    return libusb_interrupt_transfer(ctx->handle, ctx->out_endpoint, buffer,
                                      size, &transferred, USB_ACK_TIMEOUT_MS);
}

/*******************************************************************************
 * Rumble/Haptic Feedback
 ******************************************************************************/
int usb_rumble_pulse(UsbContext *ctx, uint8_t intensity, uint16_t duration_ms) {
    GipRumblePacket pkt = {
        .header = {
            .command = GIP_CMD_RUMBLE,
            .options = 0x00,
            .sequence = 0x00,
            .length = sizeof(GipRumblePacket) - sizeof(GipHeader)
        },
        .enable = 0x03,  // Enable both motors
        .magnitude_left = intensity,
        .magnitude_right = intensity,
        .magnitude_trigger_left = 0,
        .magnitude_trigger_right = 0,
        .duration = (uint8_t)(duration_ms / 10),
        .delay = 0,
        .repeat = 0
    };

    int result = usb_write_packet(ctx, (uint8_t*)&pkt, sizeof(pkt));
    if (result == 0) {
        LOG_DEBUG("Rumble pulse sent: intensity=%d, duration=%dms", intensity, duration_ms);
    } else {
        LOG_WARN("Failed to send rumble pulse");
    }
    return result;
}

/*******************************************************************************
 * Helpers
 ******************************************************************************/
const char* gip_command_name(uint8_t command) {
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

void print_buttons(uint16_t buttons) {
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
