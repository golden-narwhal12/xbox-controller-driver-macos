/*******************************************************************************
 * config.c - Runtime configuration implementation
 ******************************************************************************/

#include "../../include/config.h"
#include "../../include/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <math.h>

/*******************************************************************************
 * Key Name to Keycode Mapping
 ******************************************************************************/
typedef struct {
    const char *name;
    uint16_t keycode;
} KeyMapping;

static const KeyMapping key_mappings[] = {
    // Letters
    {"a", 0x00}, {"b", 0x0B}, {"c", 0x08}, {"d", 0x02}, {"e", 0x0E},
    {"f", 0x03}, {"g", 0x05}, {"h", 0x04}, {"i", 0x22}, {"j", 0x26},
    {"k", 0x28}, {"l", 0x25}, {"m", 0x2E}, {"n", 0x2D}, {"o", 0x1F},
    {"p", 0x23}, {"q", 0x0C}, {"r", 0x0F}, {"s", 0x01}, {"t", 0x11},
    {"u", 0x20}, {"v", 0x09}, {"w", 0x0D}, {"x", 0x07}, {"y", 0x10},
    {"z", 0x06},

    // Numbers
    {"1", 0x12}, {"2", 0x13}, {"3", 0x14}, {"4", 0x15}, {"5", 0x17},
    {"6", 0x16}, {"7", 0x1A}, {"8", 0x1C}, {"9", 0x19}, {"0", 0x1D},

    // Special keys
    {"space", 0x31}, {"return", 0x24}, {"enter", 0x24}, {"tab", 0x30},
    {"escape", 0x35}, {"esc", 0x35}, {"delete", 0x33}, {"backspace", 0x33},
    {"forward_delete", 0x75},

    // Modifiers
    {"left_shift", 0x38}, {"right_shift", 0x3C}, {"shift", 0x38},
    {"left_control", 0x3B}, {"right_control", 0x3E}, {"control", 0x3B}, {"ctrl", 0x3B},
    {"left_option", 0x3A}, {"right_option", 0x3D}, {"option", 0x3A}, {"alt", 0x3A},
    {"left_command", 0x37}, {"right_command", 0x36}, {"command", 0x37}, {"cmd", 0x37},

    // Arrow keys
    {"up", 0x7E}, {"down", 0x7D}, {"left", 0x7B}, {"right", 0x7C},
    {"up_arrow", 0x7E}, {"down_arrow", 0x7D}, {"left_arrow", 0x7B}, {"right_arrow", 0x7C},

    // Function keys
    {"f1", 0x7A}, {"f2", 0x78}, {"f3", 0x63}, {"f4", 0x76},
    {"f5", 0x60}, {"f6", 0x61}, {"f7", 0x62}, {"f8", 0x64},
    {"f9", 0x65}, {"f10", 0x6D}, {"f11", 0x67}, {"f12", 0x6F},

    // Mouse buttons (special handling)
    {"mouse_left", 0xFFFE}, {"mouse_right", 0xFFFD}, {"mouse_middle", 0xFFFC},

    {NULL, 0}
};

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/
void config_get_defaults(ControllerMapping *mapping) {
    memset(mapping, 0, sizeof(ControllerMapping));

    // Button mappings
    mapping->buttons.key_a = 0x31;  // Space
    mapping->buttons.key_b = 0x08;  // C
    mapping->buttons.key_x = 0x0F;  // R
    mapping->buttons.key_y = 0x03;  // F
    mapping->buttons.key_lb = 0x0C; // Q
    mapping->buttons.key_rb = 0x0E; // E
    mapping->buttons.key_ls = 0x38; // Left Shift
    mapping->buttons.key_rs = 0x3B; // Left Control
    mapping->buttons.key_view = 0x30;   // Tab
    mapping->buttons.key_menu = 0x35;   // Escape
    mapping->buttons.key_dpad_up = 0x7E;    // Up Arrow
    mapping->buttons.key_dpad_down = 0x7D;  // Down Arrow
    mapping->buttons.key_dpad_left = 0x7B;  // Left Arrow
    mapping->buttons.key_dpad_right = 0x7C; // Right Arrow

    // Stick settings
    mapping->sticks.left_stick_mode = STICK_MODE_WASD;
    mapping->sticks.left_up = 0x0D;     // W
    mapping->sticks.left_down = 0x01;   // S
    mapping->sticks.left_left = 0x00;   // A
    mapping->sticks.left_right = 0x02;  // D

    mapping->sticks.right_stick_mode = STICK_MODE_MOUSE;
    mapping->sticks.right_up = 0x22;    // I
    mapping->sticks.right_down = 0x28;  // K
    mapping->sticks.right_left = 0x26;  // J
    mapping->sticks.right_right = 0x25; // L

    mapping->sticks.mouse_sensitivity = DEFAULT_SENSITIVITY;
    mapping->sticks.mouse_curve = DEFAULT_CURVE;
    mapping->sticks.mouse_smoothing = DEFAULT_SMOOTHING;
    mapping->sticks.deadzone = DEFAULT_DEADZONE;

    // Trigger settings
    mapping->triggers.left_trigger_mode = TRIGGER_MODE_MOUSE;
    mapping->triggers.right_trigger_mode = TRIGGER_MODE_MOUSE;
    mapping->triggers.left_trigger_key = 0x06;  // Z
    mapping->triggers.right_trigger_key = 0x07; // X
    mapping->triggers.threshold = DEFAULT_THRESHOLD;

    // Feature settings
    mapping->features.rumble.enabled = true;
    mapping->features.rumble.button_feedback = true;
    mapping->features.rumble.intensity = 100;
    mapping->features.rumble.duration_ms = RUMBLE_DURATION_MS;

    mapping->features.turbo.enabled = true;
    mapping->features.turbo.rate = DEFAULT_TURBO_RATE;

    mapping->features.analog_keyboard.enabled = false;

    // General settings
    mapping->console_output_enabled = true;
    mapping->streaming_mode = false;
    mapping->log_level = LOG_LEVEL_INFO;
}

