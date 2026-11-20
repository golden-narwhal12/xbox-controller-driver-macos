/*******************************************************************************
 * test_driver.c - The real driver threads against a fake controller
 *
 * Links the real src/core/driver.c (packet loop, motion thread, locking,
 * config reload, unplug handling) with a scripted USB layer and a recording
 * platform. Timing is real: the motion thread sleeps on the real clock.
 *
 * `make test-tsan` runs this under ThreadSanitizer. Every platform_post_*
 * below bumps a plain (non-atomic) counter, so if the driver ever posted
 * events from two threads without holding its output mutex, TSan reports it.
 ******************************************************************************/

#include "test_framework.h"
#include "../include/driver.h"
#include "../include/log.h"
#include "../include/platform.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/*******************************************************************************
 * Recording platform (thread-safe to read while the driver runs)
 ******************************************************************************/
static _Atomic int key_down[KEY_COUNT], key_up[KEY_COUNT];
static _Atomic int mouse_down[MOUSE_BUTTON_COUNT], mouse_up[MOUSE_BUTTON_COUNT];
static _Atomic int moves, tracker_enabled, tracker_running;
static int unsynchronized_posts;      // Deliberately plain: see the file header
static _Atomic uint64_t last_event;   // (keycode << 1) | pressed of the last key event

void platform_post_key(uint16_t keycode, bool pressed) {
    unsynchronized_posts++;
    atomic_fetch_add(&(pressed ? key_down : key_up)[keycode], 1);
    atomic_store(&last_event, ((uint64_t)keycode << 1) | pressed);
}

void platform_post_mouse_button(int button, bool pressed, ScreenPoint position, int click_count) {
    (void)position; (void)click_count;
    unsynchronized_posts++;
    atomic_fetch_add(&(pressed ? mouse_down : mouse_up)[button], 1);
}

void platform_post_mouse_move(ScreenPoint position, int delta_x, int delta_y, int drag_button) {
    (void)position; (void)delta_x; (void)delta_y; (void)drag_button;
    unsynchronized_posts++;
    atomic_fetch_add(&moves, 1);
}

void platform_post_scroll(int lines) { (void)lines; unsynchronized_posts++; }
ScreenPoint platform_cursor_position(void) { return (ScreenPoint){500, 500}; }
void platform_warp_cursor(ScreenPoint position) { (void)position; }
bool platform_point_on_display(ScreenPoint position) { (void)position; return true; }
void platform_target_window(WindowTarget *out) { memset(out, 0, sizeof(*out)); }
void platform_events_cleanup(void) {}

uint64_t platform_now_us(void) {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000u;
}

void platform_sleep_until_us(uint64_t deadline_us) {
    uint64_t now = platform_now_us();
    if (deadline_us > now) usleep((useconds_t)(deadline_us - now));
}

int window_tracker_start(void) { atomic_store(&tracker_running, 1); return 0; }
void window_tracker_stop(void) { atomic_store(&tracker_running, 0); }
void window_tracker_set_enabled(bool enabled) { atomic_store(&tracker_enabled, enabled); }
void window_tracker_poke(void) {}

/*******************************************************************************
 * Scripted USB layer
 ******************************************************************************/
#define SCRIPT_CAPACITY 4096

/* One scripted result of usb_read_packet: the bytes delivered (if any) and
   the libusb status returned with them. */
static pthread_mutex_t script_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    uint8_t bytes[USB_BUFFER_SIZE];
    int length;
    int status;
} script[SCRIPT_CAPACITY];
static int script_head, script_tail;
static _Atomic int rumbles, guide_acks, opens, closes;

static void script_push_result(const uint8_t *bytes, int length, int status) {
    pthread_mutex_lock(&script_lock);
    if (length > 0) memcpy(script[script_tail].bytes, bytes, (size_t)length);
    script[script_tail].length = length;
    script[script_tail].status = status;
    script_tail = (script_tail + 1) % SCRIPT_CAPACITY;
    pthread_mutex_unlock(&script_lock);
}

static void script_push(const uint8_t *bytes, int length) {
    script_push_result(bytes, length, 0);
}

static void put_le16(uint8_t *p, int value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
}

