/*******************************************************************************
 * test_config.c - Configuration loading, validation and hot reload (K14)
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"
#include "../include/log.h"
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

/*******************************************************************************
 * Helpers
 ******************************************************************************/
static char g_dir[256];          // Scratch directory for this run
static char g_path[512];         // Scratch config file inside it
static char g_start_dir[1024];   // Where the test was started (the repo root)

static bool write_bytes(const char *path, const char *data, size_t length) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = write(fd, data, length) == (ssize_t)length;
    close(fd);
    return ok;
}

static bool write_text(const char *path, const char *json) {
    return write_bytes(path, json, strlen(json));
}

/* Writes `json` to the scratch file and loads it. */
static int load_json(const char *json, ControllerMapping *mapping) {
    if (!write_text(g_path, json)) return -99;
    return config_load(g_path, mapping);
}

static bool is_defaults(const ControllerMapping *mapping) {
    ControllerMapping defaults;
    config_get_defaults(&defaults);
    return memcmp(mapping, &defaults, sizeof(defaults)) == 0;
}

/* Sets a file's modification time to a given second, so reload tests do not
   depend on the filesystem's timestamp resolution or on sleeping. */
static void set_mtime(const char *path, long seconds) {
    struct timeval times[2] = {{seconds, 0}, {seconds, 0}};
    utimes(path, times);
}

/* Every value the rest of the driver relies on being in range. */
static bool in_range(const ControllerMapping *m) {
    for (int i = 0; i < SOURCE_COUNT; i++) {
        uint16_t b = m->bindings[i];
        if (b >= KEY_COUNT && b != BINDING_NONE && (b < BINDING_SCROLL_UP || b > BINDING_MOUSE_LEFT)) {
            return false;
        }
    }
    for (int i = 0; i < STICK_COUNT; i++) {
        const StickConfig *s = &m->stick[i];
        if (s->mode < STICK_MODE_WASD || s->mode > STICK_MODE_DISABLED) return false;
        if (s->deadzone < 0 || s->deadzone > MAX_DEADZONE) return false;
        if (!(s->threshold >= 0.01f && s->threshold <= 0.95f)) return false;
        if (!(s->hysteresis >= 0.0f && s->hysteresis <= 0.5f)) return false;
        if (!(s->pwm_period_ms >= MIN_PWM_PERIOD_MS && s->pwm_period_ms <= MAX_PWM_PERIOD_MS)) return false;
        if (!(s->pwm_full_on > s->threshold && s->pwm_full_on <= 1.0f)) return false;
        if (!(s->pwm_min_pulse_ms >= MIN_PWM_MIN_PULSE_MS &&
              s->pwm_min_pulse_ms <= s->pwm_period_ms / 2.0f)) return false;
    }
    if (!(m->mouse.sensitivity >= 0.05f && m->mouse.sensitivity <= 100.0f)) return false;
    if (!(m->mouse.curve >= 0.1f && m->mouse.curve <= 10.0f)) return false;
    if (!(m->mouse.smoothing >= 0.0f && m->mouse.smoothing <= MAX_SMOOTHING)) return false;
    if (m->cursor.confine < CONFINE_WINDOW || m->cursor.confine > CONFINE_OFF) return false;
    if (!(m->cursor.margin >= 0.0f && m->cursor.margin <= 200.0f)) return false;
    if (!(m->cursor.top_inset >= 0.0f && m->cursor.top_inset <= 400.0f)) return false;
    if (m->log_level < LOG_LEVEL_DEBUG || m->log_level > LOG_LEVEL_NONE) return false;
    return true;
}

/*******************************************************************************
 * Defaults
 ******************************************************************************/
TEST(defaults_bindings) {
    ControllerMapping m;
    config_get_defaults(&m);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("space"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_B], config_parse_key("c"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_X], config_parse_key("r"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_Y], config_parse_key("f"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_LB], BINDING_SCROLL_UP);
    ASSERT_EQ(m.bindings[SOURCE_BTN_RB], BINDING_SCROLL_DOWN);
    ASSERT_EQ(m.bindings[SOURCE_BTN_LS], config_parse_key("left_shift"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_RS], config_parse_key("left_control"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_VIEW], config_parse_key("tab"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_MENU], config_parse_key("escape"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_DPAD_UP], config_parse_key("up"));
    ASSERT_EQ(m.bindings[SOURCE_LEFT_TRIGGER], BINDING_MOUSE_RIGHT);
    ASSERT_EQ(m.bindings[SOURCE_RIGHT_TRIGGER], BINDING_MOUSE_LEFT);
    ASSERT_EQ(m.bindings[SOURCE_LEFT_STICK + STICK_DIR_UP], config_parse_key("w"));
    ASSERT_EQ(m.bindings[SOURCE_LEFT_STICK + STICK_DIR_LEFT], config_parse_key("a"));
    ASSERT_EQ(m.bindings[SOURCE_RIGHT_STICK + STICK_DIR_UP], config_parse_key("i"));
    // Every source has a default: nothing is left as keycode 0 ("a") by accident
    int bound_to_a = 0;
    for (int i = 0; i < SOURCE_COUNT; i++) bound_to_a += m.bindings[i] == 0;
    ASSERT_EQ(bound_to_a, 1);                    // only left stick "left"
    TEST_PASS();
}

TEST(defaults_values) {
    ControllerMapping m;
    config_get_defaults(&m);
    ASSERT_EQ(m.stick[STICK_LEFT].mode, STICK_MODE_WASD);
    ASSERT_FALSE(m.stick[STICK_LEFT].pwm);
    ASSERT_EQ(m.stick[STICK_RIGHT].mode, STICK_MODE_MOUSE);
    ASSERT_EQ(m.stick[STICK_LEFT].deadzone, 8000);
    ASSERT_EQ(m.stick[STICK_RIGHT].deadzone, 8000);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].threshold, 0.30f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].hysteresis, 0.05f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_period_ms, 100.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_full_on, 0.90f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_min_pulse_ms, 20.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.sensitivity, 1.5f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.curve, 1.8f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.smoothing, 0.3f, 0.0001f);
    ASSERT_EQ(m.triggers.threshold, 127);
    ASSERT_EQ(m.triggers.hysteresis, 26);
    ASSERT_TRUE(m.rumble.enabled);
    ASSERT_EQ(m.cursor.confine, CONFINE_WINDOW);
    ASSERT_FLOAT_EQ(m.cursor.margin, 12.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.cursor.top_inset, 28.0f, 0.0001f);
    ASSERT_FALSE(m.cursor.recenter_when_captured);
    ASSERT_FALSE(m.cursor.streaming_mode);
    ASSERT_TRUE(in_range(&m));
    TEST_PASS();
}

