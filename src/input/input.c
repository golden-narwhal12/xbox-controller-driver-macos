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
static float SQRT_LUT[1025];      // Pre-computed sqrt for magnitude 0-32767
static float POW_LUT[257];        // Pre-computed pow for curve 1.8
static bool lut_initialized = false;
static float cached_curve = 0.0f;

static void init_sqrt_lut(void) {
    // Magnitude ranges from 0 to ~46340 (sqrt(32767^2 + 32767^2))
    // We'll use 1024 buckets, each covering ~45 units
    for (int i = 0; i <= 1024; i++) {
        float magnitude = (float)i * 45.0f;
        SQRT_LUT[i] = sqrtf(magnitude);
    }
}

static void init_pow_lut(float curve) {
    // Normalized values from 0.0 to 1.0 (256 steps)
    cached_curve = curve;
    for (int i = 0; i <= 256; i++) {
        float norm = (float)i / 256.0f;
        POW_LUT[i] = powf(norm, curve);
    }
}

static float fast_sqrt(float magnitude_squared) {
    // Use LUT for approximate sqrt
    int idx = (int)(magnitude_squared / (45.0f * 45.0f));
    if (idx > 1024) idx = 1024;
    if (idx < 0) idx = 0;
    return SQRT_LUT[idx];
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
        init_sqrt_lut();
        init_pow_lut(curve);
        lut_initialized = true;
    }
}

/*******************************************************************************
 * Button mask to index mapping
 ******************************************************************************/
static const uint16_t button_masks[XBOX_BTN_COUNT] = {
    XBOX_BTN_A, XBOX_BTN_B, XBOX_BTN_X, XBOX_BTN_Y,
    XBOX_BTN_LB, XBOX_BTN_RB, XBOX_BTN_LS, XBOX_BTN_RS,
    XBOX_BTN_VIEW, XBOX_BTN_MENU,
    XBOX_BTN_DPAD_UP, XBOX_BTN_DPAD_DOWN, XBOX_BTN_DPAD_LEFT, XBOX_BTN_DPAD_RIGHT
};

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
            // Check for turbo mode
            if (config->features.turbo.enabled && state->turbo_enabled[i] && is_pressed) {
                // Turbo is handled in turbo processing
            } else {
                send_key_event(button_map[i].keycode, is_pressed);
            }

            // Rumble feedback on button press
            if (is_pressed && config->features.rumble.enabled &&
                config->features.rumble.button_feedback && rumble_callback) {
                rumble_callback(rumble_ctx);
            }
        }

        // Handle turbo mode
        if (config->features.turbo.enabled && state->turbo_enabled[i] && is_pressed) {
            state->turbo_counter[i]++;
            if (state->turbo_counter[i] >= config->features.turbo.rate) {
                send_key_event(button_map[i].keycode, true);
                send_key_event(button_map[i].keycode, false);
                state->turbo_counter[i] = 0;
            }
        } else {
            state->turbo_counter[i] = 0;
        }
    }

    state->prev_buttons = buttons;
}

/*******************************************************************************
 * Trigger Processing
 ******************************************************************************/
void process_triggers(uint8_t left_trigger, uint8_t right_trigger,
                      InputState *state, const ControllerMapping *config) {
    // Right trigger (swapped - GIP packet has them reversed)
    bool right_pressed = left_trigger > config->triggers.threshold;
    bool right_was_pressed = state->prev_right_trigger > config->triggers.threshold;

    if (right_pressed != right_was_pressed) {
        if (config->triggers.right_trigger_mode == TRIGGER_MODE_MOUSE) {
            send_mouse_button_event(MOUSE_BUTTON_RIGHT, right_pressed);
            state->mouse_right = right_pressed;
        } else if (config->triggers.right_trigger_mode == TRIGGER_MODE_KEY) {
            send_key_event(config->triggers.right_trigger_key, right_pressed);
            state->keys[config->triggers.right_trigger_key] = right_pressed;
        }
    }

    // Left trigger (swapped - GIP packet has them reversed)
    bool left_pressed = right_trigger > config->triggers.threshold;
    bool left_was_pressed = state->prev_left_trigger > config->triggers.threshold;

    if (left_pressed != left_was_pressed) {
        if (config->triggers.left_trigger_mode == TRIGGER_MODE_MOUSE) {
            send_mouse_button_event(MOUSE_BUTTON_LEFT, left_pressed);
            state->mouse_left = left_pressed;
        } else if (config->triggers.left_trigger_mode == TRIGGER_MODE_KEY) {
            send_key_event(config->triggers.left_trigger_key, left_pressed);
            state->keys[config->triggers.left_trigger_key] = left_pressed;
        }
    }

    state->prev_left_trigger = right_trigger;  // Swapped
    state->prev_right_trigger = left_trigger;  // Swapped
}

