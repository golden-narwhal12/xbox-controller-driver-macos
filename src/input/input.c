/*******************************************************************************
 * input.c - Input processing implementation
 ******************************************************************************/

#include "../../include/input.h"
#include "../../include/event.h"
#include "../../include/log.h"
#include <math.h>
#include <string.h>

/*******************************************************************************
 * Lookup Tables for Optimization (Phase 4)
 ******************************************************************************/
static float POW_LUT[257];        // Pre-computed pow for curve 1.8
static bool lut_initialized = false;
static float cached_curve = 0.0f;

static void init_pow_lut(float curve) {
    // Normalized values from 0.0 to 1.0 (256 steps)
    cached_curve = curve;
    for (int i = 0; i <= 256; i++) {
        float norm = (float)i / 256.0f;
        POW_LUT[i] = powf(norm, curve);
    }
}

static float fast_pow(float norm, float curve) {
    // Reinitialize LUT if curve changed
    if (curve != cached_curve) {
        init_pow_lut(curve);
    }

    int idx = (int)(fabsf(norm) * 256.0f);
    if (idx > 256) idx = 256;
    if (idx < 0) idx = 0;
    return POW_LUT[idx];
}

static void ensure_lut_initialized(float curve) {
    if (!lut_initialized) {
        init_pow_lut(curve);
        lut_initialized = true;
    }
}

/*******************************************************************************
 * Button mask to index mapping
 ******************************************************************************/
#define LEFT_TRIGGER_SOURCE 14
#define RIGHT_TRIGGER_SOURCE 15
#define LEFT_STICK_SOURCE 16
#define RIGHT_STICK_SOURCE 20

static int mouse_binding(uint16_t binding) {
    if (binding == 0xFFFE) return MOUSE_BUTTON_LEFT;
    if (binding == 0xFFFD) return MOUSE_BUTTON_RIGHT;
    if (binding == 0xFFFC) return MOUSE_BUTTON_CENTER;
    return -1;
}

static void change_binding_ref(InputState *state, uint16_t binding, bool pressed) {
    int mouse = mouse_binding(binding);
    if (mouse >= 0) {
        uint8_t *refs = &state->mouse_refs[mouse];
        if (pressed) {
            if ((*refs)++ == 0) send_mouse_button_event(mouse, true);
        } else if (*refs && --(*refs) == 0) {
            send_mouse_button_event(mouse, false);
        }
        state->mouse_left = state->mouse_refs[MOUSE_BUTTON_LEFT] != 0;
        state->mouse_right = state->mouse_refs[MOUSE_BUTTON_RIGHT] != 0;
        state->mouse_middle = state->mouse_refs[MOUSE_BUTTON_CENTER] != 0;
    } else if (binding < 256) {
        uint8_t *refs = &state->key_refs[binding];
        if (pressed) {
            if ((*refs)++ == 0) send_key_event(binding, true);
        } else if (*refs && --(*refs) == 0) {
            send_key_event(binding, false);
        }
        state->keys[binding] = *refs != 0;
    }
}

static void set_source(InputState *state, int source, uint16_t binding, bool pressed) {
    if (state->source_pressed[source] == pressed &&
        (!pressed || state->source_binding[source] == binding)) return;

    if (state->source_pressed[source])
        change_binding_ref(state, state->source_binding[source], false);
    state->source_binding[source] = binding;
    state->source_pressed[source] = pressed;
    if (pressed) change_binding_ref(state, binding, true);
}

static void clear_stick_sources(InputState *state, int base) {
    for (int i = 0; i < 4; i++) set_source(state, base + i, 0xFFFF, false);
}

/*******************************************************************************
 * State Management
 ******************************************************************************/
void input_state_init(InputState *state) {
    memset(state, 0, sizeof(InputState));
}

void input_state_release_all(InputState *state) {
    LOG_INFO("Releasing all keys and buttons...");

    for (int i = 0; i < 256; i++) {
        if (state->keys[i]) {
            send_key_event(i, false);
        }
    }
    if (state->mouse_left) {
        send_mouse_button_event(MOUSE_BUTTON_LEFT, false);
    }
    if (state->mouse_right) {
        send_mouse_button_event(MOUSE_BUTTON_RIGHT, false);
    }
    if (state->mouse_middle) {
        send_mouse_button_event(MOUSE_BUTTON_CENTER, false);
    }
    memset(state, 0, sizeof(*state));
}

/*******************************************************************************
 * Deadzone Processing
 ******************************************************************************/
void apply_deadzone(int16_t *x, int16_t *y, int16_t deadzone) {
    float magnitude_sq = (float)(*x) * (*x) + (float)(*y) * (*y);
    float magnitude = sqrtf(magnitude_sq);

    if (magnitude < deadzone) {
        *x = 0;
        *y = 0;
    } else if (magnitude > STICK_MAX) {
        float scale = (float)STICK_MAX / magnitude;
        *x = (int16_t)(*x * scale);
        *y = (int16_t)(*y * scale);
    }
}

/*******************************************************************************
 * Button Processing
 ******************************************************************************/
