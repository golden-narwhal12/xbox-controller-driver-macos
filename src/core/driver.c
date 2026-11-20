/*******************************************************************************
 * driver.c - Main driver: threads, locking and the USB packet loop
 *
 * Threads
 *   packet thread   Reads USB reports, translates them, reloads the config,
 *                   handles unplug/reconnect, sends rumble. The only thread
 *                   that touches the libusb handle.
 *   motion thread   Runs motion_tick() every MOTION_TICK_MS on absolute
 *                   deadlines (stick-to-mouse, scroll repeat).
 *   window tracker  (src/platform/window.c) Refreshes the target window.
 *
 * Locks, in the only order they may be taken:
 *   1. g_state_mutex   ctx->input_state, ctx->config, ctx->last_input.
 *                      Held only for pure computation: no USB, no file I/O,
 *                      no CoreGraphics or Accessibility calls.
 *   2. g_output_mutex  ctx->cursor and all event posting (src/event/event.c).
 *   3. leaf locks      g_stats_mutex here, the tracker's snapshot lock, and
 *                      the logger. Nothing else is taken while holding one.
 *
 * A thread turns input into events under the state mutex, then takes the
 * output mutex *before* dropping the state mutex and posts the events
 * (post_events_and_unlock). That hand-over keeps events in the order the
 * state changes happened, even though two threads produce them, while the
 * slow part (posting) never blocks the other thread's computation.
 * A tick holds the state mutex throughout, so a config reload can never
 * change the config in the middle of one.
 ******************************************************************************/

#include "../../include/driver.h"
#include "../../include/input.h"
#include "../../include/platform.h"
#include "../../include/tick.h"
#include "../../include/log.h"
#include <pthread/qos.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_POLL_INTERVAL_US   1000000u   // Check the config file's mtime
#define STATS_INTERVAL_US         1000000u   // Debug statistics period
#define STOP_POLL_INTERVAL_US     100000u    // Granularity of interruptible waits
#define MAX_CONSECUTIVE_USB_ERRORS 20        // Then treat the device as gone
#define USB_ERROR_BACKOFF_US      10000u

/*******************************************************************************
 * Global State
 ******************************************************************************/
static atomic_bool g_running;
static atomic_bool g_reload_requested;
static pthread_mutex_t g_state_mutex;
static pthread_mutex_t g_output_mutex;
static pthread_mutex_t g_stats_mutex;
static DurationStats g_tick_period_stats;    // Guarded by g_stats_mutex

/*******************************************************************************
 * Requests
 ******************************************************************************/
static void signal_handler(int sig) {
    (void)sig;
    atomic_store(&g_running, false);   // lock-free store: async-signal-safe
}

void driver_request_stop(void) {
    atomic_store(&g_running, false);
}

void driver_request_reload(void) {
    atomic_store(&g_reload_requested, true);
}

bool driver_should_stop(void) {
    return !atomic_load(&g_running);
}

/* Sleeps up to duration_us, returning early (false) if a stop is requested. */
static bool sleep_unless_stopped(uint64_t duration_us) {
    for (uint64_t slept = 0; slept < duration_us; slept += STOP_POLL_INTERVAL_US) {
        if (driver_should_stop()) return false;
        usleep(STOP_POLL_INTERVAL_US);
    }
    return !driver_should_stop();
}

/*******************************************************************************
 * Posting
 ******************************************************************************/

/* Call with g_state_mutex held; returns with it released. See the file header
   for why the output mutex is taken before the state mutex is dropped. */
static void post_events_and_unlock(DriverContext *ctx, const OutQueue *queue) {
    if (queue->count == 0) {
        pthread_mutex_unlock(&g_state_mutex);
        return;
    }
    CursorConfig cursor_config = ctx->config.cursor;   // config is state-guarded
    pthread_mutex_lock(&g_output_mutex);
    pthread_mutex_unlock(&g_state_mutex);
    cursor_dispatch(&ctx->cursor, &cursor_config, queue);
    pthread_mutex_unlock(&g_output_mutex);
    if (queue->dropped) LOG_ERROR("Output queue overflow: %d events lost", queue->dropped);
}

static void release_everything(DriverContext *ctx) {
    OutQueue queue;
    out_queue_init(&queue);
    pthread_mutex_lock(&g_state_mutex);
    input_state_release_all(&ctx->input_state, &queue);
    ctx->have_input = false;
    post_events_and_unlock(ctx, &queue);
}