/*******************************************************************************
 * Stick as Keys Processing
 ******************************************************************************/
void process_stick_as_keys(int16_t x, int16_t y,
                           uint16_t key_up, uint16_t key_down,
                           uint16_t key_left, uint16_t key_right,
                           InputState *state) {
    // Axes are swapped in the controller - swap them back
    int16_t temp = x;
    x = y;
    y = temp;

    // Normalize to -1.0 to 1.0
    float norm_x = x / (float)STICK_MAX;
    float norm_y = y / (float)STICK_MAX;

    // Determine which directions are active (with threshold)
    bool up = (norm_y > 0.3f);
    bool down = (norm_y < -0.3f);
    bool left = (norm_x < -0.3f);
    bool right = (norm_x > 0.3f);

    // Send key events for state changes
    if (up != state->keys[key_up]) {
        send_key_event(key_up, up);
        state->keys[key_up] = up;
    }
    if (down != state->keys[key_down]) {
        send_key_event(key_down, down);
        state->keys[key_down] = down;
    }
    if (left != state->keys[key_left]) {
        send_key_event(key_left, left);
        state->keys[key_left] = left;
    }
    if (right != state->keys[key_right]) {
        send_key_event(key_right, right);
        state->keys[key_right] = right;
    }
}

/*******************************************************************************
 * Stick as Mouse Processing
 ******************************************************************************/
void process_stick_as_mouse(int16_t x, int16_t y,
                            float *smoothed_x, float *smoothed_y,
                            float *mouse_dx, float *mouse_dy,
                            const ControllerMapping *config) {
    ensure_lut_initialized(config->sticks.mouse_curve);

    // Axes are swapped in the controller - swap them back
    int16_t temp = x;
    x = y;
    y = temp;

    // Normalize to -1.0 to 1.0
    float target_x = x / (float)STICK_MAX;
    float target_y = -y / (float)STICK_MAX;  // Invert Y

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
                                  state);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(left_x, left_y, 0x7E, 0x7D, 0x7B, 0x7C, state);
            break;
        case STICK_MODE_MOUSE:
            process_stick_as_mouse(left_x, left_y,
                                   &state->smoothed_left_x,
                                   &state->smoothed_left_y,
                                   &state->mouse_dx, &state->mouse_dy,
                                   config);
            break;
        case STICK_MODE_DISABLED:
        default:
            break;
    }

    // Process right stick
    switch (config->sticks.right_stick_mode) {
        case STICK_MODE_WASD:
            process_stick_as_keys(right_x, right_y,
                                  config->sticks.left_up, config->sticks.left_down,
                                  config->sticks.left_left, config->sticks.left_right,
                                  state);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(right_x, right_y, 0x7E, 0x7D, 0x7B, 0x7C, state);
            break;
        case STICK_MODE_MOUSE:
            process_stick_as_mouse(right_x, right_y,
                                   &state->smoothed_right_x,
                                   &state->smoothed_right_y,
                                   &state->mouse_dx, &state->mouse_dy,
                                   config);
            break;
        case STICK_MODE_DISABLED:
        default:
            break;
    }

    // Send accumulated mouse movement
    if (state->mouse_dx != 0.0f || state->mouse_dy != 0.0f) {
        send_mouse_movement(state->mouse_dx, state->mouse_dy, streaming_mode);
        state->mouse_dx = 0.0f;
        state->mouse_dy = 0.0f;
    }

    // Store current positions
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
    // Use cached post-deadzone values to avoid recomputation
    int16_t left_x = state->dz_left_stick_x;
    int16_t left_y = state->dz_left_stick_y;
    int16_t right_x = state->dz_right_stick_x;
    int16_t right_y = state->dz_right_stick_y;

    // Generate mouse movement if sticks are in mouse mode
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
        send_mouse_movement(state->mouse_dx, state->mouse_dy, streaming_mode);
        state->mouse_dx = 0.0f;
        state->mouse_dy = 0.0f;
    }
}
