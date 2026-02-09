/*******************************************************************************
 * driver.c - Main driver implementation
 ******************************************************************************/

#include "../../include/driver.h"
#include "../../include/config.h"
#include "../../include/input.h"
#include "../../include/log.h"
#include "../../include/thread.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

/*******************************************************************************
 * Global State
 ******************************************************************************/
static AtomicBool g_running;
static Mutex g_state_mutex;
static RWLock g_config_lock;

/*******************************************************************************
 * Rumble Callback
 ******************************************************************************/
static void rumble_callback(void *ctx) {
    DriverContext *driver = (DriverContext *)ctx;
    if (driver->config.features.rumble.enabled && driver->config.features.rumble.button_feedback) {
        usb_rumble_pulse(&driver->usb,
                         driver->config.features.rumble.intensity,
                         driver->config.features.rumble.duration_ms);
    }
}

/*******************************************************************************
 * Signal Handling
 ******************************************************************************/
static void signal_handler(int sig) {
    (void)sig;
    driver_request_stop();
    printf("\nShutting down...\n");
}

void driver_request_stop(void) {
    atomic_bool_set(&g_running, false);
}

bool driver_should_stop(void) {
    return !atomic_bool_get(&g_running);
}

/*******************************************************************************
 * Driver Initialization
 ******************************************************************************/
int driver_init(DriverContext *ctx, const char *config_path) {
    // Initialize globals
    atomic_bool_init(&g_running, true);
    mutex_init(&g_state_mutex);
    rwlock_init(&g_config_lock);

    // Initialize state
    input_state_init(&ctx->input_state);

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Xbox Controller to Keyboard/Mouse Simulator\n");
    printf("============================================\n\n");

    // Load configuration
    char loaded_path[512];
    config_load_auto(config_path, &ctx->config, loaded_path, sizeof(loaded_path));
    strncpy(ctx->config_path, loaded_path, sizeof(ctx->config_path));

    // Get initial modification time for hot-reload
    struct stat st;
    if (stat(ctx->config_path, &st) == 0) {
        ctx->config_last_modified = st.st_mtime;
    }

    // Initialize logging based on config
    log_init(ctx->config.log_level, NULL);
    ctx->verbose = ctx->config.console_output_enabled;

    printf("Config loaded from: %s\n", loaded_path);
    config_print(&ctx->config);
    printf("\n");

    printf("IMPORTANT: You may need to grant Accessibility permissions:\n");
    printf("   System Settings -> Privacy & Security -> Accessibility\n");
    printf("   Add Terminal (or your terminal app) to the list\n\n");

    // Initialize USB
    if (usb_init(&ctx->usb) != 0) {
        return -1;
    }

    return 0;
}

void driver_cleanup(DriverContext *ctx) {
    LOG_INFO("Cleaning up...");

    // Release all held keys/buttons
    input_state_release_all(&ctx->input_state);

    // Close USB
    usb_cleanup(&ctx->usb);

    // Cleanup logging
    log_cleanup();

    // Destroy locks
    mutex_destroy(&g_state_mutex);
    rwlock_destroy(&g_config_lock);

    printf("Simulator stopped cleanly!\n");
}

/*******************************************************************************
 * Input Loop
 ******************************************************************************/