/*******************************************************************************
 * Key Parsing
 ******************************************************************************/
uint16_t config_parse_key(const char *key_name) {
    if (key_name == NULL) return 0xFFFF;

    // Convert to lowercase for comparison
    char lower[64];
    size_t len = strlen(key_name);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;

    for (size_t i = 0; i < len; i++) {
        char c = key_name[i];
        if (c >= 'A' && c <= 'Z') {
            lower[i] = c + ('a' - 'A');
        } else {
            lower[i] = c;
        }
    }
    lower[len] = '\0';

    // Search in mappings
    for (const KeyMapping *km = key_mappings; km->name != NULL; km++) {
        if (strcmp(lower, km->name) == 0) {
            return km->keycode;
        }
    }

    // Try parsing as hex (0x##)
    if (len > 2 && lower[0] == '0' && lower[1] == 'x') {
        char *end;
        long value = strtol(lower, &end, 16);
        if (*end == '\0' && value >= 0 && value <= 255) return (uint16_t)value;
    }

    return 0xFFFF;
}

const char* config_key_name(uint16_t keycode) {
    for (const KeyMapping *km = key_mappings; km->name != NULL; km++) {
        if (km->keycode == keycode) {
            return km->name;
        }
    }
    return "unknown";
}

/*******************************************************************************
 * Simple JSON Parser (minimal, no external dependencies)
 ******************************************************************************/
static char* read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, size, f);
    buffer[read_size] = '\0';
    fclose(f);

    return buffer;
}

static const char* skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char* parse_string(const char *p, char *out, size_t out_size) {
    p = skip_whitespace(p);
    if (out_size) out[0] = '\0';
    if (*p != '"') return NULL;
    p++;

    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) {
            p++;
        }
        if (i < out_size - 1) out[i++] = *p;
        p++;
    }
    out[i] = '\0';

    return *p == '"' ? p + 1 : NULL;
}

