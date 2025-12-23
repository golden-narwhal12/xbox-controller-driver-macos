// phase3_gip_test.c
// Tests GIP protocol communication with Xbox One controller
// Compile: make xbox_gip_test
// Run: sudo ./xbox_gip_test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libusb.h>
#include "gip.h"

#define XBOX_VENDOR_ID  0x045e
#define XBOX_PRODUCT_ID 0x02dd

static int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nShutting down...\n");
}

// Send acknowledgment packet
int send_ack(libusb_device_handle *handle, uint8_t out_endpoint, uint8_t sequence) {
    uint8_t ack_packet[] = {
        GIP_CMD_ACKNOWLEDGE,
        0x20,       // options
        sequence,   // sequence number from the packet we're acknowledging
        0x09,       // length
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    int transferred;
    int result = libusb_interrupt_transfer(
        handle,
        out_endpoint,
        ack_packet,
        sizeof(ack_packet),
        &transferred,
        1000
    );
    
    if (result == 0) {
        printf("  → Sent ACK (seq=%d)\n", sequence);
        return 0;
    } else {
        printf("  ✗ Failed to send ACK: %s\n", libusb_error_name(result));
        return -1;
    }
}

// Initialize controller with GIP handshake
int initialize_controller(libusb_device_handle *handle, uint8_t in_endpoint, uint8_t out_endpoint) {
    uint8_t buffer[64];
    int transferred;
    int result;
    
    printf("\n=== Initializing Controller ===\n");
    printf("This performs the GIP handshake sequence\n\n");
    
    // The controller should announce itself when we first connect
    // We need to read and acknowledge these packets
    
    for (int attempt = 0; attempt < 5; attempt++) {
        printf("Reading initialization packet %d...\n", attempt + 1);
        
        result = libusb_interrupt_transfer(
            handle,
            in_endpoint,
            buffer,
            sizeof(buffer),
            &transferred,
            2000  // 2 second timeout
        );
        
        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            GipHeader *header = (GipHeader *)buffer;
            
            printf("  Received: %s (0x%02x), seq=%d, len=%d\n",
                   gip_command_name(header->command),
                   header->command,
                   header->sequence,
                   header->length);
            
            // Print raw packet
            printf("  Data: ");
            for (int i = 0; i < transferred && i < 32; i++) {
                printf("%02x ", buffer[i]);
            }
            if (transferred > 32) printf("...");
            printf("\n");
            
            // Send acknowledgment for announce packets
            if (header->command == GIP_CMD_ANNOUNCE) {
                send_ack(handle, out_endpoint, header->sequence);
            }
            
        } else if (result == LIBUSB_ERROR_TIMEOUT) {
            printf("  Timeout (this is normal after init sequence)\n");
            break;
        } else {
            printf("  Error: %s\n", libusb_error_name(result));
        }
        
        printf("\n");
    }
    
    printf("✅ Initialization complete!\n");
    
    // Send POWER ON command to start input mode
    printf("\nSending POWER ON command...\n");
    uint8_t power_on[] = {
        GIP_CMD_POWER,  // 0x05
        0x20,           // options
        0x00,           // sequence (can be 0 for commands we initiate)
        0x01,           // length
        0x00            // mode: 0x00 = on
    };
    
    result = libusb_interrupt_transfer(
        handle,
        out_endpoint,
        power_on,
        sizeof(power_on),
        &transferred,
        1000
    );
    
    if (result == 0) {
        printf("✅ Power ON command sent!\n");
    } else {
        printf("⚠️  Failed to send power command: %s\n", libusb_error_name(result));
    }
    
    // Wait a moment for controller to respond
    printf("Waiting for controller to start input mode...\n\n");
    usleep(500000);  // 500ms
    
    return 0;
}