/* Queue one input report, as raw bytes. Stick Y is +up, as on the wire. */
static void send_report(uint16_t buttons, int left_trigger, int right_trigger,
                        int left_x, int left_y, int right_x, int right_y) {
    uint8_t report[GIP_INPUT_LENGTH] = {GIP_CMD_INPUT, 0, 0, GIP_INPUT_LENGTH - 4};
    put_le16(report + GIP_INPUT_OFF_BUTTONS, buttons);
    put_le16(report + GIP_INPUT_OFF_LEFT_TRIGGER, left_trigger);
    put_le16(report + GIP_INPUT_OFF_RIGHT_TRIGGER, right_trigger);
    put_le16(report + GIP_INPUT_OFF_LEFT_X, left_x);
    put_le16(report + GIP_INPUT_OFF_LEFT_Y, left_y);
    put_le16(report + GIP_INPUT_OFF_RIGHT_X, right_x);
    put_le16(report + GIP_INPUT_OFF_RIGHT_Y, right_y);
    script_push(report, sizeof(report));
}

int usb_init(UsbContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&ctx->connected, false);
    return 0;
}

void usb_cleanup(UsbContext *ctx) { (void)ctx; }

int usb_open_device(UsbContext *ctx) {
    atomic_fetch_add(&opens, 1);
    ctx->connected = true;
    return 0;
}

void usb_close_device(UsbContext *ctx) {
    atomic_fetch_add(&closes, 1);
    ctx->connected = false;
}

int usb_initialize_controller(UsbContext *ctx, bool verbose) {
    (void)ctx; (void)verbose;
    return 0;
}

int usb_read_packet(UsbContext *ctx, uint8_t *buffer, int size, int *transferred, int timeout_ms) {
    (void)ctx; (void)size; (void)timeout_ms;
    *transferred = 0;
    pthread_mutex_lock(&script_lock);
    bool have = script_head != script_tail;
    int length = 0, status = LIBUSB_ERROR_TIMEOUT;
    if (have) {
        length = script[script_head].length;
        status = script[script_head].status;
        if (length > 0) memcpy(buffer, script[script_head].bytes, (size_t)length);
        script_head = (script_head + 1) % SCRIPT_CAPACITY;
    }
    pthread_mutex_unlock(&script_lock);

    if (!have) usleep(1000);                // an idle controller: the read times out
    *transferred = length;
    return status;
}

int usb_ack_guide_button(UsbContext *ctx, uint8_t sequence) {
    (void)ctx; (void)sequence;
    atomic_fetch_add(&guide_acks, 1);
    return 0;
}

int usb_rumble_pulse(UsbContext *ctx, uint8_t intensity, uint16_t duration_ms) {
    (void)ctx; (void)intensity; (void)duration_ms;
    atomic_fetch_add(&rumbles, 1);
    return 0;
}

void print_buttons(uint16_t buttons) { (void)buttons; }
const char* libusb_error_name(int code) { (void)code; return "fake error"; }

/*******************************************************************************
 * Harness
 ******************************************************************************/
static char g_config_path[256];
static DriverContext g_ctx;
static pthread_t g_thread;
static int g_run_result;
static _Atomic int g_status_connected, g_status_disconnected;
static _Atomic int g_w_up_at_disconnect;

static void on_status(DriverStatus status) {
    if (status == DRIVER_STATUS_CONNECTED) atomic_fetch_add(&g_status_connected, 1);
    if (status == DRIVER_STATUS_DISCONNECTED) {
        atomic_store(&g_w_up_at_disconnect, atomic_load(&key_up[KEY_W]));
        atomic_fetch_add(&g_status_disconnected, 1);
    }
}

/* Waits (up to 5 s) for an atomic counter to reach a value. */
static bool wait_for(_Atomic int *counter, int at_least) {
    for (int i = 0; i < 5000; i++) {
        if (atomic_load(counter) >= at_least) return true;
        usleep(1000);
    }
    return false;
}

static void *driver_thread(void *arg) {
    (void)arg;
    g_run_result = driver_run(&g_ctx);
    return NULL;
}

static void write_config(const char *json) {
    FILE *f = fopen(g_config_path, "w");
    if (f) {
        fputs(json, f);
        fclose(f);
    }
}

/* The driver prints a banner and its config; keep test output readable. */
static int silence_stdout(void) {
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);
    close(devnull);
    return saved;
}

static void restore_stdout(int saved) {
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
}

