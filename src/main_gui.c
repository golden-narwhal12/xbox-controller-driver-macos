/*******************************************************************************
 * main_gui.c - Entry point for the Xbox Controller Simulator with GUI
 ******************************************************************************/

#include "../include/driver.h"
#include "../include/menubar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/*******************************************************************************
 * Global State
 ******************************************************************************/
static DriverContext g_ctx;
static pthread_t g_driver_thread;
static int g_driver_result;    // Written by the driver thread, read after join

/*******************************************************************************
 * Callbacks
 ******************************************************************************/
static void on_reload_config(void *context) {
    (void)context;
    printf("Reloading configuration...\n");
    driver_request_reload();
}

static void on_quit(void *context) {
    (void)context;
    printf("Quit requested from menu bar\n");
    driver_request_stop();
}

/* Called on the driver thread; the menu bar functions hop to the main thread. */
static void on_driver_status(DriverStatus status) {
    switch (status) {
        case DRIVER_STATUS_CONNECTED:
            menubar_set_status("Connected");
            menubar_set_connected(true);
            break;
        case DRIVER_STATUS_DISCONNECTED:
            menubar_set_status("Disconnected - reconnecting...");
            menubar_set_connected(false);
            break;
        case DRIVER_STATUS_FAILED:
            menubar_set_status("Error - check console");
            menubar_set_connected(false);
            break;
    }
}

/*******************************************************************************
 * Driver Thread
 ******************************************************************************/
static void *driver_thread_func(void *arg) {
    (void)arg;
    g_driver_result = driver_run(&g_ctx);

    // The driver is done (error, signal or quit): end the menu bar loop so
    // main() can clean up.
    menubar_quit();
    return NULL;
}

/*******************************************************************************
 * Main
 ******************************************************************************/
static void print_usage(const char *program) {
    printf("Usage: %s [OPTIONS]\n\n", program);
    printf("Xbox Controller Driver with Menu Bar GUI\n\n");
    printf("Options:\n");
    printf("  --config <path>    Load configuration from specified file\n");
    printf("  --help             Show this help message\n");
}

int main(int argc, char *argv[]) {
    const char *config_path = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Initialize driver (but don't run yet)
    if (driver_init(&g_ctx, config_path) != 0) {
        fprintf(stderr, "Failed to initialize driver\n");
        return 1;
    }

    // Initialize menu bar
    menubar_init(&g_ctx, on_reload_config, on_quit);
    menubar_set_status("Initializing...");
    driver_set_status_callback(&g_ctx, on_driver_status);

    // Start driver in background thread
    if (pthread_create(&g_driver_thread, NULL, driver_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to start driver thread\n");
        driver_cleanup(&g_ctx);
        return 1;
    }

    // Run the menu bar event loop on the main thread (required by AppKit).
    // Returns when the user quits or the driver thread ends.
    menubar_run();

    // Stop the driver, then release held keys and close the device
    driver_request_stop();
    pthread_join(g_driver_thread, NULL);
    driver_cleanup(&g_ctx);

    return g_driver_result;
}