void driver_input_loop(DriverContext *ctx) {
    uint8_t buffer[USB_BUFFER_SIZE];
    int transferred;
    int result;
    int input_count = 0;
    int hot_reload_counter = 0;

    printf("=== Xbox Controller Simulator Active ===\n");
    printf("Controller input is now being translated to keyboard/mouse\n");
    if (ctx->verbose) {
        printf("Console output: ENABLED (see input below)\n");
    } else {
        printf("Console output: DISABLED\n");
    }
    printf("Press Ctrl+C to exit\n\n");

    while (!driver_should_stop()) {
        result = usb_read_packet(&ctx->usb, buffer, sizeof(buffer), &transferred, USB_TIMEOUT_MS);

        if (result == 0 && transferred >= (int)sizeof(GipHeader)) {
            GipHeader *header = (GipHeader *)buffer;

            if (header->command == GIP_CMD_INPUT &&
                transferred >= (int)sizeof(GipInputPacket)) {
                GipInputPacket *input = (GipInputPacket *)buffer;
                input_count++;

                // Lock state for writing
                mutex_lock(&g_state_mutex);

                // Read config with read lock
                rwlock_read_lock(&g_config_lock);

                // Process input
                process_buttons(input->buttons, &ctx->input_state, &ctx->config,
                               rumble_callback, ctx);
                process_triggers(input->left_trigger, input->right_trigger,
                                &ctx->input_state, &ctx->config);
                process_sticks(input->left_stick_x, input->left_stick_y,
                              input->right_stick_x, input->right_stick_y,
                              &ctx->input_state, &ctx->config, ctx->config.streaming_mode);

                rwlock_read_unlock(&g_config_lock);
                mutex_unlock(&g_state_mutex);

                // Console output (if enabled)
                if (ctx->verbose) {
                    printf("\r[%04d] ", input_count);
                    printf("BTN: ");
                    if (input->buttons) {
                        print_buttons(input->buttons);
                    } else {
                        printf("none ");
                    }
                    printf("%-40s", "");
                    printf("\r[%04d] BTN: ", input_count);
                    print_buttons(input->buttons);
                    printf("| LT:%3d RT:%3d ", input->left_trigger, input->right_trigger);
                    printf("| LS:(%6d,%6d) RS:(%6d,%6d)  ",
                           input->left_stick_x, input->left_stick_y,
                           input->right_stick_x, input->right_stick_y);
                    fflush(stdout);
                }

            } else if (header->command == GIP_CMD_GUIDE_BUTTON && ctx->verbose) {
                printf("\n[GUIDE] Guide button pressed\n");
            }

        } else if (result == LIBUSB_ERROR_TIMEOUT) {
            // Timeout: Generate continuous movement from held stick positions
            mutex_lock(&g_state_mutex);
            rwlock_read_lock(&g_config_lock);
            generate_continuous_movement(&ctx->input_state, &ctx->config, ctx->config.streaming_mode);
            rwlock_read_unlock(&g_config_lock);
            mutex_unlock(&g_state_mutex);

        } else if (result == LIBUSB_ERROR_NO_DEVICE) {
            LOG_WARN("Controller disconnected!");

            // Attempt reconnection
            usb_close_device(&ctx->usb);

            for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS && !driver_should_stop(); attempt++) {
                LOG_INFO("Reconnection attempt %d/%d...", attempt, MAX_RECONNECT_ATTEMPTS);
                sleep(RECONNECT_DELAY_MS / 1000);

                if (usb_open_device(&ctx->usb) == 0) {
                    usb_initialize_controller(&ctx->usb, ctx->verbose);
                    LOG_INFO("Reconnected!");
                    break;
                }
            }

            if (!ctx->usb.connected) {
                LOG_ERROR("Failed to reconnect after %d attempts", MAX_RECONNECT_ATTEMPTS);
                break;
            }
        }

        // Check for config hot-reload every ~1 second (100 iterations at 10ms)
        if (++hot_reload_counter >= 100) {
            hot_reload_counter = 0;

            rwlock_write_lock(&g_config_lock);
            int reload_result = config_reload_if_changed(ctx->config_path, &ctx->config,
                                                          &ctx->config_last_modified);
            if (reload_result == 1) {
                ctx->verbose = ctx->config.console_output_enabled;
                log_set_level(ctx->config.log_level);
            }
            rwlock_write_unlock(&g_config_lock);
        }
    }

    printf("\n\n");
}

/*******************************************************************************
 * Main Driver Run
 ******************************************************************************/
int driver_run(DriverContext *ctx) {
    // Open device
    if (usb_open_device(&ctx->usb) != 0) {
        return -1;
    }

    // Initialize controller
    usb_initialize_controller(&ctx->usb, ctx->verbose);

    // Run input loop
    driver_input_loop(ctx);

    return 0;
}