/* config/controller.json documents the defaults: loading it must give exactly
   the built-in defaults, so the two can never drift apart. */
TEST(shipped_config_matches_defaults) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/config/controller.json", g_start_dir);
    ControllerMapping m;
    ASSERT_EQ(config_load(path, &m), 0);
    ASSERT_TRUE(is_defaults(&m));
    TEST_PASS();
}

/*******************************************************************************
 * Key names
 ******************************************************************************/
TEST(parse_key_names) {
    ASSERT_EQ(config_parse_key("a"), 0x00);
    ASSERT_EQ(config_parse_key("A"), 0x00);      // case insensitive
    ASSERT_EQ(config_parse_key("z"), 0x06);
    ASSERT_EQ(config_parse_key("w"), 0x0D);
    ASSERT_EQ(config_parse_key("space"), 0x31);
    ASSERT_EQ(config_parse_key("SPACE"), 0x31);
    ASSERT_EQ(config_parse_key("return"), 0x24);
    ASSERT_EQ(config_parse_key("enter"), 0x24);
    ASSERT_EQ(config_parse_key("esc"), 0x35);
    ASSERT_EQ(config_parse_key("left_shift"), 0x38);
    ASSERT_EQ(config_parse_key("shift"), 0x38);
    ASSERT_EQ(config_parse_key("ctrl"), 0x3B);
    ASSERT_EQ(config_parse_key("alt"), 0x3A);
    ASSERT_EQ(config_parse_key("up"), 0x7E);
    ASSERT_EQ(config_parse_key("down"), 0x7D);
    ASSERT_EQ(config_parse_key("left"), 0x7B);
    ASSERT_EQ(config_parse_key("right"), 0x7C);
    ASSERT_EQ(config_parse_key("f12"), 0x6F);
    ASSERT_EQ(config_parse_key("mouse_left"), BINDING_MOUSE_LEFT);
    ASSERT_EQ(config_parse_key("mouse_middle"), BINDING_MOUSE_MIDDLE);
    ASSERT_EQ(config_parse_key("scroll_down"), BINDING_SCROLL_DOWN);
    TEST_PASS();
}

TEST(parse_key_hex_and_invalid) {
    ASSERT_EQ(config_parse_key("0x31"), 0x31);
    ASSERT_EQ(config_parse_key("0X7e"), 0x7E);
    ASSERT_EQ(config_parse_key("0xFF"), 0xFF);
    ASSERT_EQ(config_parse_key("0x100"), BINDING_NONE);     // not a keycode
    ASSERT_EQ(config_parse_key("0xFFFE"), BINDING_NONE);    // cannot smuggle a mouse binding
    ASSERT_EQ(config_parse_key("0x"), BINDING_NONE);
    ASSERT_EQ(config_parse_key("0x1g"), BINDING_NONE);
    ASSERT_EQ(config_parse_key("-0x1"), BINDING_NONE);
    ASSERT_EQ(config_parse_key("invalid_key_name"), BINDING_NONE);
    ASSERT_EQ(config_parse_key(""), BINDING_NONE);
    ASSERT_EQ(config_parse_key(NULL), BINDING_NONE);

    // A name longer than any key name is unknown, not truncated into a match
    char long_name[200];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    ASSERT_EQ(config_parse_key(long_name), BINDING_NONE);
    char prefixed[80] = "space";
    memset(prefixed + 5, 'x', 70);
    prefixed[75] = '\0';
    ASSERT_EQ(config_parse_key(prefixed), BINDING_NONE);
    TEST_PASS();
}

TEST(key_names_round_trip) {
    ASSERT_STR_EQ(config_key_name(0x00), "a");
    ASSERT_STR_EQ(config_key_name(0x31), "space");
    ASSERT_STR_EQ(config_key_name(0x7E), "up");
    ASSERT_STR_EQ(config_key_name(BINDING_MOUSE_RIGHT), "mouse_right");
    ASSERT_STR_EQ(config_key_name(BINDING_NONE), "none");
    ASSERT_STR_EQ(config_key_name(0xC8), "unknown");
    for (uint16_t key = 0; key < KEY_COUNT; key++) {
        const char *name = config_key_name(key);
        if (strcmp(name, "unknown") != 0) ASSERT_EQ(config_parse_key(name), key);
    }
    TEST_PASS();
}

/*******************************************************************************
 * Loading: sections and backward compatibility
 ******************************************************************************/
