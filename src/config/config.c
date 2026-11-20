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
    {"mouse_left", BINDING_MOUSE_LEFT}, {"mouse_right", BINDING_MOUSE_RIGHT},
    {"mouse_middle", BINDING_MOUSE_MIDDLE},

    // Scroll wheel (one notch per press, repeats while held)
    {"scroll_up", BINDING_SCROLL_UP}, {"scroll_down", BINDING_SCROLL_DOWN},

    {NULL, 0}
};

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/
void config_get_defaults(ControllerMapping *mapping) {
    memset(mapping, 0, sizeof(ControllerMapping));

    uint16_t *b = mapping->bindings;
    b[SOURCE_BTN_A] = KEY_SPACE;
    b[SOURCE_BTN_B] = KEY_C;
    b[SOURCE_BTN_X] = KEY_R;
    b[SOURCE_BTN_Y] = KEY_F;
    b[SOURCE_BTN_LB] = BINDING_SCROLL_UP;
    b[SOURCE_BTN_RB] = BINDING_SCROLL_DOWN;
    b[SOURCE_BTN_LS] = KEY_LEFT_SHIFT;
    b[SOURCE_BTN_RS] = KEY_LEFT_CONTROL;
    b[SOURCE_BTN_VIEW] = KEY_TAB;
    b[SOURCE_BTN_MENU] = KEY_ESCAPE;
    b[SOURCE_BTN_DPAD_UP] = KEY_UP_ARROW;
    b[SOURCE_BTN_DPAD_DOWN] = KEY_DOWN_ARROW;
    b[SOURCE_BTN_DPAD_LEFT] = KEY_LEFT_ARROW;
    b[SOURCE_BTN_DPAD_RIGHT] = KEY_RIGHT_ARROW;

    // RT is the primary (left) click, LT the secondary (right) click,
    // matching the usual console layout (RT attacks/fires).
    b[SOURCE_LEFT_TRIGGER] = BINDING_MOUSE_RIGHT;
    b[SOURCE_RIGHT_TRIGGER] = BINDING_MOUSE_LEFT;

    b[SOURCE_LEFT_STICK + STICK_DIR_UP] = KEY_W;
    b[SOURCE_LEFT_STICK + STICK_DIR_DOWN] = KEY_S;
    b[SOURCE_LEFT_STICK + STICK_DIR_LEFT] = KEY_A;
    b[SOURCE_LEFT_STICK + STICK_DIR_RIGHT] = KEY_D;
    b[SOURCE_RIGHT_STICK + STICK_DIR_UP] = KEY_I;
    b[SOURCE_RIGHT_STICK + STICK_DIR_DOWN] = KEY_K;
    b[SOURCE_RIGHT_STICK + STICK_DIR_LEFT] = KEY_J;
    b[SOURCE_RIGHT_STICK + STICK_DIR_RIGHT] = KEY_L;

    for (int i = 0; i < STICK_COUNT; i++) {
        StickConfig *stick = &mapping->stick[i];
        stick->mode = i == STICK_LEFT ? STICK_MODE_WASD : STICK_MODE_MOUSE;
        stick->deadzone = DEFAULT_DEADZONE;
        stick->threshold = DEFAULT_STICK_THRESHOLD;
        stick->hysteresis = DEFAULT_STICK_HYSTERESIS;
    }

    mapping->mouse.sensitivity = DEFAULT_SENSITIVITY;
    mapping->mouse.curve = DEFAULT_CURVE;
    mapping->mouse.smoothing = DEFAULT_SMOOTHING;

    mapping->triggers.threshold = DEFAULT_TRIGGER_THRESHOLD;
    mapping->triggers.hysteresis = DEFAULT_TRIGGER_HYSTERESIS;

    mapping->rumble.enabled = true;
    mapping->rumble.button_feedback = true;
    mapping->rumble.intensity = DEFAULT_RUMBLE_INTENSITY;
    mapping->rumble.duration_ms = DEFAULT_RUMBLE_DURATION_MS;

    mapping->cursor.confine = CONFINE_WINDOW;
    mapping->cursor.margin = DEFAULT_CONFINE_MARGIN;
    mapping->cursor.top_inset = DEFAULT_CONFINE_TOP_INSET;
    mapping->cursor.recenter_when_captured = false;
    mapping->cursor.streaming_mode = false;

    mapping->console_output_enabled = true;
    mapping->log_level = LOG_LEVEL_INFO;
}

/*******************************************************************************
 * Key Parsing
 ******************************************************************************/
