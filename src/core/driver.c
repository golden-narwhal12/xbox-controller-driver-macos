/*******************************************************************************
 * driver.c - Main driver implementation
 ******************************************************************************/

#include "../../include/driver.h"
#include "../../include/config.h"
#include "../../include/input.h"
#include "../../include/event.h"
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
static AtomicBool g_reload_requested;
static Mutex g_state_mutex;
static RWLock g_config_lock;

/*******************************************************************************
 * Rumble Callback
 ******************************************************************************/
static void rumble_callback(void *ctx) {
    DriverContext *driver = (DriverContext *)ctx;
    if (driver->config.features.rumble.enabled && driver->config.features.rumble.button_feedback) {
        driver->rumble_pending = true;
    }
}

/*******************************************************************************
 * Signal Handling
 ******************************************************************************/
static void signal_handler(int sig) {
    (void)sig;
    driver_request_stop();
}

void driver_request_stop(void) {
    atomic_bool_set(&g_running, false);
}

void driver_request_reload(void) {
    atomic_bool_set(&g_reload_requested, true);
}

bool driver_should_stop(void) {
    return !atomic_bool_get(&g_running);
}

/*******************************************************************************
 * Driver Initialization
 ******************************************************************************/
int driver_init(DriverContext *ctx, const char *config_path) {
    memset(ctx, 0, sizeof(*ctx));
    // Initialize globals
    atomic_bool_init(&g_running, true);
    atomic_bool_init(&g_reload_requested, false);
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
    if (config_load_auto(config_path, &ctx->config, loaded_path, sizeof(loaded_path)) != 0) {
        mutex_destroy(&g_state_mutex);
        rwlock_destroy(&g_config_lock);
        return -1;
    }
    snprintf(ctx->config_path, sizeof(ctx->config_path), "%s", loaded_path);

    // Get initial modification time for hot-reload
    struct stat st;
    if (stat(ctx->config_path, &st) == 0) {
        ctx->config_last_modified = st.st_mtimespec;
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
        log_cleanup();
        mutex_destroy(&g_state_mutex);
        rwlock_destroy(&g_config_lock);
        return -1;
    }

    return 0;
}

void driver_cleanup(DriverContext *ctx) {
    LOG_INFO("Cleaning up...");

    // Release all held keys/buttons
    input_state_release_all(&ctx->input_state);
    event_cleanup();

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
static uint64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int driver_input_loop(DriverContext *ctx) {
    uint8_t buffer[USB_BUFFER_SIZE];
    int transferred;
    int result;
    int input_count = 0;
    uint64_t last_motion = monotonic_ms();
    uint64_t last_reload = last_motion;

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
                GipInputPacket packet;
                memcpy(&packet, buffer, sizeof(packet));
                GipInputPacket *input = &packet;
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

                bool rumble_pending = ctx->rumble_pending;
                ctx->rumble_pending = false;
                uint8_t rumble_intensity = ctx->config.features.rumble.intensity;
                uint16_t rumble_duration = ctx->config.features.rumble.duration_ms;

                rwlock_read_unlock(&g_config_lock);
                mutex_unlock(&g_state_mutex);

                if (rumble_pending)
                    usb_rumble_pulse(&ctx->usb, rumble_intensity, rumble_duration);

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

            } else if (header->command == GIP_CMD_GUIDE_BUTTON) {
                if (header->options == 0x30) usb_ack_guide_button(&ctx->usb, header->sequence);
                if (ctx->verbose) printf("\n[GUIDE] Guide button pressed\n");
            }

        } else if (result == LIBUSB_ERROR_NO_DEVICE) {
            LOG_WARN("Controller disconnected!");

            mutex_lock(&g_state_mutex);
            input_state_release_all(&ctx->input_state);
            mutex_unlock(&g_state_mutex);

            // Attempt reconnection
            usb_close_device(&ctx->usb);

            for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS && !driver_should_stop(); attempt++) {
                LOG_INFO("Reconnection attempt %d/%d...", attempt, MAX_RECONNECT_ATTEMPTS);
                sleep(RECONNECT_DELAY_MS / 1000);

                if (usb_open_device(&ctx->usb) == 0) {
                    if (usb_initialize_controller(&ctx->usb, ctx->verbose) != 0) {
                        LOG_WARN("Reconnect: controller init failed, retrying...");
                        usb_close_device(&ctx->usb);
                        continue;
                    }
                    LOG_INFO("Reconnected!");
                    break;
                }
            }

            if (!ctx->usb.connected) {
                LOG_ERROR("Failed to reconnect after %d attempts", MAX_RECONNECT_ATTEMPTS);
                return -1;
            }
        }

        uint64_t now = monotonic_ms();
        if (now - last_motion >= 10) {
            last_motion = now;
            mutex_lock(&g_state_mutex);
            rwlock_read_lock(&g_config_lock);
            generate_continuous_movement(&ctx->input_state, &ctx->config, ctx->config.streaming_mode);
            rwlock_read_unlock(&g_config_lock);
            mutex_unlock(&g_state_mutex);
        }

        bool force_reload = atomic_exchange(&g_reload_requested.value, false);
        if (force_reload || now - last_reload >= 1000) {
            last_reload = now;
            mutex_lock(&g_state_mutex);
            rwlock_write_lock(&g_config_lock);
            struct timespec previous_modified = ctx->config_last_modified;
            if (force_reload) ctx->config_last_modified = (struct timespec){0};
            int reload_result = config_reload_if_changed(ctx->config_path, &ctx->config,
                                                          &ctx->config_last_modified);
            if (reload_result == 1) {
                input_state_release_all(&ctx->input_state);
                ctx->verbose = ctx->config.console_output_enabled;
                log_set_level(ctx->config.log_level);
            } else if (force_reload) {
                ctx->config_last_modified = previous_modified;
            }
            rwlock_write_unlock(&g_config_lock);
            mutex_unlock(&g_state_mutex);
        }
    }

    printf("\n\n");
    return 0;
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
    if (usb_initialize_controller(&ctx->usb, ctx->verbose) != 0) {
        LOG_ERROR("Failed to initialize controller");
        usb_close_device(&ctx->usb);
        return -1;
    }

    // Run input loop
    return driver_input_loop(ctx);
}