TEST(buttons_and_sticks_load) {
    ControllerMapping m;
    ASSERT_EQ(load_json("{\"buttons\":{\"dpad_up\":\"f1\",\"a\":\"mouse_left\",\"y\":\"0x24\"},"
                        "\"left_stick\":{\"deadzone\":9000,\"up\":\"t\"},"
                        "\"right_stick\":{\"mode\":\"arrows\",\"up\":\"u\"}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_DPAD_UP], config_parse_key("f1"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], BINDING_MOUSE_LEFT);
    ASSERT_EQ(m.bindings[SOURCE_BTN_Y], 0x24);
    ASSERT_EQ(m.bindings[SOURCE_BTN_B], config_parse_key("c"));          // untouched
    ASSERT_EQ(m.stick[STICK_LEFT].mode, STICK_MODE_WASD);
    ASSERT_EQ(m.bindings[SOURCE_LEFT_STICK + STICK_DIR_UP], config_parse_key("t"));
    ASSERT_EQ(m.stick[STICK_RIGHT].mode, STICK_MODE_ARROWS);
    ASSERT_EQ(m.bindings[SOURCE_RIGHT_STICK + STICK_DIR_UP], config_parse_key("u"));
    // "up" inside a stick is that stick's key, not the d-pad or the other stick
    ASSERT_EQ(m.bindings[SOURCE_RIGHT_STICK + STICK_DIR_DOWN], config_parse_key("k"));
    TEST_PASS();
}

/* left_stick.deadzone has always set both sticks; right_stick.deadzone is new. */
TEST(deadzone_shared_unless_overridden) {
    ControllerMapping m;
    ASSERT_EQ(load_json("{\"left_stick\":{\"deadzone\":9000}}", &m), 0);
    ASSERT_EQ(m.stick[STICK_LEFT].deadzone, 9000);
    ASSERT_EQ(m.stick[STICK_RIGHT].deadzone, 9000);

    ASSERT_EQ(load_json("{\"left_stick\":{\"deadzone\":9000},"
                        "\"right_stick\":{\"deadzone\":4000}}", &m), 0);
    ASSERT_EQ(m.stick[STICK_LEFT].deadzone, 9000);
    ASSERT_EQ(m.stick[STICK_RIGHT].deadzone, 4000);

    ASSERT_EQ(load_json("{\"right_stick\":{\"deadzone\":4000}}", &m), 0);
    ASSERT_EQ(m.stick[STICK_LEFT].deadzone, DEFAULT_DEADZONE);
    ASSERT_EQ(m.stick[STICK_RIGHT].deadzone, 4000);
    TEST_PASS();
}

/* Triggers accept any key or mouse name; old "left": "mouse_left" configs too. */
TEST(trigger_bindings) {
    ControllerMapping m;
    ASSERT_EQ(load_json("{\"triggers\":{\"left\":\"mouse_left\",\"right\":\"space\"}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_LEFT_TRIGGER], BINDING_MOUSE_LEFT);
    ASSERT_EQ(m.bindings[SOURCE_RIGHT_TRIGGER], config_parse_key("space"));

    ASSERT_EQ(load_json("{\"triggers\":{\"left\":\"disabled\"}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_LEFT_TRIGGER], BINDING_NONE);
    ASSERT_EQ(m.bindings[SOURCE_RIGHT_TRIGGER], BINDING_MOUSE_LEFT);     // default kept

    // Buttons and stick directions can be unbound the same way
    ASSERT_EQ(load_json("{\"buttons\":{\"a\":\"none\",\"b\":\"disabled\"},"
                        "\"left_stick\":{\"down\":\"none\"}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], BINDING_NONE);
    ASSERT_EQ(m.bindings[SOURCE_BTN_B], BINDING_NONE);
    ASSERT_EQ(m.bindings[SOURCE_LEFT_STICK + STICK_DIR_DOWN], BINDING_NONE);
    TEST_PASS();
}

/* Configs written for older versions still load. */
TEST(old_configs_still_load) {
    ControllerMapping m;
    ASSERT_EQ(load_json("{\"features\":{\"rumble\":{\"enabled\":false},"
                        "\"turbo\":{\"enabled\":true,\"rate\":10},"
                        "\"analog_keyboard\":{\"enabled\":false}}}", &m), 0);
    ASSERT_FALSE(m.rumble.enabled);

    // The config file shipped before the overhaul, verbatim
    ASSERT_EQ(load_json(
        "{\n  \"buttons\": {\n    \"a\": \"space\",\n    \"b\": \"c\",\n    \"x\": \"r\",\n"
        "    \"y\": \"f\",\n    \"lb\": \"scroll_up\",\n    \"rb\": \"scroll_down\",\n"
        "    \"ls\": \"left_shift\",\n    \"rs\": \"left_control\",\n    \"view\": \"tab\",\n"
        "    \"menu\": \"escape\"\n  },\n"
        "  \"left_stick\": {\n    \"mode\": \"wasd\",\n    \"deadzone\": 8000\n  },\n"
        "  \"right_stick\": {\n    \"mode\": \"mouse\",\n    \"sensitivity\": 1.5,\n"
        "    \"curve\": 1.8,\n    \"smoothing\": 0.3\n  },\n"
        "  \"triggers\": {\n    \"left\": \"mouse_right\",\n    \"right\": \"mouse_left\",\n"
        "    \"threshold\": 127\n  },\n"
        "  \"features\": {\n    \"rumble\": {\n      \"enabled\": true,\n"
        "      \"button_feedback\": true\n    }\n  },\n"
        "  \"advanced\": {\n    \"log_level\": \"info\",\n    \"streaming_mode\": false,\n"
        "    \"console_output\": true\n  }\n}\n", &m), 0);
    ASSERT_TRUE(is_defaults(&m));
    TEST_PASS();
}

/* Every option added by the overhaul, set to a non-default value, loads. */
TEST(new_options_round_trip) {
    ControllerMapping m;
    ASSERT_EQ(load_json(
        "{\"left_stick\":{\"mode\":\"wasd_pwm\",\"threshold\":0.4,\"hysteresis\":0.1,"
        "\"pwm_period_ms\":150,\"pwm_full_on\":0.8,\"pwm_min_pulse_ms\":30},"
        "\"right_stick\":{\"mode\":\"arrows_pwm\",\"threshold\":0.2,\"hysteresis\":0.02,"
        "\"pwm_period_ms\":60,\"pwm_full_on\":0.95,\"pwm_min_pulse_ms\":10,\"deadzone\":5000,"
        "\"sensitivity\":2.5,\"curve\":1.2,\"smoothing\":0.5},"
        "\"triggers\":{\"threshold\":200,\"hysteresis\":40},"
        "\"features\":{\"rumble\":{\"enabled\":false,\"button_feedback\":false,"
        "\"intensity\":50,\"duration_ms\":120}},"
        "\"advanced\":{\"confine_cursor\":\"display\",\"confine_margin\":6,"
        "\"confine_top_inset\":40,\"recenter_when_captured\":true,"
        "\"streaming_mode\":true,\"console_output\":false,\"log_level\":\"debug\"}}", &m), 0);

    const StickConfig *left = &m.stick[STICK_LEFT], *right = &m.stick[STICK_RIGHT];
    ASSERT_EQ(left->mode, STICK_MODE_WASD);
    ASSERT_TRUE(left->pwm);
    ASSERT_FLOAT_EQ(left->threshold, 0.4f, 0.0001f);
    ASSERT_FLOAT_EQ(left->hysteresis, 0.1f, 0.0001f);
    ASSERT_FLOAT_EQ(left->pwm_period_ms, 150.0f, 0.0001f);
    ASSERT_FLOAT_EQ(left->pwm_full_on, 0.8f, 0.0001f);
    ASSERT_FLOAT_EQ(left->pwm_min_pulse_ms, 30.0f, 0.0001f);
    ASSERT_EQ(left->deadzone, DEFAULT_DEADZONE);
    ASSERT_EQ(right->mode, STICK_MODE_ARROWS);
    ASSERT_TRUE(right->pwm);
    ASSERT_FLOAT_EQ(right->threshold, 0.2f, 0.0001f);
    ASSERT_FLOAT_EQ(right->hysteresis, 0.02f, 0.0001f);
    ASSERT_FLOAT_EQ(right->pwm_period_ms, 60.0f, 0.0001f);
    ASSERT_FLOAT_EQ(right->pwm_full_on, 0.95f, 0.0001f);
    ASSERT_FLOAT_EQ(right->pwm_min_pulse_ms, 10.0f, 0.0001f);
    ASSERT_EQ(right->deadzone, 5000);
    ASSERT_FLOAT_EQ(m.mouse.sensitivity, 2.5f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.curve, 1.2f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.smoothing, 0.5f, 0.0001f);
    ASSERT_EQ(m.triggers.threshold, 200);
    ASSERT_EQ(m.triggers.hysteresis, 40);
    ASSERT_FALSE(m.rumble.enabled);
    ASSERT_FALSE(m.rumble.button_feedback);
    ASSERT_EQ(m.rumble.intensity, 50);
    ASSERT_EQ(m.rumble.duration_ms, 120);
    ASSERT_EQ(m.cursor.confine, CONFINE_DISPLAY);
    ASSERT_FLOAT_EQ(m.cursor.margin, 6.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.cursor.top_inset, 40.0f, 0.0001f);
    ASSERT_TRUE(m.cursor.recenter_when_captured);
    ASSERT_TRUE(m.cursor.streaming_mode);
    ASSERT_FALSE(m.console_output_enabled);
    ASSERT_EQ(m.log_level, LOG_LEVEL_DEBUG);
    ASSERT_TRUE(in_range(&m));

    ASSERT_EQ(load_json("{\"advanced\":{\"confine_cursor\":\"off\"}}", &m), 0);
    ASSERT_EQ(m.cursor.confine, CONFINE_OFF);
    ASSERT_EQ(load_json("{\"advanced\":{\"confine_cursor\":\"window\"},"
                        "\"left_stick\":{\"mode\":\"disabled\"}}", &m), 0);
    ASSERT_EQ(m.cursor.confine, CONFINE_WINDOW);
    ASSERT_EQ(m.stick[STICK_LEFT].mode, STICK_MODE_DISABLED);
    ASSERT_FALSE(m.stick[STICK_LEFT].pwm);
    TEST_PASS();
}

/*******************************************************************************
 * Robustness
 ******************************************************************************/
TEST(malformed_json_is_rejected) {
    const char *bad[] = {
        "",                                   // empty file
        "   \n\t ",                           // only whitespace
        "[]",                                 // not an object
        "\"buttons\"",
        "42",
        "null",
        "{",
        "}",
        "{\"buttons\":}",
        "{\"buttons\":{\"a\":\"space\"}",     // missing closing brace
        "{\"buttons\":{\"a\":\"space\"}}}",   // one brace too many
        "{\"buttons\":{\"a\":\"space\"}} x",  // trailing garbage
        "{\"buttons\":{\"a\":\"space\",}}{}", // two documents
        "{\"a\" \"b\"}",                      // missing colon
        "{\"a\":1 \"b\":2}",                  // missing comma
        "{a:1}",                              // unquoted key
        "{'a':1}",                            // single quotes
        "{\"a\":\"unterminated}",
        "{\"a\":tru}",
        "{\"a\":nan}",                        // strtod would accept these
        "{\"a\":inf}",
        "{\"a\":-inf}",
        "{\"a\":infinity}",
        "{\"a\":0x10}",
        "{\"a\":1e999}",                      // overflows to infinity
        "{\"a\":+1}",
        "{\"a\":.5}",
        "{\"a\":[1,2}",                       // mismatched brackets
        "{\"a\":{\"b\":[}]}",
        "{\"a\":1,,\"b\":2}",
        "{,}",
        "{\"left_stick\":{\"deadzone\":nan}}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ControllerMapping m;
        memset(&m, 0xAB, sizeof(m));
        if (load_json(bad[i], &m) != -1) {
            printf("\n         accepted: %s ", bad[i]);
            ASSERT(false);
        }
        ASSERT_TRUE(is_defaults(&m));         // a rejected file leaves the defaults
    }
    TEST_PASS();
}

/* A file cut off at any byte is rejected cleanly (never half-applied). */
TEST(truncated_file_is_rejected) {
    const char *full =
        "{\"buttons\":{\"a\":\"f1\",\"b\":\"mouse_left\"},"
        "\"left_stick\":{\"mode\":\"wasd_pwm\",\"deadzone\":9000,\"threshold\":0.4},"
        "\"right_stick\":{\"sensitivity\":2.5,\"curve\":1.2},"
        "\"triggers\":{\"left\":\"disabled\",\"threshold\":200},"
        "\"features\":{\"rumble\":{\"enabled\":false}},"
        "\"advanced\":{\"confine_cursor\":\"off\",\"streaming_mode\":true}}";
    size_t length = strlen(full);
    ControllerMapping m;
    for (size_t cut = 0; cut < length; cut++) {
        ASSERT_TRUE(write_bytes(g_path, full, cut));
        memset(&m, 0xAB, sizeof(m));
        ASSERT_EQ(config_load(g_path, &m), -1);
        ASSERT_TRUE(is_defaults(&m));
    }
    ASSERT_TRUE(write_bytes(g_path, full, length));
    ASSERT_EQ(config_load(g_path, &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("f1"));
    ASSERT_EQ(m.cursor.confine, CONFINE_OFF);
    TEST_PASS();
}

TEST(unknown_keys_are_ignored) {
    ControllerMapping m;
    ASSERT_EQ(load_json("{\"future_section\":{\"x\":[1,{\"y\":null},\"z\"]},"
                        "\"buttons\":{\"paddle_1\":\"q\",\"a\":\"q\",\"extra\":{\"a\":\"w\"}},"
                        "\"left_stick\":{\"wobble\":true,\"deadzone\":7000},"
                        "\"comment\":\"buttons: {a: e}\","
                        "\"advanced\":{\"telemetry\":false}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("q"));
    ASSERT_EQ(m.stick[STICK_LEFT].deadzone, 7000);
    // Nothing else moved
    ControllerMapping expected;
    config_get_defaults(&expected);
    expected.bindings[SOURCE_BTN_A] = config_parse_key("q");
    expected.stick[STICK_LEFT].deadzone = 7000;
    expected.stick[STICK_RIGHT].deadzone = 7000;
    ASSERT_EQ(memcmp(&m, &expected, sizeof(m)), 0);
    TEST_PASS();
}

/* Defined behavior for repeated keys: the first occurrence wins. */
TEST(duplicate_keys_first_wins) {
    ControllerMapping m;
    ASSERT_EQ(load_json("{\"buttons\":{\"a\":\"q\",\"a\":\"e\"},"
                        "\"buttons\":{\"a\":\"z\",\"b\":\"z\"},"
                        "\"triggers\":{\"threshold\":10,\"threshold\":250}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("q"));
    ASSERT_EQ(m.bindings[SOURCE_BTN_B], config_parse_key("c"));    // second "buttons" unused
    ASSERT_EQ(m.triggers.threshold, 10);
    TEST_PASS();
}

/* A value of the wrong type is ignored; the rest of the file still applies. */
TEST(wrong_types_are_ignored) {
    ControllerMapping m;
    ASSERT_EQ(load_json(
        "{\"buttons\":{\"a\":5,\"b\":true,\"x\":null,\"y\":[\"f\"],\"lb\":{\"k\":\"q\"},\"rb\":\"q\"},"
        "\"left_stick\":{\"mode\":3,\"deadzone\":\"9000\",\"threshold\":\"0.5\",\"up\":false},"
        "\"right_stick\":\"mouse\","
        "\"triggers\":{\"left\":1,\"threshold\":\"high\",\"hysteresis\":true},"
        "\"features\":{\"rumble\":{\"enabled\":\"yes\",\"intensity\":\"50\",\"button_feedback\":0}},"
        "\"advanced\":{\"streaming_mode\":\"true\",\"confine_cursor\":1,\"confine_margin\":\"2\","
        "\"log_level\":0,\"recenter_when_captured\":1,\"console_output\":null}}", &m), 0);

    ControllerMapping expected;
    config_get_defaults(&expected);
    expected.bindings[SOURCE_BTN_RB] = config_parse_key("q");      // the one valid entry
    ASSERT_EQ(memcmp(&m, &expected, sizeof(m)), 0);

    // Sections of the wrong type are skipped as a whole
    ASSERT_EQ(load_json("{\"buttons\":[\"a\",\"space\"],\"left_stick\":7,\"triggers\":null,"
                        "\"features\":{\"rumble\":false},\"advanced\":\"none\"}", &m), 0);
    ASSERT_TRUE(is_defaults(&m));

    // Unknown names keep the default too
    ASSERT_EQ(load_json("{\"buttons\":{\"a\":\"hyperspace\"},\"left_stick\":{\"mode\":\"tank\"},"
                        "\"advanced\":{\"confine_cursor\":\"screen\",\"log_level\":\"loud\"}}", &m), 0);
    ASSERT_TRUE(is_defaults(&m));
    TEST_PASS();
}

/* Out-of-range numbers are clamped to the nearest allowed value. */
TEST(out_of_range_values_are_clamped) {
    ControllerMapping m;
    ASSERT_EQ(load_json(
        "{\"left_stick\":{\"deadzone\":-100,\"threshold\":7,\"hysteresis\":-1,"
        "\"pwm_period_ms\":0,\"pwm_full_on\":3,\"pwm_min_pulse_ms\":0},"
        "\"right_stick\":{\"deadzone\":99999,\"threshold\":-2,\"hysteresis\":9,"
        "\"pwm_period_ms\":100000,\"pwm_min_pulse_ms\":900,"
        "\"sensitivity\":0,\"curve\":0,\"smoothing\":1},"
        "\"triggers\":{\"threshold\":300,\"hysteresis\":-5},"
        "\"features\":{\"rumble\":{\"intensity\":1000,\"duration_ms\":-1}},"
        "\"advanced\":{\"confine_margin\":-3,\"confine_top_inset\":100000}}", &m), 0);

    ASSERT_EQ(m.stick[STICK_LEFT].deadzone, 0);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].threshold, 0.95f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].hysteresis, 0.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_period_ms, MIN_PWM_PERIOD_MS, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_full_on, 1.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_min_pulse_ms, MIN_PWM_MIN_PULSE_MS, 0.0001f);
    ASSERT_EQ(m.stick[STICK_RIGHT].deadzone, MAX_DEADZONE);
    ASSERT_FLOAT_EQ(m.stick[STICK_RIGHT].threshold, 0.01f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_RIGHT].hysteresis, 0.5f, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_RIGHT].pwm_period_ms, MAX_PWM_PERIOD_MS, 0.0001f);
    ASSERT_FLOAT_EQ(m.stick[STICK_RIGHT].pwm_min_pulse_ms, MAX_PWM_PERIOD_MS / 2.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.sensitivity, 0.05f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.curve, 0.1f, 0.0001f);
    ASSERT_FLOAT_EQ(m.mouse.smoothing, MAX_SMOOTHING, 0.0001f);
    ASSERT_EQ(m.triggers.threshold, 255);
    ASSERT_EQ(m.triggers.hysteresis, 0);
    ASSERT_EQ(m.rumble.intensity, 255);
    ASSERT_EQ(m.rumble.duration_ms, 0);
    ASSERT_FLOAT_EQ(m.cursor.margin, 0.0f, 0.0001f);
    ASSERT_FLOAT_EQ(m.cursor.top_inset, 400.0f, 0.0001f);
    ASSERT_TRUE(in_range(&m));

    ASSERT_EQ(load_json("{\"right_stick\":{\"sensitivity\":-4},\"triggers\":{\"threshold\":-1}}", &m), 0);
    ASSERT_FLOAT_EQ(m.mouse.sensitivity, 0.05f, 0.0001f);
    ASSERT_EQ(m.triggers.threshold, 0);
    TEST_PASS();
}

/* Limits that depend on each other. */
TEST(dependent_limits_are_enforced) {
    ControllerMapping m;
    // Full-on point at or below the press point would divide by zero or less
    ASSERT_EQ(load_json("{\"left_stick\":{\"threshold\":0.6,\"pwm_full_on\":0.5}}", &m), 0);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_full_on, 0.65f, 0.0001f);
    ASSERT_EQ(load_json("{\"left_stick\":{\"threshold\":0.5,\"pwm_full_on\":0.5}}", &m), 0);
    ASSERT(m.stick[STICK_LEFT].pwm_full_on > m.stick[STICK_LEFT].threshold);
    ASSERT_EQ(load_json("{\"left_stick\":{\"threshold\":0.95}}", &m), 0);    // default 0.9 now too low
    ASSERT(m.stick[STICK_LEFT].pwm_full_on > 0.95f);
    ASSERT(m.stick[STICK_LEFT].pwm_full_on <= 1.0f);
    ASSERT_TRUE(in_range(&m));

    // On-time and off-time minimums must both fit in one period
    ASSERT_EQ(load_json("{\"left_stick\":{\"pwm_period_ms\":30,\"pwm_min_pulse_ms\":20}}", &m), 0);
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_min_pulse_ms, 15.0f, 0.0001f);
    ASSERT_EQ(load_json("{\"left_stick\":{\"pwm_period_ms\":30}}", &m), 0);  // default 20 now too long
    ASSERT_FLOAT_EQ(m.stick[STICK_LEFT].pwm_min_pulse_ms, 15.0f, 0.0001f);
    ASSERT_TRUE(in_range(&m));
    TEST_PASS();
}

/* Clamping and ignoring are reported, naming the offending key. */
TEST(bad_values_are_logged) {
    char log_path[600];
    snprintf(log_path, sizeof(log_path), "%s/stderr.txt", g_dir);
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT(saved >= 0 && fd >= 0);
    dup2(fd, STDERR_FILENO);
    close(fd);
    log_set_level(LOG_LEVEL_WARN);

    ControllerMapping m;
    int result = load_json("{\"left_stick\":{\"pwm_period_ms\":0,\"mode\":\"tank\"},"
                           "\"triggers\":{\"threshold\":\"high\"},"
                           "\"buttons\":{\"a\":\"hyperspace\"}}", &m);

    log_set_level(LOG_LEVEL_NONE);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    ASSERT_EQ(result, 0);

    char text[4096] = {0};
    FILE *f = fopen(log_path, "r");
    ASSERT_NOT_NULL(f);
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    ASSERT_NOT_NULL(strstr(text, "left_stick.pwm_period_ms"));
    ASSERT_NOT_NULL(strstr(text, "left_stick.mode"));
    ASSERT_NOT_NULL(strstr(text, "triggers.threshold"));
    ASSERT_NOT_NULL(strstr(text, "buttons.a"));
    ASSERT_NOT_NULL(strstr(text, "hyperspace"));
    TEST_PASS();
}

TEST(strings_escapes_and_long_values) {
    ControllerMapping m;
    // Escaped quotes and backslashes inside strings do not end the string
    ASSERT_EQ(load_json("{\"note\":\"say \\\"hi\\\" \\\\ {[,:\",\"buttons\":{\"a\":\"q\"}}", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("q"));

    // A string ending in an escaped quote is unterminated
    ASSERT_EQ(load_json("{\"note\":\"abc\\\"}", &m), -1);

    // Very long keys and values: skipped or ignored, never overflowed
    static char json[70000];
    const char *head = "{\"", *middle = "\":1,\"buttons\":{\"a\":\"", *tail = "\",\"b\":\"q\"}}";
    char *p = json;
    memcpy(p, head, strlen(head)); p += strlen(head);
    memset(p, 'k', 30000); p += 30000;
    memcpy(p, middle, strlen(middle)); p += strlen(middle);
    memset(p, 'v', 30000); p += 30000;
    memcpy(p, tail, strlen(tail) + 1);
    ASSERT_EQ(load_json(json, &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("space"));    // unknown name: default
    ASSERT_EQ(m.bindings[SOURCE_BTN_B], config_parse_key("q"));

    // Whitespace everywhere is fine
    ASSERT_EQ(load_json(" \n\t{ \"buttons\" \r\n : { \"a\" : \"q\" } \n } \n\n", &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("q"));
    TEST_PASS();
}

TEST(nesting_depth_is_limited) {
    ControllerMapping m;
    char json[4096];
    for (int depth = 1; depth <= 40; depth++) {
        int length = snprintf(json, sizeof(json), "{\"x\":");
        memset(json + length, '[', (size_t)depth); length += depth;
        memset(json + length, ']', (size_t)depth); length += depth;
        snprintf(json + length, sizeof(json) - (size_t)length, ",\"buttons\":{\"a\":\"q\"}}");
        int result = load_json(json, &m);
        ASSERT(result == 0 || result == -1);
        if (depth <= 10) ASSERT_EQ(result, 0);
        if (depth >= 20) ASSERT_EQ(result, -1);     // rejected, not a stack overflow
    }
    // Thousands of open brackets must not recurse without bound
    static char deep[20010];
    memset(deep, '[', 20000);
    deep[20000] = '\0';
    ASSERT_EQ(load_json(deep, &m), -1);
    TEST_PASS();
}

TEST(unreadable_inputs_are_rejected) {
    ControllerMapping m;
    ASSERT_EQ(config_load("/nonexistent/dir/config.json", &m), -1);
    ASSERT_TRUE(is_defaults(&m));
    ASSERT_EQ(config_load(g_dir, &m), -1);          // a directory, not a file
    ASSERT_TRUE(is_defaults(&m));

    // A pipe has no size (ftell fails). The old loader allocated one byte for
    // it and then read the whole pipe into that byte.
    char fifo[600];
    snprintf(fifo, sizeof(fifo), "%s/pipe.json", g_dir);
    ASSERT_EQ(mkfifo(fifo, 0600), 0);
    pid_t writer = fork();
    ASSERT(writer >= 0);
    if (writer == 0) {
        signal(SIGPIPE, SIG_IGN);
        int fd = open(fifo, O_WRONLY);             // waits for the reader
        const char *json = "{\"buttons\":{\"a\":\"q\"},\"padding\":\"0123456789012345678901234567890123456789\"}";
        if (fd >= 0) {
            (void)!write(fd, json, strlen(json));
            close(fd);
        }
        _exit(0);
    }
    int pipe_result = config_load(fifo, &m);
    int child_status;
    waitpid(writer, &child_status, 0);
    ASSERT_EQ(pipe_result, -1);
    ASSERT_TRUE(is_defaults(&m));

    // A file with an embedded NUL ends there: what follows is not parsed
    const char with_nul[] = "{\"buttons\":{\"a\":\"q\"}}\0{\"buttons\":{\"a\":\"e\"}}";
    ASSERT_TRUE(write_bytes(g_path, with_nul, sizeof(with_nul) - 1));
    ASSERT_EQ(config_load(g_path, &m), 0);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("q"));

    // Absurdly large files are refused rather than read into memory
    size_t big = CONFIG_MAX_FILE_BYTES + 2;
    char *buffer = malloc(big);
    ASSERT_NOT_NULL(buffer);
    memset(buffer, ' ', big);
    buffer[0] = '{';
    buffer[big - 1] = '}';
    bool written = write_bytes(g_path, buffer, big);
    free(buffer);
    ASSERT_TRUE(written);
    ASSERT_EQ(config_load(g_path, &m), -1);
    TEST_PASS();
}

/* Random damage to a valid file: must never crash, and whatever loads must
   still be in range. Run under `make test-asan` to catch memory errors too. */
TEST(fuzz_mutated_configs) {
    const char *seed_json =
        "{\"buttons\":{\"a\":\"space\",\"b\":\"mouse_left\",\"lb\":\"scroll_up\"},"
        "\"left_stick\":{\"mode\":\"wasd_pwm\",\"deadzone\":8000,\"threshold\":0.3,"
        "\"hysteresis\":0.05,\"pwm_period_ms\":100,\"pwm_full_on\":0.9,\"pwm_min_pulse_ms\":20},"
        "\"right_stick\":{\"mode\":\"mouse\",\"sensitivity\":1.5,\"curve\":1.8,\"smoothing\":0.3},"
        "\"triggers\":{\"left\":\"mouse_right\",\"right\":\"disabled\",\"threshold\":127},"
        "\"features\":{\"rumble\":{\"enabled\":true,\"intensity\":100,\"duration_ms\":50}},"
        "\"advanced\":{\"log_level\":\"none\",\"confine_cursor\":\"window\",\"confine_margin\":2}}";
    const char alphabet[] = "{}[]\":,0123456789.-+eE truefalsnl\\\n\t\x01\x7f\xff";
    size_t length = strlen(seed_json);
    char mutated[1024];
    uint32_t seed = 0xC0FFEE;
    int loaded = 0;

    for (int round = 0; round < 3000; round++) {
        memcpy(mutated, seed_json, length + 1);
        size_t size = length;
        int edits = 1 + (int)(test_random(&seed) % 4);
        for (int e = 0; e < edits; e++) {
            size_t at = test_random(&seed) % size;
            switch (test_random(&seed) % 3) {
                case 0:   // replace a byte
                    mutated[at] = alphabet[test_random(&seed) % (sizeof(alphabet) - 1)];
                    break;
                case 1:   // delete a byte
                    memmove(mutated + at, mutated + at + 1, size - at - 1);
                    size--;
                    break;
                default:  // duplicate a byte
                    memmove(mutated + at + 1, mutated + at, size - at);
                    size++;
                    break;
            }
        }
        ASSERT_TRUE(write_bytes(g_path, mutated, size));
        ControllerMapping m;
        int result = config_load(g_path, &m);
        ASSERT(result == 0 || result == -1);
        if (result == -1) ASSERT_TRUE(is_defaults(&m));
        if (!in_range(&m)) {
            mutated[size] = '\0';
            printf("\n         out of range after loading: %s ", mutated);
            ASSERT(false);
        }
        loaded += result == 0;
    }
    ASSERT(loaded > 100);        // the fuzzer does reach the value readers
    ASSERT(loaded < 2900);       // ...and the syntax checker
    TEST_PASS();
}

/*******************************************************************************
 * Hot reload
 ******************************************************************************/
TEST(reload_only_when_file_changes) {
    ControllerMapping m;
    ASSERT_TRUE(write_text(g_path, "{\"buttons\":{\"a\":\"q\"}}"));
    set_mtime(g_path, 1000);
    struct timespec seen = {0};

    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 1);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("q"));
    ASSERT_EQ(seen.tv_sec, 1000);
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 0);     // unchanged
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 0);

    ASSERT_TRUE(write_text(g_path, "{\"buttons\":{\"a\":\"e\"}}"));
    set_mtime(g_path, 1001);
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 1);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("e"));

    // An older timestamp (file restored from backup) is not "newer"
    set_mtime(g_path, 900);
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 0);

    // The menu's "Reload Configuration" forces it by forgetting the timestamp
    seen = (struct timespec){0};
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 1);

    ASSERT_EQ(config_reload_if_changed("/nonexistent/config.json", &m, &seen), -1);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("e"));
    TEST_PASS();
}