// Main input reading loop
void input_loop(libusb_device_handle *handle, uint8_t in_endpoint) {
    uint8_t buffer[64];
    int transferred;
    int result;
    int input_count = 0;
    
    printf("=== Reading Controller Input ===\n");
    printf("Move sticks and press buttons...\n");
    printf("Press Ctrl+C to exit\n\n");
    
    while (running) {
        result = libusb_interrupt_transfer(
            handle,
            in_endpoint,
            buffer,
            sizeof(buffer),
            &transferred,
            100  // 100ms timeout
        );
        
        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            GipHeader *header = (GipHeader *)buffer;
            
            // Check if this is an input packet
            if (header->command == GIP_CMD_INPUT && transferred >= (int)sizeof(GipInputPacket)) {
                GipInputPacket *input = (GipInputPacket *)buffer;
                input_count++;
                
                // Clear line and print input state
                printf("\r[%04d] ", input_count);
                
                // Buttons
                printf("BTN: ");
                if (input->buttons) {
                    print_buttons(input->buttons);
                } else {
                    printf("none ");
                }
                
                // Pad to consistent width
                printf("%-40s", "");
                printf("\r[%04d] BTN: ", input_count);
                print_buttons(input->buttons);
                
                // Triggers
                printf("| LT:%3d RT:%3d ", 
                       input->left_trigger, 
                       input->right_trigger);
                
                // Sticks (note: Y comes before X in packet, but display as X,Y for clarity)
                printf("| LS:(%6d,%6d) RS:(%6d,%6d)  ",
                       input->left_stick_x, input->left_stick_y,
                       input->right_stick_x, input->right_stick_y);
                
                fflush(stdout);
                
            } else if (header->command == GIP_CMD_GUIDE_BUTTON) {
                // Guide button press
                printf("\n🎮 GUIDE BUTTON PRESSED\n");
                
            } else {
                // Other GIP packet
                printf("\nReceived: %s (0x%02x)\n", 
                       gip_command_name(header->command),
                       header->command);
            }
            
        } else if (result != LIBUSB_ERROR_TIMEOUT) {
            printf("\nRead error: %s\n", libusb_error_name(result));
            if (result == LIBUSB_ERROR_NO_DEVICE) {
                printf("Controller disconnected!\n");
                break;
            }
        }
    }
    
    printf("\n\n");
}

int main() {
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int result;
    
    // Set up signal handler for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Xbox One Controller GIP Protocol Test\n");
    printf("======================================\n\n");
    
    // Initialize libusb
    result = libusb_init(&ctx);
    if (result < 0) {
        printf("❌ Failed to initialize libusb: %s\n", libusb_error_name(result));
        return 1;
    }
    
    // Find controller
    printf("Looking for Xbox controller...\n");
    handle = libusb_open_device_with_vid_pid(ctx, XBOX_VENDOR_ID, XBOX_PRODUCT_ID);
    if (!handle) {
        printf("❌ Controller not found\n");
        libusb_exit(ctx);
        return 1;
    }
    printf("✅ Found controller\n");
    
    // Detach kernel driver if needed
    if (libusb_kernel_driver_active(handle, 0) == 1) {
        libusb_detach_kernel_driver(handle, 0);
    }
    
    // Claim interface
    result = libusb_claim_interface(handle, 0);
    if (result < 0) {
        printf("❌ Failed to claim interface: %s\n", libusb_error_name(result));
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    printf("✅ Claimed interface\n");
    
    // Get endpoint addresses
    struct libusb_config_descriptor *config;
    libusb_get_active_config_descriptor(libusb_get_device(handle), &config);
    
    uint8_t in_endpoint = 0;
    uint8_t out_endpoint = 0;
    
    const struct libusb_interface *inter = &config->interface[0];
    const struct libusb_interface_descriptor *interdesc = &inter->altsetting[0];
    
    for (int i = 0; i < interdesc->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *ep = &interdesc->endpoint[i];
        int type = ep->bmAttributes & 0x03;
        
        if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                in_endpoint = ep->bEndpointAddress;
            } else {
                out_endpoint = ep->bEndpointAddress;
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    
    printf("Endpoints: IN=0x%02x, OUT=0x%02x\n", in_endpoint, out_endpoint);
    
    if (in_endpoint == 0 || out_endpoint == 0) {
        printf("❌ Could not find interrupt endpoints\n");
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    
    // Perform GIP initialization
    initialize_controller(handle, in_endpoint, out_endpoint);
    
    // Enter main input loop
    input_loop(handle, in_endpoint);
    
    // Cleanup
    printf("Cleaning up...\n");
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    
    printf("✅ Done!\n");
    return 0;
}