static bool start_driver(const char *config_json) {
    for (int i = 0; i < KEY_COUNT; i++) {
        atomic_store(&key_down[i], 0);
        atomic_store(&key_up[i], 0);
    }
    for (int i = 0; i < MOUSE_BUTTON_COUNT; i++) {
        atomic_store(&mouse_down[i], 0);
        atomic_store(&mouse_up[i], 0);
    }
    atomic_store(&moves, 0);
    atomic_store(&rumbles, 0);
    atomic_store(&guide_acks, 0);
    atomic_store(&opens, 0);
    atomic_store(&closes, 0);
    atomic_store(&g_status_connected, 0);
    atomic_store(&g_status_disconnected, 0);
    pthread_mutex_lock(&script_lock);
    script_head = script_tail = 0;
    pthread_mutex_unlock(&script_lock);

    write_config(config_json);
    int saved = silence_stdout();
    bool ok = driver_init(&g_ctx, g_config_path) == 0;
    if (ok) {
        driver_set_status_callback(&g_ctx, on_status);
        ok = pthread_create(&g_thread, NULL, driver_thread, NULL) == 0;
    }
    // Let the "driver active" banner go to /dev/null as well
    if (ok) ok = wait_for(&g_status_connected, 1);
    usleep(100000);
    restore_stdout(saved);
    return ok;
}

static void stop_driver(void) {
    int saved = silence_stdout();
    driver_request_stop();
    pthread_join(g_thread, NULL);
    driver_cleanup(&g_ctx);
    restore_stdout(saved);
}

#define BASE_CONFIG \
    "\"advanced\":{\"console_output\":false,\"log_level\":\"none\",\"confine_cursor\":\"off\"}"

/*******************************************************************************
 * Tests
 ******************************************************************************/

/* Reports become events; the motion thread turns a held stick into motion on
   its own clock; and nothing stays held after shutdown. */
TEST(input_motion_and_clean_shutdown) {
    ASSERT_TRUE(start_driver("{" BASE_CONFIG "}"));
    ASSERT_TRUE(wait_for(&g_status_connected, 1));

    // A + left stick forward + right stick right + RT, in one report
    send_report(XBOX_BTN_A, 0, 1023, 0, 30000, 30000, 0);
    ASSERT_TRUE(wait_for(&key_down[KEY_SPACE], 1));
    ASSERT_TRUE(wait_for(&key_down[KEY_W], 1));
    ASSERT_TRUE(wait_for(&mouse_down[MOUSE_BUTTON_LEFT], 1));
    ASSERT_TRUE(wait_for(&rumbles, 1));

    // The same state reported again (and with the stick nudged) is not a new press
    send_report(XBOX_BTN_A, 0, 1023, 0, 30000, 30000, 0);
    send_report(XBOX_BTN_A, 0, 1023, 0, 30500, 30000, 0);

    // No more reports arrive, yet the look stick keeps moving the mouse
    int before = atomic_load(&moves);
    ASSERT_TRUE(wait_for(&moves, before + 20));

    // Still held: no stray releases while waiting
    ASSERT_EQ(atomic_load(&key_up[KEY_SPACE]), 0);
    ASSERT_EQ(atomic_load(&key_up[KEY_W]), 0);
    ASSERT_EQ(atomic_load(&mouse_up[MOUSE_BUTTON_LEFT]), 0);

    stop_driver();
    ASSERT_EQ(g_run_result, 0);
    ASSERT_EQ(atomic_load(&key_up[KEY_SPACE]), 1);       // released on the way out
    ASSERT_EQ(atomic_load(&key_up[KEY_W]), 1);
    ASSERT_EQ(atomic_load(&mouse_up[MOUSE_BUTTON_LEFT]), 1);
    ASSERT_EQ(atomic_load(&rumbles), 1);
    ASSERT_EQ(atomic_load(&tracker_running), 0);
    int after_stop = atomic_load(&moves);
    usleep(50000);
    ASSERT_EQ(atomic_load(&moves), after_stop);          // the motion thread is gone
    TEST_PASS();
}

/* A flood of reports while the motion thread is posting: events from the two
   threads must come out in a consistent order (every key ends up released,
   downs and ups pair up), and nothing is lost. */
TEST(packet_and_motion_threads_interleave_safely) {
    ASSERT_TRUE(start_driver("{\"buttons\":{\"a\":\"w\",\"b\":\"q\"},"
                             "\"left_stick\":{\"mode\":\"wasd_pwm\",\"pwm_period_ms\":30,"
                             "\"pwm_min_pulse_ms\":5}," BASE_CONFIG "}"));
    ASSERT_TRUE(wait_for(&g_status_connected, 1));

    // PWM pulses W from the motion thread while reports toggle A (also W) and
    // wiggle both sticks from the packet thread.
    for (int round = 0; round < 60; round++) {
        for (int i = 0; i < 20; i++) {
            send_report((i % 2 ? XBOX_BTN_A : 0) | (i % 3 ? XBOX_BTN_B : 0), 0, 0,
                        0, 18000 + i * 300, 20000 + i * 100, 0);
        }
        usleep(5000);
    }
    send_report(0, 0, 0, 0, 0, 0, 0);

    // Everything queued gets processed, then all keys are up
    for (int i = 0; i < 5000; i++) {
        pthread_mutex_lock(&script_lock);
        bool drained = script_head == script_tail;
        pthread_mutex_unlock(&script_lock);
        if (drained) break;
        usleep(1000);
    }
    usleep(100000);
    stop_driver();

    for (int key = 0; key < KEY_COUNT; key++) {
        ASSERT_EQ(atomic_load(&key_down[key]), atomic_load(&key_up[key]));
    }
    ASSERT(atomic_load(&key_down[0x0C]) > 100);           // Q, from the B button
    ASSERT(atomic_load(&key_down[KEY_W]) > 5);
    ASSERT(atomic_load(&moves) > 10);
    ASSERT_EQ(atomic_load(&last_event) & 1, 0);           // the final key event is a release
    TEST_PASS();
}