uint16_t config_parse_key(const char *key_name) {
    if (key_name == NULL) return BINDING_NONE;

    // Convert to lowercase for comparison
    char lower[64];
    size_t len = strlen(key_name);
    if (len >= sizeof(lower)) return BINDING_NONE;

    for (size_t i = 0; i < len; i++) {
        char c = key_name[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    lower[len] = '\0';

    for (const KeyMapping *km = key_mappings; km->name != NULL; km++) {
        if (strcmp(lower, km->name) == 0) {
            return km->keycode;
        }
    }

    // A raw virtual keycode as hex (0x##)
    if (len > 2 && lower[0] == '0' && lower[1] == 'x') {
        char *end;
        long value = strtol(lower, &end, 16);
        if (*end == '\0' && value >= 0 && value < KEY_COUNT) return (uint16_t)value;
    }

    return BINDING_NONE;
}

const char* config_key_name(uint16_t binding) {
    if (binding == BINDING_NONE) return "none";
    for (const KeyMapping *km = key_mappings; km->name != NULL; km++) {
        if (km->keycode == binding) {
            return km->name;
        }
    }
    return "unknown";
}

/*******************************************************************************
 * Minimal JSON reader (no external dependencies)
 *
 * The whole document is validated once with skip_value() before any value is
 * read, so the typed readers below only ever see well-formed JSON.
 ******************************************************************************/
#define JSON_MAX_DEPTH 16

static char* read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    char *buffer = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);   // -1 for things that are not regular files
        if (size >= 0 && size <= CONFIG_MAX_FILE_BYTES && fseek(f, 0, SEEK_SET) == 0) {
            buffer = (char*)malloc((size_t)size + 1);
            if (buffer) {
                size_t read_size = fread(buffer, 1, (size_t)size, f);
                buffer[read_size] = '\0';
            }
        }
    }
    fclose(f);
    return buffer;
}

static const char* skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Copies a JSON string (escapes are taken literally: \" becomes ") into `out`,
   truncating if needed. Returns the position after the closing quote, or NULL
   if `p` is not a complete string. */
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
        if (out_size && i < out_size - 1) out[i++] = *p;
        p++;
    }
    if (out_size) out[i] = '\0';

    return *p == '"' ? p + 1 : NULL;
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_number_start(char c) {
    return c == '-' || is_digit(c);
}

/* Returns the end of the JSON number at `p`, or NULL if there is none.
   strtod() alone is too generous: it also accepts "nan", "inf", hex and a
   leading '+' or '.'. */
static const char* scan_number(const char *p) {
    if (*p == '-') p++;
    if (!is_digit(*p)) return NULL;
    while (is_digit(*p)) p++;
    if (*p == '.') {
        p++;
        if (!is_digit(*p)) return NULL;
        while (is_digit(*p)) p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!is_digit(*p)) return NULL;
        while (is_digit(*p)) p++;
    }
    return p;
}