static const char* parse_number(const char *p, double *out) {
    p = skip_whitespace(p);
    char *end;
    *out = strtod(p, &end);
    return end;
}

static const char* parse_bool(const char *p, bool *out) {
    p = skip_whitespace(p);
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return p + 4;
    } else if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return p + 5;
    }
    return NULL;
}

static const char* skip_value(const char *p, int depth) {
    if (depth > 16) return NULL;
    p = skip_whitespace(p);
    if (*p == '"') {
        char ignored[1];
        return parse_string(p, ignored, sizeof(ignored));
    }
    if (*p == '{' || *p == '[') {
        char closing = *p++ == '{' ? '}' : ']';
        p = skip_whitespace(p);
        while (*p && *p != closing) {
            if (closing == '}') {
                char ignored[1];
                p = parse_string(p, ignored, sizeof(ignored));
                if (!p) return NULL;
                p = skip_whitespace(p);
                if (*p++ != ':') return NULL;
            }
            p = skip_value(p, depth + 1);
            if (!p) return NULL;
            p = skip_whitespace(p);
            if (*p == ',') p = skip_whitespace(p + 1);
            else if (*p != closing) return NULL;
        }
        return *p == closing ? p + 1 : NULL;
    }
    if (strncmp(p, "true", 4) == 0) return p + 4;
    if (strncmp(p, "false", 5) == 0) return p + 5;
    if (strncmp(p, "null", 4) == 0) return p + 4;
    char *end;
    double number = strtod(p, &end);
    return end != p && isfinite(number) ? end : NULL;
}

static const char* find_key(const char *json, const char *key) {
    const char *p = skip_whitespace(json);
    if (*p++ != '{') return NULL;
    p = skip_whitespace(p);
    while (*p && *p != '}') {
        char name[128];
        p = parse_string(p, name, sizeof(name));
        if (!p) return NULL;
        p = skip_whitespace(p);
        if (*p++ != ':') return NULL;
        p = skip_whitespace(p);
        if (strcmp(name, key) == 0) return p;
        p = skip_value(p, 1);
        if (!p) return NULL;
        p = skip_whitespace(p);
        if (*p == ',') p = skip_whitespace(p + 1);
        else if (*p != '}') return NULL;
    }
    return NULL;
}

static void load_key_binding(const char *object, const char *name, uint16_t *binding) {
    const char *value = find_key(object, name);
    char key_name[64];
    if (value && parse_string(value, key_name, sizeof(key_name))) {
        uint16_t parsed = config_parse_key(key_name);
        if (parsed != 0xFFFF) *binding = parsed;
    }
}

/*******************************************************************************
 * Configuration Loading
 ******************************************************************************/