/* A broken edit keeps the running config, is reported once, and the fixed
   file is picked up afterwards. */
TEST(failed_reload_keeps_old_config) {
    ControllerMapping m;
    ASSERT_TRUE(write_text(g_path, "{\"buttons\":{\"a\":\"q\"},\"right_stick\":{\"mode\":\"arrows\"}}"));
    set_mtime(g_path, 2000);
    struct timespec seen = {0};
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 1);
    ControllerMapping before = m;

    ASSERT_TRUE(write_text(g_path, "{\"buttons\": {\"a\": \"space\""));   // saved mid-edit
    set_mtime(g_path, 2001);
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), -1);
    ASSERT_EQ(memcmp(&m, &before, sizeof(m)), 0);                  // untouched
    ASSERT_EQ(m.stick[STICK_RIGHT].mode, STICK_MODE_ARROWS);
    // The same broken file is not re-read (and re-reported) on every poll
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 0);
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 0);
    ASSERT_EQ(memcmp(&m, &before, sizeof(m)), 0);

    ASSERT_TRUE(write_text(g_path, "{\"buttons\":{\"a\":\"e\"}}"));       // fixed
    set_mtime(g_path, 2002);
    ASSERT_EQ(config_reload_if_changed(g_path, &m, &seen), 1);
    ASSERT_EQ(m.bindings[SOURCE_BTN_A], config_parse_key("e"));
    ASSERT_EQ(m.stick[STICK_RIGHT].mode, STICK_MODE_MOUSE);        // back to default: full replace
    TEST_PASS();
}