void process_buttons(uint16_t buttons, InputState *state, const ControllerMapping *config,
                     void (*rumble_callback)(void *ctx), void *rumble_ctx) {
    struct {
        uint16_t mask;
        uint16_t keycode;
    } button_map[] = {
        {XBOX_BTN_A, config->buttons.key_a},
        {XBOX_BTN_B, config->buttons.key_b},
        {XBOX_BTN_X, config->buttons.key_x},
        {XBOX_BTN_Y, config->buttons.key_y},
        {XBOX_BTN_LB, config->buttons.key_lb},
        {XBOX_BTN_RB, config->buttons.key_rb},
        {XBOX_BTN_LS, config->buttons.key_ls},
        {XBOX_BTN_RS, config->buttons.key_rs},
        {XBOX_BTN_VIEW, config->buttons.key_view},
        {XBOX_BTN_MENU, config->buttons.key_menu},
        {XBOX_BTN_DPAD_UP, config->buttons.key_dpad_up},
        {XBOX_BTN_DPAD_DOWN, config->buttons.key_dpad_down},
        {XBOX_BTN_DPAD_LEFT, config->buttons.key_dpad_left},
        {XBOX_BTN_DPAD_RIGHT, config->buttons.key_dpad_right}
    };

    for (int i = 0; i < XBOX_BTN_COUNT; i++) {
        bool is_pressed = (buttons & button_map[i].mask) != 0;
        bool was_pressed = (state->prev_buttons & button_map[i].mask) != 0;

        if (is_pressed != was_pressed) {
            // Rumble feedback on button press
            if (is_pressed && config->features.rumble.enabled &&
                config->features.rumble.button_feedback && rumble_callback) {
                rumble_callback(rumble_ctx);
            }
        }

        bool turbo = config->features.turbo.enabled && state->turbo_enabled[i];
        if (turbo && is_pressed) {
            set_source(state, i, button_map[i].keycode, false);
            state->turbo_counter[i]++;
            if (config->features.turbo.rate && state->turbo_counter[i] >= config->features.turbo.rate) {
                set_source(state, i, button_map[i].keycode, true);
                set_source(state, i, button_map[i].keycode, false);
                state->turbo_counter[i] = 0;
            }
        } else {
            set_source(state, i, button_map[i].keycode, is_pressed);
            state->turbo_counter[i] = 0;
        }
    }

    state->prev_buttons = buttons;
}

/*******************************************************************************
 * Trigger Processing
 ******************************************************************************/
void process_triggers(uint16_t left_trigger, uint16_t right_trigger,
                      InputState *state, const ControllerMapping *config) {
    // GIP reports 10-bit pressure. Keep the existing 0-255 config threshold scale.
    bool left_pressed = (left_trigger > 1023 ? 255 : left_trigger >> 2) > config->triggers.threshold;
    bool right_pressed = (right_trigger > 1023 ? 255 : right_trigger >> 2) > config->triggers.threshold;
    uint16_t left_binding = config->triggers.left_trigger_mode == TRIGGER_MODE_MOUSE
                            ? 0xFFFE : config->triggers.left_trigger_key;
    uint16_t right_binding = config->triggers.right_trigger_mode == TRIGGER_MODE_MOUSE
                             ? 0xFFFD : config->triggers.right_trigger_key;
    set_source(state, LEFT_TRIGGER_SOURCE, left_binding,
               left_pressed && config->triggers.left_trigger_mode != TRIGGER_MODE_DISABLED);
    set_source(state, RIGHT_TRIGGER_SOURCE, right_binding,
               right_pressed && config->triggers.right_trigger_mode != TRIGGER_MODE_DISABLED);
    state->prev_left_trigger = left_trigger;
    state->prev_right_trigger = right_trigger;
}

/*******************************************************************************
 * Stick as Keys Processing
 ******************************************************************************/
void process_stick_as_keys(int16_t x, int16_t y,
                           uint16_t key_up, uint16_t key_down,
                           uint16_t key_left, uint16_t key_right,
                           InputState *state, int source_base) {

    // Normalize to -1.0 to 1.0
    float norm_x = x / (float)STICK_MAX;
    float norm_y = y / (float)STICK_MAX;

    // Determine which directions are active (with threshold)
    bool up = (norm_y < -0.3f);
    bool down = (norm_y > 0.3f);
    bool left = (norm_x < -0.3f);
    bool right = (norm_x > 0.3f);

    // Send key events for state changes
    set_source(state, source_base, key_up, up);
    set_source(state, source_base + 1, key_down, down);
    set_source(state, source_base + 2, key_left, left);
    set_source(state, source_base + 3, key_right, right);
}

/*******************************************************************************
 * Stick as Mouse Processing
 ******************************************************************************/
