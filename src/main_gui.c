/*******************************************************************************
 * main_gui.c - Entry point for the Xbox Controller Simulator with GUI
 ******************************************************************************/

#include "../include/driver.h"
#include "../include/menubar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/*******************************************************************************
 * Global State
 ******************************************************************************/
static DriverContext g_ctx;
static pthread_t g_driver_thread;
static bool g_driver_running = false;

/*******************************************************************************
 * Callbacks
 ******************************************************************************/
static void on_reload_config(void *context) {
    (void)context;
    printf("Reloading configuration...\n");

    // The driver's hot-reload mechanism will pick this up
    // Force reload by touching the modification time
    time_t now = time(NULL);
    g_ctx.config_last_modified = now - 10;  // Force reload check
}

static void on_quit(void *context) {
    (void)context;
    printf("Quit requested from menu bar\n");
    driver_request_stop();
}

/*******************************************************************************
 * Driver Thread
 ******************************************************************************/
static void *driver_thread_func(void *arg) {
    (void)arg;

    // Run the driver
    int result = driver_run(&g_ctx);

    if (result != 0) {
        menubar_set_status("Error - Check console");
    }

    g_driver_running = false;

    // Request quit if driver exits
    menubar_quit();

    return NULL;
}

static void start_driver_thread(void) {
    g_driver_running = true;
    pthread_create(&g_driver_thread, NULL, driver_thread_func, NULL);
}

/*******************************************************************************
 * Status Update Thread
 ******************************************************************************/
static void *status_thread_func(void *arg) {
    (void)arg;

    while (g_driver_running) {
        if (g_ctx.usb.connected) {
            menubar_set_status("Connected");
            menubar_set_connected(true);
        } else {
            menubar_set_status("Disconnected");
            menubar_set_connected(false);
        }

        usleep(500000);  // Update every 500ms
    }

    return NULL;
}

static void start_status_thread(void) {
    pthread_t status_thread;
    pthread_create(&status_thread, NULL, status_thread_func, NULL);
    pthread_detach(status_thread);
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

    // Start driver in background thread
    start_driver_thread();

    // Start status update thread
    start_status_thread();

    // Run menu bar event loop (blocks until quit)
    // This must be on the main thread for macOS
    menubar_run();

    // Wait for driver thread to finish
    if (g_driver_running) {
        driver_request_stop();
        pthread_join(g_driver_thread, NULL);
    }

    // Cleanup
    driver_cleanup(&g_ctx);

    return 0;
}