/* Reload from the menu while a button and the stick are held: the old keys
   are released, the new bindings apply to what is still held, and a broken
   file changes nothing. */
TEST(reload_while_held) {
    ASSERT_TRUE(start_driver("{" BASE_CONFIG "}"));
    ASSERT_TRUE(wait_for(&g_status_connected, 1));
    send_report(XBOX_BTN_A, 0, 1023, 0, 30000, 0, 0);
    ASSERT_TRUE(wait_for(&key_down[KEY_SPACE], 1));
    ASSERT_TRUE(wait_for(&key_down[KEY_W], 1));
    ASSERT_TRUE(wait_for(&mouse_down[MOUSE_BUTTON_LEFT], 1));

    write_config("{\"buttons\":{\"a\":\"f\"},\"left_stick\":{\"mode\":\"arrows\"},"
                 "\"advanced\":{\"console_output\":false,\"log_level\":\"none\","
                 "\"confine_cursor\":\"window\"}}");
    driver_request_reload();
    ASSERT_TRUE(wait_for(&key_up[KEY_SPACE], 1));
    ASSERT_TRUE(wait_for(&key_up[KEY_W], 1));
    ASSERT_TRUE(wait_for(&key_down[KEY_F], 1));            // no new report was needed
    ASSERT_TRUE(wait_for(&key_down[KEY_UP_ARROW], 1));
    // Everything is released, including a binding the reload did not change
    // (RT is still the left click): it is let go and pressed again.
    ASSERT_TRUE(wait_for(&mouse_up[MOUSE_BUTTON_LEFT], 1));
    ASSERT_TRUE(wait_for(&mouse_down[MOUSE_BUTTON_LEFT], 2));
    ASSERT_TRUE(wait_for(&tracker_enabled, 1));            // confinement setting applied
    ASSERT_EQ(atomic_load(&rumbles), 1);                   // re-applying is not a new press

    write_config("{\"buttons\":{\"a\":\"q\"");             // broken
    driver_request_reload();
    usleep(300000);
    ASSERT_EQ(atomic_load(&key_up[KEY_F]), 0);             // still held, still F

    send_report(0, 0, 0, 0, 0, 0, 0);
    ASSERT_TRUE(wait_for(&key_up[KEY_F], 1));
    ASSERT_TRUE(wait_for(&key_up[KEY_UP_ARROW], 1));
    stop_driver();
    ASSERT_EQ(atomic_load(&key_down[KEY_F]), 1);
    ASSERT_EQ(atomic_load(&key_up[KEY_F]), 1);
    TEST_PASS();
}

/* Unplug: everything is released before the reconnect attempts start, and a
   stop request interrupts the 5 s reconnect wait promptly. */
TEST(unplug_releases_everything) {
    ASSERT_TRUE(start_driver("{" BASE_CONFIG "}"));
    ASSERT_TRUE(wait_for(&g_status_connected, 1));
    send_report(0, 1023, 0, 0, 30000, 0, 0);
    ASSERT_TRUE(wait_for(&key_down[KEY_W], 1));
    ASSERT_TRUE(wait_for(&mouse_down[MOUSE_BUTTON_RIGHT], 1));

    script_push_result(NULL, 0, LIBUSB_ERROR_NO_DEVICE);
    ASSERT_TRUE(wait_for(&g_status_disconnected, 1));
    ASSERT_EQ(atomic_load(&g_w_up_at_disconnect), 1);      // released before the status change
    ASSERT_EQ(atomic_load(&mouse_up[MOUSE_BUTTON_RIGHT]), 1);
    ASSERT_EQ(atomic_load(&closes), 1);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    stop_driver();
    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
    ASSERT(seconds < 1.0);
    ASSERT_EQ(g_run_result, 0);
    ASSERT_EQ(atomic_load(&key_up[KEY_W]), 1);             // not released twice
    TEST_PASS();
}