void process_stick_as_mouse(int16_t x, int16_t y,
                            float *smoothed_x, float *smoothed_y,
                            float *mouse_dx, float *mouse_dy,
                            const ControllerMapping *config) {
    ensure_lut_initialized(config->sticks.mouse_curve);

    // Normalize to -1.0 to 1.0
    float target_x = x / (float)STICK_MAX;
    float target_y = y / (float)STICK_MAX;  // CoreGraphics screen Y grows downward

    // Exponential smoothing
    float alpha = 1.0f - config->sticks.mouse_smoothing;
    *smoothed_x = alpha * target_x + (1.0f - alpha) * (*smoothed_x);
    *smoothed_y = alpha * target_y + (1.0f - alpha) * (*smoothed_y);

    float norm_x = *smoothed_x;
    float norm_y = *smoothed_y;

    // Apply exponential curve using LUT
    float sign_x = (norm_x >= 0) ? 1.0f : -1.0f;
    float sign_y = (norm_y >= 0) ? 1.0f : -1.0f;

    float curved_x = sign_x * fast_pow(norm_x, config->sticks.mouse_curve);
    float curved_y = sign_y * fast_pow(norm_y, config->sticks.mouse_curve);

    // Scale by sensitivity
    float dx = curved_x * config->sticks.mouse_sensitivity * 15.0f;
    float dy = curved_y * config->sticks.mouse_sensitivity * 15.0f;

    // Accumulate deltas
    *mouse_dx += dx;
    *mouse_dy += dy;
}

/*******************************************************************************
 * Full Stick Processing
 ******************************************************************************/
void process_sticks(int16_t left_x, int16_t left_y,
                    int16_t right_x, int16_t right_y,
                    InputState *state, const ControllerMapping *config,
                    bool streaming_mode) {
    (void)streaming_mode;
    // Apply deadzones and cache results
    apply_deadzone(&left_x, &left_y, config->sticks.deadzone);
    apply_deadzone(&right_x, &right_y, config->sticks.deadzone);

    // Cache post-deadzone values for continuous movement
    state->dz_left_stick_x = left_x;
    state->dz_left_stick_y = left_y;
    state->dz_right_stick_x = right_x;
    state->dz_right_stick_y = right_y;

    // Process left stick
    switch (config->sticks.left_stick_mode) {
        case STICK_MODE_WASD:
            process_stick_as_keys(left_x, left_y,
                                  config->sticks.left_up, config->sticks.left_down,
                                  config->sticks.left_left, config->sticks.left_right,
                                  state, LEFT_STICK_SOURCE);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(left_x, left_y, 0x7E, 0x7D, 0x7B, 0x7C,
                                  state, LEFT_STICK_SOURCE);
            break;
        case STICK_MODE_MOUSE:
            clear_stick_sources(state, LEFT_STICK_SOURCE);
            break;
        case STICK_MODE_DISABLED:
        default:
            clear_stick_sources(state, LEFT_STICK_SOURCE);
            break;
    }

    // Process right stick
    switch (config->sticks.right_stick_mode) {
        case STICK_MODE_WASD:
            process_stick_as_keys(right_x, right_y,
                                  config->sticks.right_up, config->sticks.right_down,
                                  config->sticks.right_left, config->sticks.right_right,
                                  state, RIGHT_STICK_SOURCE);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(right_x, right_y, 0x7E, 0x7D, 0x7B, 0x7C,
                                  state, RIGHT_STICK_SOURCE);
            break;
        case STICK_MODE_MOUSE:
            clear_stick_sources(state, RIGHT_STICK_SOURCE);
            break;
        case STICK_MODE_DISABLED:
        default:
            clear_stick_sources(state, RIGHT_STICK_SOURCE);
            break;
    }

    // Mouse movement is generated at a fixed rate by the driver loop.
    state->current_left_stick_x = left_x;
    state->current_left_stick_y = left_y;
    state->current_right_stick_x = right_x;
    state->current_right_stick_y = right_y;
}

/*******************************************************************************
 * Continuous Movement Generation
 ******************************************************************************/
void generate_continuous_movement(InputState *state, const ControllerMapping *config,
                                  bool streaming_mode) {
    int16_t left_x = state->dz_left_stick_x;
    int16_t left_y = state->dz_left_stick_y;
    int16_t right_x = state->dz_right_stick_x;
    int16_t right_y = state->dz_right_stick_y;

    if (config->sticks.left_stick_mode == STICK_MODE_MOUSE) {
        process_stick_as_mouse(left_x, left_y,
                               &state->smoothed_left_x,
                               &state->smoothed_left_y,
                               &state->mouse_dx, &state->mouse_dy,
                               config);
    }

    if (config->sticks.right_stick_mode == STICK_MODE_MOUSE) {
        process_stick_as_mouse(right_x, right_y,
                               &state->smoothed_right_x,
                               &state->smoothed_right_y,
                               &state->mouse_dx, &state->mouse_dy,
                               config);
    }

    // Send accumulated mouse movement
    if (state->mouse_dx != 0.0f || state->mouse_dy != 0.0f) {
        send_mouse_movement(state->mouse_dx, state->mouse_dy, streaming_mode,
                            state->mouse_left, state->mouse_right, state->mouse_middle);
        state->mouse_dx = 0.0f;
        state->mouse_dy = 0.0f;
    }
}