/*******************************************************************************
 * Path precedence: --config > ./controller.json > ./config/controller.json >
 * ~/.config/xbox-controller/config.json > defaults
 ******************************************************************************/
TEST(config_path_precedence) {
    char work[400], home[400], path[600], loaded[CONFIG_PATH_MAX];
    snprintf(work, sizeof(work), "%s/work", g_dir);
    snprintf(home, sizeof(home), "%s/home", g_dir);
    snprintf(path, sizeof(path), "%s/config", work);
    ASSERT_EQ(mkdir(work, 0755), 0);
    ASSERT_EQ(mkdir(path, 0755), 0);
    ASSERT_EQ(mkdir(home, 0755), 0);
    snprintf(path, sizeof(path), "%s/.config", home);
    ASSERT_EQ(mkdir(path, 0755), 0);
    snprintf(path, sizeof(path), "%s/.config/xbox-controller", home);
    ASSERT_EQ(mkdir(path, 0755), 0);

    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    ASSERT_EQ(chdir(work), 0);
    setenv("HOME", home, 1);
    ControllerMapping m;
    bool ok = true;
#define CHECK(condition) do { if (!(condition)) { ok = false; \
        printf("\n         precedence check failed: %s (line %d) ", #condition, __LINE__); } } while (0)

    // Nothing anywhere: built-in defaults
    CHECK(config_load_auto(NULL, &m, loaded, sizeof(loaded)) == 0);
    CHECK(strcmp(loaded, CONFIG_PATH_DEFAULTS) == 0);
    CHECK(is_defaults(&m));

    // User config only
    snprintf(path, sizeof(path), "%s%s", home, CONFIG_PATH_USER);
    write_text(path, "{\"buttons\":{\"a\":\"u\"}}");
    CHECK(config_load_auto(NULL, &m, loaded, sizeof(loaded)) == 0);
    CHECK(strcmp(loaded, path) == 0);
    CHECK(m.bindings[SOURCE_BTN_A] == config_parse_key("u"));

    // The repository template beats the user config
    write_text("config/controller.json", "{\"buttons\":{\"a\":\"p\"}}");
    CHECK(config_load_auto(NULL, &m, loaded, sizeof(loaded)) == 0);
    CHECK(strcmp(loaded, CONFIG_PATH_PROJECT) == 0);
    CHECK(m.bindings[SOURCE_BTN_A] == config_parse_key("p"));

    // ./controller.json beats both
    write_text("controller.json", "{\"buttons\":{\"a\":\"l\"}}");
    CHECK(config_load_auto("", &m, loaded, sizeof(loaded)) == 0);   // "" = no CLI path
    CHECK(strcmp(loaded, CONFIG_PATH_LOCAL) == 0);
    CHECK(m.bindings[SOURCE_BTN_A] == config_parse_key("l"));

    // An explicit --config path beats everything
    write_text("explicit.json", "{\"buttons\":{\"a\":\"x\"}}");
    CHECK(config_load_auto("explicit.json", &m, loaded, sizeof(loaded)) == 0);
    CHECK(strcmp(loaded, "explicit.json") == 0);
    CHECK(m.bindings[SOURCE_BTN_A] == config_parse_key("x"));

    // ...and if it cannot be loaded that is an error, not a silent fallback
    CHECK(config_load_auto("missing.json", &m, loaded, sizeof(loaded)) == -1);
    write_text("explicit.json", "{broken");
    CHECK(config_load_auto("explicit.json", &m, NULL, 0) == -1);

    // A broken higher-priority file falls through to the next one
    write_text("controller.json", "{broken");
    CHECK(config_load_auto(NULL, &m, loaded, sizeof(loaded)) == 0);
    CHECK(strcmp(loaded, CONFIG_PATH_PROJECT) == 0);
    CHECK(m.bindings[SOURCE_BTN_A] == config_parse_key("p"));

    // A NULL loaded_path is allowed
    CHECK(config_load_auto(NULL, &m, NULL, 0) == 0);
#undef CHECK

    if (old_home) {
        setenv("HOME", old_home, 1);
        free(old_home);
    }
    ASSERT_EQ(chdir(g_start_dir), 0);
    ASSERT_TRUE(ok);
    TEST_PASS();
}

/* config_print must cope with every mode (it indexes name tables). */
TEST(config_print_handles_every_mode) {
    ControllerMapping m;
    config_get_defaults(&m);
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    ASSERT(saved >= 0 && devnull >= 0);
    dup2(devnull, STDOUT_FILENO);
    close(devnull);
    for (int mode = STICK_MODE_WASD; mode <= STICK_MODE_DISABLED; mode++) {
        for (int confine = CONFINE_WINDOW; confine <= CONFINE_OFF; confine++) {
            m.stick[STICK_LEFT].mode = (StickMode)mode;
            m.stick[STICK_LEFT].pwm = mode % 2 == 0;
            m.stick[STICK_RIGHT].mode = (StickMode)(STICK_MODE_DISABLED - mode);
            m.cursor.confine = (ConfineMode)confine;
            m.bindings[SOURCE_LEFT_TRIGGER] = mode % 2 ? BINDING_NONE : 0xC8;
            config_print(&m);
        }
    }
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    TEST_PASS();
}

/*******************************************************************************
 * Main
 ******************************************************************************/
int main(void) {
    log_set_level(LOG_LEVEL_NONE);       // the loader's warnings are expected here
    if (!getcwd(g_start_dir, sizeof(g_start_dir))) return 2;
    snprintf(g_dir, sizeof(g_dir), "/tmp/xbox-config-test-XXXXXX");
    if (!mkdtemp(g_dir)) return 2;
    snprintf(g_path, sizeof(g_path), "%s/config.json", g_dir);

    TEST_SUITE("Configuration");

    printf("\n-- Defaults --\n");
    RUN_TEST(defaults_bindings);
    RUN_TEST(defaults_values);
    RUN_TEST(shipped_config_matches_defaults);

    printf("\n-- Key names --\n");
    RUN_TEST(parse_key_names);
    RUN_TEST(parse_key_hex_and_invalid);
    RUN_TEST(key_names_round_trip);

    printf("\n-- Loading --\n");
    RUN_TEST(buttons_and_sticks_load);
    RUN_TEST(deadzone_shared_unless_overridden);
    RUN_TEST(trigger_bindings);
    RUN_TEST(old_configs_still_load);
    RUN_TEST(new_options_round_trip);

    printf("\n-- Robustness --\n");
    RUN_TEST(malformed_json_is_rejected);
    RUN_TEST(truncated_file_is_rejected);
    RUN_TEST(unknown_keys_are_ignored);
    RUN_TEST(duplicate_keys_first_wins);
    RUN_TEST(wrong_types_are_ignored);
    RUN_TEST(out_of_range_values_are_clamped);
    RUN_TEST(dependent_limits_are_enforced);
    RUN_TEST(bad_values_are_logged);
    RUN_TEST(strings_escapes_and_long_values);
    RUN_TEST(nesting_depth_is_limited);
    RUN_TEST(unreadable_inputs_are_rejected);
    RUN_TEST(fuzz_mutated_configs);

    printf("\n-- Hot reload --\n");
    RUN_TEST(reload_only_when_file_changes);
    RUN_TEST(failed_reload_keeps_old_config);

    printf("\n-- Paths --\n");
    RUN_TEST(config_path_precedence);
    RUN_TEST(config_print_handles_every_mode);

    TEST_SUMMARY();

    char command[400];
    snprintf(command, sizeof(command), "rm -rf '%s'", g_dir);
    if (system(command) != 0) fprintf(stderr, "could not remove %s\n", g_dir);
    return TEST_EXIT_CODE();
}
