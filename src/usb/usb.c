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
    atomic_init(&ctx->connected, false);

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

/*
 * Supported controllers, matched by exact USB VID:PID. Endpoint discovery and
 * the GIP init sequence below are generic, so other GIP (Xbox One / Series)
 * pads can be added by listing their VID/PID here.
 */
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    const char *name;
} SupportedDevice;

static const SupportedDevice SUPPORTED_DEVICES[] = {
    {0x045e, 0x02dd, "Xbox One Controller (2015 firmware)"},
    {0x0e6f, 0x02de, "PDP Wired Controller for Xbox Series X|S (White Camo)"},
    {0x0e6f, 0x02d8, "PDP Wired Controller for Xbox Series X|S (Blue Camo)"},
    {0x24c6, 0x581a, "PowerA/BDA XB1 Classic Controller"},
};

int usb_open_device(UsbContext *ctx) {
    LOG_INFO("Looking for a supported controller...");

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(ctx->ctx, &list);
    if (count < 0) {
        LOG_ERROR("Failed to enumerate USB devices: %s", libusb_error_name((int)count));
        return -1;
    }

    const SupportedDevice *matched = NULL;
    uint16_t got_vid = 0, got_pid = 0;

    for (ssize_t i = 0; i < count && ctx->handle == NULL; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) {
            continue;
        }
        for (size_t d = 0; d < sizeof(SUPPORTED_DEVICES) / sizeof(SUPPORTED_DEVICES[0]); d++) {
            const SupportedDevice *sd = &SUPPORTED_DEVICES[d];
            if (desc.idVendor != sd->vendor_id || desc.idProduct != sd->product_id) {
                continue;
            }

            int open_result = libusb_open(list[i], &ctx->handle);
            if (open_result < 0) {
                /* Matched, but couldn't open it -- keep looking for another. */
                LOG_WARN("Found %s (%04x:%04x) but could not open it: %s; trying next match",
                         sd->name, desc.idVendor, desc.idProduct,
                         libusb_error_name(open_result));
                ctx->handle = NULL;
                break;
            }

            matched = sd;
            got_vid = desc.idVendor;
            got_pid = desc.idProduct;
            break;
        }
    }

    libusb_free_device_list(list, 1);

    if (ctx->handle == NULL) {
        LOG_ERROR("No supported controller found (or none could be opened). "
                  "Make sure it's plugged in and you're running with sudo");
        return -1;
    }
    LOG_INFO("Found controller: %s (%04x:%04x)", matched->name, got_vid, got_pid);

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
    struct libusb_config_descriptor *desc = NULL;
    result = libusb_get_active_config_descriptor(libusb_get_device(ctx->handle), &desc);
    if (result == 0 && (desc->bNumInterfaces < 1 || desc->interface[0].num_altsetting < 1)) {
        libusb_free_config_descriptor(desc);
        result = LIBUSB_ERROR_NOT_FOUND;
    }
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
        if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
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
static int usb_write_packet(UsbContext *ctx, uint8_t *buffer, int size, int timeout_ms) {
    int transferred = 0;
    int result = libusb_interrupt_transfer(ctx->handle, ctx->out_endpoint, buffer,
                                            size, &transferred, timeout_ms);
    if (result != 0) return result;
    return transferred == size ? 0 : LIBUSB_ERROR_IO;
}

/* Acknowledgement payloads are GIP_ACK_PAYLOAD bytes: the first two name the
   message being acknowledged, the rest are zero. */
#define GIP_ACK_PAYLOAD 9

static int usb_send_ack(UsbContext *ctx, uint8_t sequence, bool verbose) {
    uint8_t ack_packet[sizeof(GipHeader) + GIP_ACK_PAYLOAD] = {
        GIP_CMD_ACKNOWLEDGE, GIP_OPT_INTERNAL, sequence, GIP_ACK_PAYLOAD,
    };
    int result = usb_write_packet(ctx, ack_packet, sizeof(ack_packet), USB_ACK_TIMEOUT_MS);
    if (result != 0) {
        LOG_WARN("Failed to acknowledge announce (seq=%d): %s", sequence,
                 libusb_error_name(result));
    } else if (verbose) {
        LOG_DEBUG("Sent ACK (seq=%d)", sequence);
    }
    return result;
}

/* The guide button report asks for an acknowledgement; without one the
   controller keeps resending it. Same packet as Linux xpad's mode-report ack. */
int usb_ack_guide_button(UsbContext *ctx, uint8_t sequence) {
    uint8_t packet[sizeof(GipHeader) + GIP_ACK_PAYLOAD] = {
        GIP_CMD_ACKNOWLEDGE, GIP_OPT_INTERNAL, sequence, GIP_ACK_PAYLOAD,
        0x00, GIP_CMD_GUIDE_BUTTON, GIP_OPT_INTERNAL, 0x02,
    };
    return usb_write_packet(ctx, packet, sizeof(packet), USB_OUTPUT_TIMEOUT_MS);
}