/*******************************************************************************
 * Motion thread
 ******************************************************************************/
static void *motion_thread_main(void *arg) {
    DriverContext *ctx = arg;
    // Ask for prompt wakeups: this thread sets the feel of mouse motion
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    TickSched sched;
    tick_sched_init(&sched, platform_now_us(), MOTION_TICK_US);

    while (!driver_should_stop()) {
        platform_sleep_until_us(tick_sched_deadline(&sched));
        uint64_t elapsed_us = tick_sched_step(&sched, platform_now_us());
        if (elapsed_us == 0) continue;   // woke early; nothing is due yet

        OutQueue queue;
        out_queue_init(&queue);
        pthread_mutex_lock(&g_state_mutex);
        // dt scaling inside motion_tick absorbs whatever jitter remains
        motion_tick(&ctx->input_state, &ctx->config, (float)elapsed_us / 1000.0f, &queue);
        post_events_and_unlock(ctx, &queue);

        pthread_mutex_lock(&g_stats_mutex);
        duration_stats_add(&g_tick_period_stats, elapsed_us);
        pthread_mutex_unlock(&g_stats_mutex);
    }
    return NULL;
}

/*******************************************************************************
 * Initialization and cleanup
 ******************************************************************************/
int driver_init(DriverContext *ctx, const char *config_path) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&g_running, true);
    atomic_init(&g_reload_requested, false);
    pthread_mutex_init(&g_state_mutex, NULL);
    pthread_mutex_init(&g_output_mutex, NULL);
    pthread_mutex_init(&g_stats_mutex, NULL);
    duration_stats_reset(&g_tick_period_stats);

    input_state_init(&ctx->input_state);
    cursor_state_init(&ctx->cursor, (int)getpid());

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    printf("Xbox Controller to Keyboard/Mouse Simulator\n");
    printf("============================================\n\n");

    if (config_load_auto(config_path, &ctx->config, ctx->config_path,
                         sizeof(ctx->config_path)) != 0) {
        goto fail;
    }

    // Initial modification time for hot-reload
    struct stat st;
    if (stat(ctx->config_path, &st) == 0) {
        ctx->config_last_modified = st.st_mtimespec;
    }

    log_init(ctx->config.log_level, NULL);
    ctx->verbose = ctx->config.console_output_enabled;

    printf("Config loaded from: %s\n", ctx->config_path);
    config_print(&ctx->config);
    printf("\n");

    printf("IMPORTANT: You may need to grant Accessibility permissions:\n");
    printf("   System Settings -> Privacy & Security -> Accessibility\n");
    printf("   Add Terminal (or your terminal app) to the list\n\n");

    if (usb_init(&ctx->usb) != 0) {
        log_cleanup();
        goto fail;
    }

    // Failure is not fatal: without a tracker the cursor is confined to displays
    window_tracker_start();
    window_tracker_set_enabled(ctx->config.cursor.confine == CONFINE_WINDOW);
    return 0;

fail:
    pthread_mutex_destroy(&g_state_mutex);
    pthread_mutex_destroy(&g_output_mutex);
    pthread_mutex_destroy(&g_stats_mutex);
    return -1;
}

void driver_set_status_callback(DriverContext *ctx, DriverStatusCallback callback) {
    ctx->status_callback = callback;
}

static void report_status(DriverContext *ctx, DriverStatus status) {
    if (ctx->status_callback) ctx->status_callback(status);
}

void driver_cleanup(DriverContext *ctx) {
    LOG_INFO("Cleaning up...");

    driver_request_stop();
    if (ctx->motion_thread_started) {
        pthread_join(ctx->motion_thread, NULL);
        ctx->motion_thread_started = false;
    }

    // Nothing may stay held after we exit
    release_everything(ctx);
    platform_events_cleanup();

    window_tracker_stop();
    usb_cleanup(&ctx->usb);
    log_cleanup();

    pthread_mutex_destroy(&g_state_mutex);
    pthread_mutex_destroy(&g_output_mutex);
    pthread_mutex_destroy(&g_stats_mutex);

    printf("Simulator stopped cleanly!\n");
}

/*******************************************************************************
 * Packet handling
 ******************************************************************************/
