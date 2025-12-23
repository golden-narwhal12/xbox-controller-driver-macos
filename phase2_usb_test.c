// phase2_usb_test.c
// Tests basic USB communication with Xbox One controller
// Compile: make xbox_usb_test
// Run: sudo ./xbox_usb_test

#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>

#define XBOX_VENDOR_ID  0x045e
#define XBOX_PRODUCT_ID 0x02dd  // Model 1697

int main() {
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int result;
    
    printf("Xbox One Controller USB Test\n");
    printf("=============================\n\n");
    
    // Initialize libusb
    result = libusb_init(&ctx);
    if (result < 0) {
        printf("❌ Failed to initialize libusb: %s\n", libusb_error_name(result));
        return 1;
    }
    
    // Set debug level (optional)
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
    
    // Find and open the Xbox controller
    printf("Looking for Xbox controller (VID=%04x, PID=%04x)...\n", 
           XBOX_VENDOR_ID, XBOX_PRODUCT_ID);
    
    handle = libusb_open_device_with_vid_pid(ctx, XBOX_VENDOR_ID, XBOX_PRODUCT_ID);
    if (!handle) {
        printf("❌ Could not find Xbox controller\n");
        printf("   Make sure it's plugged in and you're running with sudo\n");
        libusb_exit(ctx);
        return 1;
    }
    
    printf("✅ Found Xbox One controller!\n\n");
    
    // Get device descriptor
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(libusb_get_device(handle), &desc);
    
    printf("Device Information:\n");
    printf("  USB Version: %04x\n", desc.bcdUSB);
    printf("  Device Version: %04x\n", desc.bcdDevice);
    printf("  Vendor ID: %04x\n", desc.idVendor);
    printf("  Product ID: %04x\n", desc.idProduct);
    printf("  Device Class: %d\n", desc.bDeviceClass);
    printf("  Number of Configurations: %d\n", desc.bNumConfigurations);
    printf("\n");
    
    // Detach kernel driver if active
    if (libusb_kernel_driver_active(handle, 0) == 1) {
        printf("Kernel driver is active, detaching...\n");
        result = libusb_detach_kernel_driver(handle, 0);
        if (result != 0) {
            printf("⚠️  Warning: Could not detach kernel driver: %s\n", 
                   libusb_error_name(result));
        }
    }
    
    // Claim interface 0 (main controller interface)
    printf("Claiming controller interface...\n");
    result = libusb_claim_interface(handle, 0);
    if (result < 0) {
        printf("❌ Failed to claim interface: %s\n", libusb_error_name(result));
        printf("   This might mean macOS is holding the device.\n");
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    
    printf("✅ Successfully claimed controller interface!\n\n");
    
    // Get configuration descriptor
    struct libusb_config_descriptor *config;
    result = libusb_get_active_config_descriptor(libusb_get_device(handle), &config);
    if (result != 0) {
        printf("❌ Failed to get configuration descriptor\n");
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    
    printf("Configuration:\n");
    printf("  Number of interfaces: %d\n", config->bNumInterfaces);
    printf("\n");
    
    // Get endpoint information for interface 0
    const struct libusb_interface *inter = &config->interface[0];
    const struct libusb_interface_descriptor *interdesc = &inter->altsetting[0];
    
    printf("Interface 0 Endpoints:\n");
    uint8_t in_endpoint = 0;
    uint8_t out_endpoint = 0;
    
    for (int i = 0; i < interdesc->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *endpoint = &interdesc->endpoint[i];
        int direction = endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN;
        
        printf("  Endpoint %d:\n", i);
        printf("    Address: 0x%02x\n", endpoint->bEndpointAddress);
        printf("    Direction: %s\n", direction ? "IN (device to host)" : "OUT (host to device)");
        printf("    Transfer Type: ");
        
        int type = endpoint->bmAttributes & 0x03;
        switch (type) {
            case LIBUSB_TRANSFER_TYPE_CONTROL:
                printf("Control\n");
                break;
            case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
                printf("Isochronous\n");
                break;
            case LIBUSB_TRANSFER_TYPE_BULK:
                printf("Bulk\n");
                break;
            case LIBUSB_TRANSFER_TYPE_INTERRUPT:
                printf("Interrupt\n");
                break;
        }
        
        printf("    Max Packet Size: %d bytes\n", endpoint->wMaxPacketSize);
        printf("    Interval: %d\n", endpoint->bInterval);
        
        // Save interrupt endpoints for later use
        if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
            if (direction) {
                in_endpoint = endpoint->bEndpointAddress;
                printf("    👉 This is the INPUT endpoint for controller data\n");
            } else {
                out_endpoint = endpoint->bEndpointAddress;
                printf("    👉 This is the OUTPUT endpoint for commands (rumble, etc.)\n");
            }
        }
        printf("\n");
    }
    
    libusb_free_config_descriptor(config);
    
    // Try to read some data from the IN endpoint
    if (in_endpoint != 0) {
        printf("Attempting to read from controller (endpoint 0x%02x)...\n", in_endpoint);
        printf("Press any button on your controller...\n\n");
        
        uint8_t buffer[64];
        int transferred;
        int packets_received = 0;
        
        for (int i = 0; i < 10; i++) {  // Try for 10 iterations
            result = libusb_interrupt_transfer(
                handle,
                in_endpoint,
                buffer,
                sizeof(buffer),
                &transferred,
                1000  // 1 second timeout
            );
            
            if (result == 0 && transferred > 0) {
                packets_received++;
                printf("📦 Received %d bytes: ", transferred);
                for (int j = 0; j < transferred && j < 32; j++) {
                    printf("%02x ", buffer[j]);
                }
                if (transferred > 32) {
                    printf("...");
                }
                printf("\n");
            } else if (result == LIBUSB_ERROR_TIMEOUT) {
                printf(".");
                fflush(stdout);
            } else {
                printf("\n⚠️  Read error: %s\n", libusb_error_name(result));
            }
        }
        
        printf("\n");
        if (packets_received > 0) {
            printf("✅ SUCCESS! Received %d packets from controller\n", packets_received);
            printf("   This means USB communication is working!\n");
            printf("   Next step: Parse the GIP protocol from these packets\n");
        } else {
            printf("⚠️  No data received. This might mean:\n");
            printf("   1. The controller needs an initialization sequence first\n");
            printf("   2. macOS is interfering with the device\n");
            printf("   3. The controller is in a different mode\n");
        }
    }
    
    // Cleanup
    printf("\nCleaning up...\n");
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    
    printf("\n✅ Test completed successfully!\n");
    printf("\nIf you saw packet data above, you're ready for Phase 3 (GIP protocol).\n");
    printf("If not, don't worry - Phase 3 will implement the initialization sequence.\n");
    
    return 0;
}