static const char* gip_command_name(uint8_t command) {
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

#define INIT_PACKET_GAP_US 50000    // Between init packets
#define INIT_SETTLE_US     500000   // Before the first input read

int usb_initialize_controller(UsbContext *ctx, bool verbose) {
    uint8_t buffer[USB_BUFFER_SIZE];
    int transferred;
    int result;

    if (verbose) {
        LOG_INFO("Initializing Controller - Performing GIP handshake...");
    }

    // Acknowledge the controller's announce packets until it goes quiet
    for (int attempt = 0; attempt < USB_INIT_READ_ATTEMPTS; attempt++) {
        transferred = 0;
        result = libusb_interrupt_transfer(ctx->handle, ctx->in_endpoint, buffer,
                                            sizeof(buffer), &transferred, USB_INIT_TIMEOUT_MS);

        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            const GipHeader *header = (const GipHeader *)buffer;

            if (verbose) {
                LOG_DEBUG("Received: %s (0x%02x), seq=%d",
                          gip_command_name(header->command), header->command, header->sequence);
            }

            if (header->command == GIP_CMD_ANNOUNCE) {
                usb_send_ack(ctx, header->sequence, verbose);
            }
        } else if (result == LIBUSB_ERROR_NO_DEVICE) {
            LOG_ERROR("Controller disappeared during the handshake");
            return -1;
        } else if (result == LIBUSB_ERROR_TIMEOUT) {
            break;
        }
    }

    if (verbose) {
        LOG_INFO("Initialization complete! Sending GIP init sequence...");
    }

    /*
     * GIP init sequence, mirroring the Linux xpad driver. PDP pads (0x0e6f)
     * need power_on -> led_on -> auth_done to start sending input reports;
     * sending only power_on leaves the LED dark and the pad silent.
     * Byte [2] of each packet is an incrementing sequence number
     * (xpad: odata[2] = odata_serial++).
     */
    uint8_t power_on[]   = {GIP_CMD_POWER, GIP_OPT_INTERNAL, 0x00, 0x01, 0x00};
    uint8_t led_on[]     = {GIP_CMD_LED, GIP_OPT_INTERNAL, 0x00, 0x03, 0x00, 0x01, 0x14};
    uint8_t auth_done[]  = {GIP_CMD_AUTHENTICATE, GIP_OPT_INTERNAL, 0x00, 0x02, 0x01, 0x00};

    struct { uint8_t *pkt; int len; const char *name; } init_seq[] = {
        {power_on,  (int)sizeof(power_on),  "POWER ON"},
        {led_on,    (int)sizeof(led_on),    "LED ON"},
        {auth_done, (int)sizeof(auth_done), "AUTH DONE"},
    };

    for (size_t i = 0; i < sizeof(init_seq) / sizeof(init_seq[0]); i++) {
        init_seq[i].pkt[2] = ctx->sequence++;
        result = usb_write_packet(ctx, init_seq[i].pkt, init_seq[i].len, USB_ACK_TIMEOUT_MS);
        if (result != 0) {
            LOG_ERROR("Failed to send %s init packet: %s", init_seq[i].name,
                      libusb_error_name(result));
            return -1;
        }
        if (verbose) {
            LOG_INFO("Sent %s (seq=%d)", init_seq[i].name, init_seq[i].pkt[2]);
        }
        usleep(INIT_PACKET_GAP_US);
    }

    usleep(INIT_SETTLE_US);
    return 0;
}

int usb_read_packet(UsbContext *ctx, uint8_t *buffer, int size, int *transferred, int timeout_ms) {
    *transferred = 0;
    return libusb_interrupt_transfer(ctx->handle, ctx->in_endpoint, buffer,
                                      size, transferred, timeout_ms);
}

/*******************************************************************************
 * Rumble/Haptic Feedback
 ******************************************************************************/
#define RUMBLE_MAIN_MOTORS   0x03   // Left + right main motors, not the triggers
#define RUMBLE_DURATION_UNIT 10     // Duration field is in 10 ms steps

int usb_rumble_pulse(UsbContext *ctx, uint8_t intensity, uint16_t duration_ms) {
    unsigned units = duration_ms / RUMBLE_DURATION_UNIT;
    GipRumblePacket pkt = {
        .header = {
            .command = GIP_CMD_RUMBLE,
            .options = 0x00,
            .sequence = ctx->sequence++,   // wraps at 255, as the protocol expects
            .length = sizeof(GipRumblePacket) - sizeof(GipHeader)
        },
        .reserved = 0,
        .enable = RUMBLE_MAIN_MOTORS,
        .magnitude_trigger_left = 0,
        .magnitude_trigger_right = 0,
        .magnitude_left = intensity,
        .magnitude_right = intensity,
        .duration = (uint8_t)(units > UINT8_MAX ? UINT8_MAX : units),
        .delay = 0,
        .repeat = 0
    };

    int result = usb_write_packet(ctx, (uint8_t*)&pkt, sizeof(pkt), USB_OUTPUT_TIMEOUT_MS);
    if (result == 0) {
        LOG_DEBUG("Rumble pulse sent: intensity=%d, duration=%dms", intensity, duration_ms);
    } else {
        LOG_WARN("Failed to send rumble pulse: %s", libusb_error_name(result));
    }
    return result;
}

/*******************************************************************************
 * Helpers
 ******************************************************************************/
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