static void print_input(int count, const GipInput *input) {
    printf("\r[%04d] BTN: ", count);
    if (input->buttons) print_buttons(input->buttons);
    else printf("none ");
    printf("| LT:%4d RT:%4d | LS:(%6d,%6d) RS:(%6d,%6d)      ",
           input->left_trigger, input->right_trigger,
           input->left_stick_x, input->left_stick_y,
           input->right_stick_x, input->right_stick_y);
    fflush(stdout);
}

static void handle_input(DriverContext *ctx, const GipInput *input) {
    OutQueue queue;
    out_queue_init(&queue);

    pthread_mutex_lock(&g_state_mutex);
    int press_edges = process_input(input, &ctx->input_state, &ctx->config, &queue);
    ctx->last_input = *input;
    ctx->have_input = true;
    RumbleConfig rumble = ctx->config.rumble;
    post_events_and_unlock(ctx, &queue);

    // USB write happens with no lock held
    if (press_edges > 0 && rumble.enabled && rumble.button_feedback) {
        usb_rumble_pulse(&ctx->usb, rumble.intensity, rumble.duration_ms);
    }
}

/*******************************************************************************
 * Config reload
 ******************************************************************************/
static void reload_config(DriverContext *ctx, bool force) {
    if (force) ctx->config_last_modified = (struct timespec){0};

    // Read and parse with no lock held; only the swap needs the state mutex
    ControllerMapping next;
    if (config_reload_if_changed(ctx->config_path, &next, &ctx->config_last_modified) != 1) {
        return;
    }

    OutQueue queue;
    out_queue_init(&queue);
    pthread_mutex_lock(&g_state_mutex);
    // Release under the old bindings, then apply the new ones to whatever is
    // still physically held (the controller only reports changes, so a held
    // stick would otherwise stay dead until it next moved).
    input_state_release_all(&ctx->input_state, &queue);
    ctx->config = next;
    if (ctx->have_input) {
        process_input(&ctx->last_input, &ctx->input_state, &ctx->config, &queue);
    }
    post_events_and_unlock(ctx, &queue);

    ctx->verbose = next.console_output_enabled;
    log_set_level(next.log_level);
    window_tracker_set_enabled(next.cursor.confine == CONFINE_WINDOW);
}

/*******************************************************************************
 * Reconnect
 ******************************************************************************/
static bool reconnect(DriverContext *ctx) {
    release_everything(ctx);
    usb_close_device(&ctx->usb);
    report_status(ctx, DRIVER_STATUS_DISCONNECTED);

    for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; attempt++) {
        LOG_INFO("Reconnection attempt %d/%d...", attempt, MAX_RECONNECT_ATTEMPTS);
        if (!sleep_unless_stopped((uint64_t)RECONNECT_DELAY_MS * 1000u)) return false;

        if (usb_open_device(&ctx->usb) != 0) continue;
        if (usb_initialize_controller(&ctx->usb, ctx->verbose) != 0) {
            LOG_WARN("Reconnect: controller init failed, retrying...");
            usb_close_device(&ctx->usb);
            continue;
        }
        LOG_INFO("Reconnected!");
        report_status(ctx, DRIVER_STATUS_CONNECTED);
        return true;
    }

    LOG_ERROR("Failed to reconnect after %d attempts", MAX_RECONNECT_ATTEMPTS);
    return false;
}

/*******************************************************************************
 * Debug statistics (log_level "debug"): lets the owner check, on hardware,
 * the idle packet rate, how steady the motion tick is, and how long a report
 * takes to become posted events.
 ******************************************************************************/
typedef struct {
    uint64_t window_start_us;
    unsigned packets;
    DurationStats latency;    // report received -> its events posted
} PacketStats;

static void report_stats(PacketStats *stats, uint64_t now_us) {
    if (now_us - stats->window_start_us < STATS_INTERVAL_US) return;

    pthread_mutex_lock(&g_stats_mutex);
    DurationStats ticks = g_tick_period_stats;
    duration_stats_reset(&g_tick_period_stats);
    pthread_mutex_unlock(&g_stats_mutex);

    double seconds = (double)(now_us - stats->window_start_us) / 1e6;
    LOG_DEBUG("stats: %.0f packets/s | tick period min/avg/max %llu/%llu/%llu us (%llu ticks)"
              " | packet-to-post latency avg/max %llu/%llu us",
              stats->packets / seconds,
              (unsigned long long)ticks.min,
              (unsigned long long)duration_stats_average(&ticks),
              (unsigned long long)ticks.max, (unsigned long long)ticks.count,
              (unsigned long long)duration_stats_average(&stats->latency),
              (unsigned long long)stats->latency.max);

    stats->window_start_us = now_us;
    stats->packets = 0;
    duration_stats_reset(&stats->latency);
}