static const char* skip_value(const char *p, int depth) {
    if (depth > JSON_MAX_DEPTH) return NULL;
    p = skip_whitespace(p);
    if (*p == '"') {
        return parse_string(p, NULL, 0);
    }
    if (*p == '{' || *p == '[') {
        char closing = *p++ == '{' ? '}' : ']';
        p = skip_whitespace(p);
        while (*p && *p != closing) {
            if (closing == '}') {
                p = parse_string(p, NULL, 0);
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
    const char *end = scan_number(p);
    // A syntactically fine number can still overflow a double ("1e999")
    return end && isfinite(strtod(p, NULL)) ? end : NULL;
}

/* Returns the value of `key` in the object at `json` (the first one if the key
   is repeated), or NULL if `json` is not an object or has no such key. */
static const char* find_key(const char *json, const char *key) {
    if (!json) return NULL;
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

/*******************************************************************************
 * Typed readers. Each leaves *out untouched when the key is absent, and warns
 * (naming section.key) when the value is unusable.
 ******************************************************************************/
static bool read_string(const char *object, const char *section, const char *key,
                        char *out, size_t out_size) {
    const char *value = find_key(object, key);
    if (!value) return false;
    if (!parse_string(value, out, out_size)) {
        LOG_WARN("Config %s.%s: expected a string; ignored", section, key);
        return false;
    }
    return true;
}

static void read_bool(const char *object, const char *section, const char *key, bool *out) {
    const char *value = find_key(object, key);
    if (!value) return;
    if (strncmp(value, "true", 4) == 0) *out = true;
    else if (strncmp(value, "false", 5) == 0) *out = false;
    else LOG_WARN("Config %s.%s: expected true or false; ignored", section, key);
}

/* Out-of-range numbers are clamped (and reported), not rejected. */
static void read_number(const char *object, const char *section, const char *key,
                        double min, double max, double *out) {
    const char *value = find_key(object, key);
    if (!value) return;
    if (!is_number_start(*value)) {
        LOG_WARN("Config %s.%s: expected a number; ignored", section, key);
        return;
    }
    double number = strtod(value, NULL);
    if (number < min || number > max) {
        double clamped = number < min ? min : max;
        LOG_WARN("Config %s.%s: %g is outside %g..%g; using %g",
                 section, key, number, min, max, clamped);
        number = clamped;
    }
    *out = number;
}

static void read_float(const char *object, const char *section, const char *key,
                       float min, float max, float *out) {
    double value = *out;
    read_number(object, section, key, min, max, &value);
    *out = (float)value;
}

/* A binding is a key or mouse name, or "disabled" / "none" to unbind. */
static void read_binding(const char *object, const char *section, const char *key,
                         uint16_t *out) {
    char name[64];
    if (!read_string(object, section, key, name, sizeof(name))) return;
    if (strcmp(name, "disabled") == 0 || strcmp(name, "none") == 0) {
        *out = BINDING_NONE;
        return;
    }
    uint16_t binding = config_parse_key(name);
    if (binding == BINDING_NONE) {
        LOG_WARN("Config %s.%s: unknown key name \"%s\"; ignored", section, key, name);
        return;
    }
    *out = binding;
}

typedef struct {
    const char *name;
    int value;
} NamedValue;

static void read_choice(const char *object, const char *section, const char *key,
                        const NamedValue *choices, int *out) {
    char name[64];
    if (!read_string(object, section, key, name, sizeof(name))) return;
    for (const NamedValue *c = choices; c->name; c++) {
        if (strcmp(name, c->name) == 0) {
            *out = c->value;
            return;
        }
    }
    LOG_WARN("Config %s.%s: unknown value \"%s\"; ignored", section, key, name);
}

/*******************************************************************************
 * Sections
 ******************************************************************************/
static const NamedValue BUTTON_NAMES[] = {
    {"a", SOURCE_BTN_A}, {"b", SOURCE_BTN_B}, {"x", SOURCE_BTN_X}, {"y", SOURCE_BTN_Y},
    {"lb", SOURCE_BTN_LB}, {"rb", SOURCE_BTN_RB}, {"ls", SOURCE_BTN_LS}, {"rs", SOURCE_BTN_RS},
    {"view", SOURCE_BTN_VIEW}, {"menu", SOURCE_BTN_MENU},
    {"dpad_up", SOURCE_BTN_DPAD_UP}, {"dpad_down", SOURCE_BTN_DPAD_DOWN},
    {"dpad_left", SOURCE_BTN_DPAD_LEFT}, {"dpad_right", SOURCE_BTN_DPAD_RIGHT},
    {NULL, 0}
};

static const NamedValue DIRECTION_NAMES[] = {
    {"up", STICK_DIR_UP}, {"down", STICK_DIR_DOWN},
    {"left", STICK_DIR_LEFT}, {"right", STICK_DIR_RIGHT},
    {NULL, 0}
};

static const NamedValue STICK_MODE_NAMES[] = {
    {"wasd", STICK_MODE_WASD}, {"arrows", STICK_MODE_ARROWS},
    {"mouse", STICK_MODE_MOUSE}, {"disabled", STICK_MODE_DISABLED},
    {NULL, 0}
};

static const NamedValue LOG_LEVEL_NAMES[] = {
    {"debug", LOG_LEVEL_DEBUG}, {"info", LOG_LEVEL_INFO}, {"warn", LOG_LEVEL_WARN},
    {"error", LOG_LEVEL_ERROR}, {"none", LOG_LEVEL_NONE},
    {NULL, 0}
};

static const NamedValue CONFINE_NAMES[] = {
    {"window", CONFINE_WINDOW}, {"display", CONFINE_DISPLAY}, {"off", CONFINE_OFF},
    {NULL, 0}
};

static void load_stick(const char *object, const char *section, int stick_index,
                       int source_base, ControllerMapping *mapping) {
    StickConfig *stick = &mapping->stick[stick_index];

    for (const NamedValue *d = DIRECTION_NAMES; d->name; d++) {
        read_binding(object, section, d->name, &mapping->bindings[source_base + d->value]);
    }

    int mode = stick->mode;
    read_choice(object, section, "mode", STICK_MODE_NAMES, &mode);
    stick->mode = (StickMode)mode;

    double deadzone = stick->deadzone;
    read_number(object, section, "deadzone", 0, MAX_DEADZONE, &deadzone);
    stick->deadzone = (int16_t)deadzone;

    read_float(object, section, "threshold", 0.01f, 0.95f, &stick->threshold);
    read_float(object, section, "hysteresis", 0.0f, 0.5f, &stick->hysteresis);
}

/*******************************************************************************
 * Configuration Loading
 ******************************************************************************/
int config_load(const char *path, ControllerMapping *mapping) {
    // Start with defaults
    config_get_defaults(mapping);

    char *json = read_file(path);
    if (!json) {
        LOG_DEBUG("Could not read config file: %s", path);
        return -1;
    }

    const char *end = skip_value(json, 0);
    if (*skip_whitespace(json) != '{' || !end || *skip_whitespace(end) != '\0') {
        LOG_ERROR("Invalid configuration JSON: %s", path);
        free(json);
        return -1;
    }

    LOG_INFO("Loading configuration from: %s", path);

    const char *buttons = find_key(json, "buttons");
    for (const NamedValue *b = BUTTON_NAMES; b->name; b++) {
        read_binding(buttons, "buttons", b->name, &mapping->bindings[b->value]);
    }

    // left_stick.deadzone has always applied to both sticks, so read it into
    // both; right_stick.deadzone (new) then overrides it for the right stick.
    const char *left_stick = find_key(json, "left_stick");
    double shared_deadzone = mapping->stick[STICK_RIGHT].deadzone;
    read_number(left_stick, "left_stick", "deadzone", 0, MAX_DEADZONE, &shared_deadzone);
    mapping->stick[STICK_RIGHT].deadzone = (int16_t)shared_deadzone;
    load_stick(left_stick, "left_stick", STICK_LEFT, SOURCE_LEFT_STICK, mapping);

    const char *right_stick = find_key(json, "right_stick");
    load_stick(right_stick, "right_stick", STICK_RIGHT, SOURCE_RIGHT_STICK, mapping);
    read_float(right_stick, "right_stick", "sensitivity", 0.05f, 100.0f,
               &mapping->mouse.sensitivity);
    read_float(right_stick, "right_stick", "curve", 0.1f, 10.0f, &mapping->mouse.curve);
    read_float(right_stick, "right_stick", "smoothing", 0.0f, MAX_SMOOTHING,
               &mapping->mouse.smoothing);

    const char *triggers = find_key(json, "triggers");
    read_binding(triggers, "triggers", "left", &mapping->bindings[SOURCE_LEFT_TRIGGER]);
    read_binding(triggers, "triggers", "right", &mapping->bindings[SOURCE_RIGHT_TRIGGER]);
    double number = mapping->triggers.threshold;
    read_number(triggers, "triggers", "threshold", 0, TRIGGER_CONFIG_MAX, &number);
    mapping->triggers.threshold = (uint8_t)number;
    number = mapping->triggers.hysteresis;
    read_number(triggers, "triggers", "hysteresis", 0, TRIGGER_CONFIG_MAX, &number);
    mapping->triggers.hysteresis = (uint8_t)number;

    const char *rumble = find_key(find_key(json, "features"), "rumble");
    read_bool(rumble, "features.rumble", "enabled", &mapping->rumble.enabled);
    read_bool(rumble, "features.rumble", "button_feedback", &mapping->rumble.button_feedback);
    number = mapping->rumble.intensity;
    read_number(rumble, "features.rumble", "intensity", 0, 255, &number);
    mapping->rumble.intensity = (uint8_t)number;
    number = mapping->rumble.duration_ms;
    read_number(rumble, "features.rumble", "duration_ms", 0, 2550, &number);
    mapping->rumble.duration_ms = (uint16_t)number;

    const char *advanced = find_key(json, "advanced");
    int choice = mapping->log_level;
    read_choice(advanced, "advanced", "log_level", LOG_LEVEL_NAMES, &choice);
    mapping->log_level = (LogLevel)choice;
    read_bool(advanced, "advanced", "streaming_mode", &mapping->cursor.streaming_mode);
    read_bool(advanced, "advanced", "console_output", &mapping->console_output_enabled);
    choice = mapping->cursor.confine;
    read_choice(advanced, "advanced", "confine_cursor", CONFINE_NAMES, &choice);
    mapping->cursor.confine = (ConfineMode)choice;
    read_float(advanced, "advanced", "confine_margin", 0.0f, 200.0f, &mapping->cursor.margin);
    read_float(advanced, "advanced", "confine_top_inset", 0.0f, 400.0f,
               &mapping->cursor.top_inset);
    read_bool(advanced, "advanced", "recenter_when_captured",
              &mapping->cursor.recenter_when_captured);

    free(json);
    return 0;
}

static bool try_load(const char *path, ControllerMapping *mapping,
                     char *loaded_path, size_t path_size) {
    if (access(path, R_OK) != 0 || config_load(path, mapping) != 0) return false;
    if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", path);
    return true;
}

int config_load_auto(const char *cli_path, ControllerMapping *mapping,
                     char *loaded_path, size_t path_size) {
    // Priority 1: CLI argument. Failing to load what was asked for is an error.
    if (cli_path && strlen(cli_path) > 0) {
        if (config_load(cli_path, mapping) == 0) {
            if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", cli_path);
            return 0;
        }
        LOG_ERROR("Could not load requested configuration: %s", cli_path);
        return -1;
    }

    // Priority 2: local config. Priority 3: the repository's config template.
    if (try_load(CONFIG_PATH_LOCAL, mapping, loaded_path, path_size)) return 0;
    if (try_load(CONFIG_PATH_PROJECT, mapping, loaded_path, path_size)) return 0;

    // Priority 4: user config
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        char user_config[CONFIG_PATH_MAX];
        snprintf(user_config, sizeof(user_config), "%s%s", home, CONFIG_PATH_USER);
        if (try_load(user_config, mapping, loaded_path, path_size)) return 0;
    }

    // Priority 5: defaults
    config_get_defaults(mapping);
    if (loaded_path && path_size) snprintf(loaded_path, path_size, "%s", CONFIG_PATH_DEFAULTS);
    return 0;
}

int config_reload_if_changed(const char *path, ControllerMapping *mapping,
                             struct timespec *last_modified) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    bool newer = st.st_mtimespec.tv_sec > last_modified->tv_sec ||
                 (st.st_mtimespec.tv_sec == last_modified->tv_sec &&
                  st.st_mtimespec.tv_nsec > last_modified->tv_nsec);
    if (!newer) return 0;

    // Remember this version either way, so a broken file is reported once
    // rather than on every poll until it is fixed.
    *last_modified = st.st_mtimespec;

    ControllerMapping next;
    if (config_load(path, &next) != 0) {
        LOG_ERROR("Keeping the previous configuration");
        return -1;
    }
    *mapping = next;
    LOG_INFO("Configuration reloaded");
    return 1;
}

/*******************************************************************************
 * Configuration Printing
 ******************************************************************************/
static void print_stick(const char *label, const ControllerMapping *mapping, int index) {
    static const char *MODE_NAMES[] = {"WASD", "Arrows", "Mouse", "Disabled"};
    const StickConfig *stick = &mapping->stick[index];
    printf("  %s stick: %s, deadzone %d (%.1f%%)", label, MODE_NAMES[stick->mode],
           stick->deadzone,
           stick->deadzone * 100.0f / STICK_MAX);
    if (stick->mode == STICK_MODE_WASD || stick->mode == STICK_MODE_ARROWS) {
        printf(", key threshold %.2f (release %.2f)", stick->threshold,
               stick->threshold - stick->hysteresis);
    }
    printf("\n");
}

void config_print(const ControllerMapping *mapping) {
    static const char *CONFINE_LABELS[] = {"focused window", "displays", "off"};

    printf("Configuration:\n");
    print_stick("Left", mapping, STICK_LEFT);
    print_stick("Right", mapping, STICK_RIGHT);
    printf("  Left trigger: %s\n", config_key_name(mapping->bindings[SOURCE_LEFT_TRIGGER]));
    printf("  Right trigger: %s\n", config_key_name(mapping->bindings[SOURCE_RIGHT_TRIGGER]));
    printf("  Trigger threshold: %d +/- %d of 255\n", mapping->triggers.threshold,
           mapping->triggers.hysteresis / 2);
    printf("  Mouse sensitivity: %.1f, curve: %.1f, smoothing: %.2f\n",
           mapping->mouse.sensitivity, mapping->mouse.curve, mapping->mouse.smoothing);
    printf("  Cursor confinement: %s\n", CONFINE_LABELS[mapping->cursor.confine]);
    printf("  Streaming mode: %s\n", mapping->cursor.streaming_mode ? "ENABLED" : "disabled");
    printf("  Rumble: %s\n", mapping->rumble.enabled ? "enabled" : "disabled");
}