int config_load(const char *path, ControllerMapping *mapping) {
    // Start with defaults
    config_get_defaults(mapping);

    char *json = read_file(path);
    if (!json) {
        LOG_DEBUG("Could not open config file: %s", path);
        return -1;
    }

    const char *end = skip_value(json, 0);
    if (*skip_whitespace(json) != '{' || !end || *skip_whitespace(end) != '\0') {
        LOG_ERROR("Invalid configuration JSON: %s", path);
        free(json);
        return -1;
    }

    LOG_INFO("Loading configuration from: %s", path);

    const char *p;
    char str_val[64];
    double num_val;
    bool bool_val;

    // Parse buttons
    const char *buttons = find_key(json, "buttons");
    if (buttons) {
        if ((p = find_key(buttons, "a"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_a = key;
        }
        if ((p = find_key(buttons, "b"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_b = key;
        }
        if ((p = find_key(buttons, "x"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_x = key;
        }
        if ((p = find_key(buttons, "y"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_y = key;
        }
        if ((p = find_key(buttons, "lb"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_lb = key;
        }
        if ((p = find_key(buttons, "rb"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_rb = key;
        }
        if ((p = find_key(buttons, "ls"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_ls = key;
        }
        if ((p = find_key(buttons, "rs"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_rs = key;
        }
        if ((p = find_key(buttons, "view"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_view = key;
        }
        if ((p = find_key(buttons, "menu"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_menu = key;
        }
        if ((p = find_key(buttons, "dpad_up"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_dpad_up = key;
        }
        if ((p = find_key(buttons, "dpad_down"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_dpad_down = key;
        }
        if ((p = find_key(buttons, "dpad_left"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_dpad_left = key;
        }
        if ((p = find_key(buttons, "dpad_right"))) {
            parse_string(p, str_val, sizeof(str_val));
            uint16_t key = config_parse_key(str_val);
            if (key != 0xFFFF) mapping->buttons.key_dpad_right = key;
        }
    }

    // Parse left_stick
    const char *left_stick = find_key(json, "left_stick");
    if (left_stick) {
        load_key_binding(left_stick, "up", &mapping->sticks.left_up);
        load_key_binding(left_stick, "down", &mapping->sticks.left_down);
        load_key_binding(left_stick, "left", &mapping->sticks.left_left);
        load_key_binding(left_stick, "right", &mapping->sticks.left_right);
        if ((p = find_key(left_stick, "mode"))) {
            parse_string(p, str_val, sizeof(str_val));
            if (strcmp(str_val, "wasd") == 0) mapping->sticks.left_stick_mode = STICK_MODE_WASD;
            else if (strcmp(str_val, "arrows") == 0) mapping->sticks.left_stick_mode = STICK_MODE_ARROWS;
            else if (strcmp(str_val, "mouse") == 0) mapping->sticks.left_stick_mode = STICK_MODE_MOUSE;
            else if (strcmp(str_val, "disabled") == 0) mapping->sticks.left_stick_mode = STICK_MODE_DISABLED;
        }
        if ((p = find_key(left_stick, "deadzone"))) {
            if (parse_number(p, &num_val) != p && num_val >= 0 && num_val <= STICK_MAX)
                mapping->sticks.deadzone = (int16_t)num_val;
        }
    }

    // Parse right_stick
    const char *right_stick = find_key(json, "right_stick");
    if (right_stick) {
        load_key_binding(right_stick, "up", &mapping->sticks.right_up);
        load_key_binding(right_stick, "down", &mapping->sticks.right_down);
        load_key_binding(right_stick, "left", &mapping->sticks.right_left);
        load_key_binding(right_stick, "right", &mapping->sticks.right_right);
        if ((p = find_key(right_stick, "mode"))) {
            parse_string(p, str_val, sizeof(str_val));
            if (strcmp(str_val, "wasd") == 0) mapping->sticks.right_stick_mode = STICK_MODE_WASD;
            else if (strcmp(str_val, "arrows") == 0) mapping->sticks.right_stick_mode = STICK_MODE_ARROWS;
            else if (strcmp(str_val, "mouse") == 0) mapping->sticks.right_stick_mode = STICK_MODE_MOUSE;
            else if (strcmp(str_val, "disabled") == 0) mapping->sticks.right_stick_mode = STICK_MODE_DISABLED;
        }
        if ((p = find_key(right_stick, "sensitivity"))) {
            if (parse_number(p, &num_val) != p && num_val >= 0 && num_val <= 100)
                mapping->sticks.mouse_sensitivity = (float)num_val;
        }
        if ((p = find_key(right_stick, "curve"))) {
            if (parse_number(p, &num_val) != p && num_val > 0 && num_val <= 10)
                mapping->sticks.mouse_curve = (float)num_val;
        }
        if ((p = find_key(right_stick, "smoothing"))) {
            if (parse_number(p, &num_val) != p && num_val >= 0 && num_val <= 1)
                mapping->sticks.mouse_smoothing = (float)num_val;
        }
    }

    // Parse triggers
    const char *triggers = find_key(json, "triggers");
    if (triggers) {
        if ((p = find_key(triggers, "left"))) {
            parse_string(p, str_val, sizeof(str_val));
            if (strcmp(str_val, "mouse_left") == 0) {
                mapping->triggers.left_trigger_mode = TRIGGER_MODE_MOUSE;
            } else if (strcmp(str_val, "disabled") == 0) {
                mapping->triggers.left_trigger_mode = TRIGGER_MODE_DISABLED;
            } else {
                uint16_t key = config_parse_key(str_val);
                if (key != 0xFFFF) {
                    mapping->triggers.left_trigger_mode = TRIGGER_MODE_KEY;
                    mapping->triggers.left_trigger_key = key;
                }
            }
        }
        if ((p = find_key(triggers, "right"))) {
            parse_string(p, str_val, sizeof(str_val));
            if (strcmp(str_val, "mouse_right") == 0) {
                mapping->triggers.right_trigger_mode = TRIGGER_MODE_MOUSE;
            } else if (strcmp(str_val, "disabled") == 0) {
                mapping->triggers.right_trigger_mode = TRIGGER_MODE_DISABLED;
            } else {
                uint16_t key = config_parse_key(str_val);
                if (key != 0xFFFF) {
                    mapping->triggers.right_trigger_mode = TRIGGER_MODE_KEY;
                    mapping->triggers.right_trigger_key = key;
                }
            }
        }
        if ((p = find_key(triggers, "threshold"))) {
            if (parse_number(p, &num_val) != p && num_val >= 0 && num_val <= 255)
                mapping->triggers.threshold = (uint8_t)num_val;
        }
    }

    // Parse features
    const char *features = find_key(json, "features");
    if (features) {
        const char *rumble = find_key(features, "rumble");
        if (rumble) {
            if ((p = find_key(rumble, "enabled"))) {
                if (parse_bool(p, &bool_val)) mapping->features.rumble.enabled = bool_val;
            }
            if ((p = find_key(rumble, "button_feedback"))) {
                if (parse_bool(p, &bool_val)) mapping->features.rumble.button_feedback = bool_val;
            }
            if ((p = find_key(rumble, "intensity"))) {
                if (parse_number(p, &num_val) != p && num_val >= 0 && num_val <= 255)
                    mapping->features.rumble.intensity = (uint8_t)num_val;
            }
            if ((p = find_key(rumble, "duration_ms"))) {
                if (parse_number(p, &num_val) != p && num_val >= 0 && num_val <= 2550)
                    mapping->features.rumble.duration_ms = (uint16_t)num_val;
            }
        }
        const char *turbo = find_key(features, "turbo");
        if (turbo) {
            if ((p = find_key(turbo, "enabled"))) {
                if (parse_bool(p, &bool_val)) mapping->features.turbo.enabled = bool_val;
            }
            if ((p = find_key(turbo, "rate"))) {
                if (parse_number(p, &num_val) != p && num_val >= 1 && num_val <= 255)
                    mapping->features.turbo.rate = (uint8_t)num_val;
            }
        }
        const char *analog = find_key(features, "analog_keyboard");
        if (analog) {
            if ((p = find_key(analog, "enabled"))) {
                if (parse_bool(p, &bool_val)) mapping->features.analog_keyboard.enabled = bool_val;
            }
        }
    }

    // Parse advanced
    const char *advanced = find_key(json, "advanced");
    if (advanced) {
        if ((p = find_key(advanced, "log_level"))) {
            parse_string(p, str_val, sizeof(str_val));
            if (strcmp(str_val, "debug") == 0) mapping->log_level = LOG_LEVEL_DEBUG;
            else if (strcmp(str_val, "info") == 0) mapping->log_level = LOG_LEVEL_INFO;
            else if (strcmp(str_val, "warn") == 0) mapping->log_level = LOG_LEVEL_WARN;
            else if (strcmp(str_val, "error") == 0) mapping->log_level = LOG_LEVEL_ERROR;
            else if (strcmp(str_val, "none") == 0) mapping->log_level = LOG_LEVEL_NONE;
        }
        if ((p = find_key(advanced, "streaming_mode"))) {
            if (parse_bool(p, &bool_val)) mapping->streaming_mode = bool_val;
        }
        if ((p = find_key(advanced, "console_output"))) {
            if (parse_bool(p, &bool_val)) mapping->console_output_enabled = bool_val;
        }
    }

    free(json);
    return 0;
}

int config_load_auto(const char *cli_path, ControllerMapping *mapping, char *loaded_path, size_t path_size) {
    // Priority 1: CLI argument
    if (cli_path && strlen(cli_path) > 0) {
        if (config_load(cli_path, mapping) == 0) {
            if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", cli_path);
            return 0;
        }
        LOG_ERROR("Could not load requested configuration: %s", cli_path);
        return -1;
    }

    // Priority 2: Local config
    if (access(CONFIG_PATH_LOCAL, R_OK) == 0) {
        if (config_load(CONFIG_PATH_LOCAL, mapping) == 0) {
            if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", CONFIG_PATH_LOCAL);
            return 0;
        }
    }

    // Priority 3: Repository config template when running from the checkout
    if (access(CONFIG_PATH_PROJECT, R_OK) == 0) {
        if (config_load(CONFIG_PATH_PROJECT, mapping) == 0) {
            if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", CONFIG_PATH_PROJECT);
            return 0;
        }
    }

    // Priority 4: User config
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }

    if (home) {
        char user_config[CONFIG_PATH_MAX];
        snprintf(user_config, sizeof(user_config), "%s%s", home, CONFIG_PATH_USER);

        if (access(user_config, R_OK) == 0) {
            if (config_load(user_config, mapping) == 0) {
                if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", user_config);
                return 0;
            }
        }
    }

    // Priority 5: Defaults
    config_get_defaults(mapping);
    if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", "(defaults)");
    return 0;
}

int config_reload_if_changed(const char *path, ControllerMapping *mapping, struct timespec *last_modified) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    if (st.st_mtimespec.tv_sec > last_modified->tv_sec ||
        (st.st_mtimespec.tv_sec == last_modified->tv_sec &&
         st.st_mtimespec.tv_nsec > last_modified->tv_nsec)) {
        ControllerMapping next;
        if (config_load(path, &next) == 0) {
            *mapping = next;
            *last_modified = st.st_mtimespec;
            LOG_INFO("Configuration reloaded");
            return 1;
        }
        return -1;
    }

    return 0;
}

/*******************************************************************************
 * Configuration Printing
 ******************************************************************************/
void config_print(const ControllerMapping *mapping) {
    const char *stick_mode_names[] = {"WASD", "Arrows", "Mouse", "Disabled"};
    const char *trigger_mode_names[] = {"Mouse", "Key", "Disabled"};

    printf("Configuration:\n");
    printf("  Left stick: %s\n", stick_mode_names[mapping->sticks.left_stick_mode]);
    printf("  Right stick: %s\n", stick_mode_names[mapping->sticks.right_stick_mode]);
    printf("  Left trigger: %s\n", trigger_mode_names[mapping->triggers.left_trigger_mode]);
    printf("  Right trigger: %s\n", trigger_mode_names[mapping->triggers.right_trigger_mode]);
    printf("  Deadzone: %d (%.1f%%)\n", mapping->sticks.deadzone,
           (mapping->sticks.deadzone / 32767.0f) * 100.0f);
    printf("  Mouse smoothing: %.2f\n", mapping->sticks.mouse_smoothing);
    printf("  Mouse sensitivity: %.1f\n", mapping->sticks.mouse_sensitivity);
    printf("  Mouse curve: %.1f\n", mapping->sticks.mouse_curve);
    printf("  Streaming mode: %s\n", mapping->streaming_mode ? "ENABLED" : "disabled");
    printf("  Rumble: %s\n", mapping->features.rumble.enabled ? "enabled" : "disabled");
    printf("  Turbo: %s\n", mapping->features.turbo.enabled ? "enabled" : "disabled");
}