/*******************************************************************************
 * Packet loop
 ******************************************************************************/
static int packet_loop(DriverContext *ctx) {
    uint8_t buffer[USB_BUFFER_SIZE];
    int input_count = 0;
    int consecutive_errors = 0;
    uint64_t last_config_poll_us = platform_now_us();
    PacketStats stats = {.window_start_us = last_config_poll_us};
    duration_stats_reset(&stats.latency);

    printf("=== Xbox Controller Simulator Active ===\n");
    printf("Controller input is now being translated to keyboard/mouse\n");
    printf("Console output: %s\n", ctx->verbose ? "ENABLED (see input below)" : "DISABLED");
    printf("Press Ctrl+C to exit\n\n");

    while (!driver_should_stop()) {
        int transferred = 0;
        int result = usb_read_packet(&ctx->usb, buffer, sizeof(buffer), &transferred,
                                     USB_READ_TIMEOUT_MS);
        uint64_t received_us = platform_now_us();

        // A read that timed out can still have delivered a report
        bool have_data = (result == 0 || result == LIBUSB_ERROR_TIMEOUT) &&
                         transferred >= (int)sizeof(GipHeader);
        if (result == 0 || result == LIBUSB_ERROR_TIMEOUT) consecutive_errors = 0;

        if (have_data) {
            const GipHeader *header = (const GipHeader *)buffer;
            GipInput input;
            stats.packets++;

            if (gip_parse_input(buffer, (size_t)transferred, &input)) {
                handle_input(ctx, &input);
                duration_stats_add(&stats.latency, platform_now_us() - received_us);
                if (ctx->verbose) print_input(++input_count, &input);
            } else if (header->command == GIP_CMD_GUIDE_BUTTON) {
                if (header->options == (GIP_OPT_ACK | GIP_OPT_INTERNAL)) {
                    usb_ack_guide_button(&ctx->usb, header->sequence);
                }
                if (ctx->verbose) printf("\n[GUIDE] Guide button pressed\n");
            }
        } else if (result != 0 && result != LIBUSB_ERROR_TIMEOUT) {
            bool gone = result == LIBUSB_ERROR_NO_DEVICE;
            if (!gone) {
                // Don't spin on an error that returns immediately, and don't
                // keep a dead handle forever.
                if (consecutive_errors == 0) {
                    LOG_WARN("USB read failed: %s", libusb_error_name(result));
                }
                gone = ++consecutive_errors >= MAX_CONSECUTIVE_USB_ERRORS;
                if (!gone) usleep(USB_ERROR_BACKOFF_US);
            }
            if (gone) {
                LOG_WARN("Controller disconnected!");
                consecutive_errors = 0;
                if (!reconnect(ctx)) {
                    if (driver_should_stop()) break;
                    report_status(ctx, DRIVER_STATUS_FAILED);
                    return -1;
                }
            }
        }

        uint64_t now_us = platform_now_us();
        bool force_reload = atomic_exchange(&g_reload_requested, false);
        if (force_reload || now_us - last_config_poll_us >= CONFIG_POLL_INTERVAL_US) {
            last_config_poll_us = now_us;
            reload_config(ctx, force_reload);
        }
        if (log_get_level() == LOG_LEVEL_DEBUG) report_stats(&stats, now_us);
    }

    printf("\n\n");
    return 0;
}

/*******************************************************************************
 * Main Driver Run
 ******************************************************************************/
int driver_run(DriverContext *ctx) {
    if (usb_open_device(&ctx->usb) != 0) {
        report_status(ctx, DRIVER_STATUS_FAILED);
        return -1;
    }

    if (usb_initialize_controller(&ctx->usb, ctx->verbose) != 0) {
        LOG_ERROR("Failed to initialize controller");
        usb_close_device(&ctx->usb);
        report_status(ctx, DRIVER_STATUS_FAILED);
        return -1;
    }

    if (pthread_create(&ctx->motion_thread, NULL, motion_thread_main, ctx) != 0) {
        LOG_ERROR("Failed to start the motion thread");
        report_status(ctx, DRIVER_STATUS_FAILED);
        return -1;
    }
    ctx->motion_thread_started = true;
    report_status(ctx, DRIVER_STATUS_CONNECTED);

    return packet_loop(ctx);
}