/* Non-input packets: the guide button is acknowledged; short and unknown
   packets are ignored. */
TEST(other_packets) {
    ASSERT_TRUE(start_driver("{" BASE_CONFIG "}"));
    ASSERT_TRUE(wait_for(&g_status_connected, 1));

    const uint8_t guide[] = {GIP_CMD_GUIDE_BUTTON, GIP_OPT_ACK | GIP_OPT_INTERNAL, 5, 2, 1, 0x5B};
    const uint8_t guide_no_ack[] = {GIP_CMD_GUIDE_BUTTON, GIP_OPT_INTERNAL, 6, 2, 0, 0x5B};
    const uint8_t runt[] = {GIP_CMD_INPUT, 0};
    const uint8_t short_input[] = {GIP_CMD_INPUT, 0, 1, 4, 0x10, 0, 0, 0};   // A, but truncated
    const uint8_t unknown[20] = {0x7F};
    script_push(guide, sizeof(guide));
    script_push(guide_no_ack, sizeof(guide_no_ack));
    script_push(runt, sizeof(runt));
    script_push(short_input, sizeof(short_input));
    script_push(unknown, sizeof(unknown));
    send_report(XBOX_BTN_B, 0, 0, 0, 0, 0, 0);
    ASSERT_TRUE(wait_for(&key_down[KEY_C], 1));            // the loop got past all of them

    ASSERT_EQ(atomic_load(&guide_acks), 1);
    ASSERT_EQ(atomic_load(&key_down[KEY_SPACE]), 0);       // the truncated report did nothing
    stop_driver();
    TEST_PASS();
}

/* libusb can report a timeout and still hand over a complete report (the
   report arrived while the read was being cancelled). It must not be lost. */
TEST(report_delivered_with_timeout_status) {
    ASSERT_TRUE(start_driver("{" BASE_CONFIG "}"));
    uint8_t report[GIP_INPUT_LENGTH] = {GIP_CMD_INPUT, 0, 0, GIP_INPUT_LENGTH - 4};
    put_le16(report + GIP_INPUT_OFF_BUTTONS, XBOX_BTN_X);
    script_push_result(report, sizeof(report), LIBUSB_ERROR_TIMEOUT);
    ASSERT_TRUE(wait_for(&key_down[KEY_R], 1));
    stop_driver();
    TEST_PASS();
}

/* Read errors other than "no device": a few are ridden out; an unbroken run
   of them is treated as an unplug (keys released, reconnect) instead of
   spinning on a dead handle forever. */
TEST(persistent_read_errors_become_disconnect) {
    ASSERT_TRUE(start_driver("{" BASE_CONFIG "}"));
    send_report(XBOX_BTN_A, 0, 0, 0, 0, 0, 0);
    ASSERT_TRUE(wait_for(&key_down[KEY_SPACE], 1));

    for (int i = 0; i < 5; i++) script_push_result(NULL, 0, LIBUSB_ERROR_IO);
    send_report(XBOX_BTN_A | XBOX_BTN_B, 0, 0, 0, 0, 0, 0);
    ASSERT_TRUE(wait_for(&key_down[KEY_C], 1));            // still alive after a glitch
    ASSERT_EQ(atomic_load(&g_status_disconnected), 0);
    ASSERT_EQ(atomic_load(&key_up[KEY_SPACE]), 0);

    for (int i = 0; i < 40; i++) script_push_result(NULL, 0, LIBUSB_ERROR_PIPE);
    ASSERT_TRUE(wait_for(&g_status_disconnected, 1));
    ASSERT_EQ(atomic_load(&key_up[KEY_SPACE]), 1);
    ASSERT_EQ(atomic_load(&key_up[KEY_C]), 1);
    stop_driver();
    TEST_PASS();
}

int main(void) {
    log_set_level(LOG_LEVEL_NONE);
    snprintf(g_config_path, sizeof(g_config_path), "/tmp/xbox-driver-test-%d.json", (int)getpid());

    TEST_SUITE("Driver Threads");
    RUN_TEST(input_motion_and_clean_shutdown);
    RUN_TEST(packet_and_motion_threads_interleave_safely);
    RUN_TEST(reload_while_held);
    RUN_TEST(unplug_releases_everything);
    RUN_TEST(other_packets);
    RUN_TEST(report_delivered_with_timeout_status);
    RUN_TEST(persistent_read_errors_become_disconnect);
    TEST_SUMMARY();

    unlink(g_config_path);
    (void)unsynchronized_posts;
    return TEST_EXIT_CODE();
}
