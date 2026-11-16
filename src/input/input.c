/*******************************************************************************
 * input.c - Input processing implementation
 ******************************************************************************/

#include "../../include/input.h"
#include "../../include/event.h"
#include "../../include/log.h"
#include <math.h>
#include <string.h>

/*******************************************************************************
 * Response Curve Lookup Table
 ******************************************************************************/
static float POW_LUT[257];        // |norm|^curve sampled at 0, 1/256, ..., 1
static bool lut_initialized = false;
static float cached_curve = 0.0f;

static void init_pow_lut(float curve) {
    cached_curve = curve;
    for (int i = 0; i <= 256; i++) {
        float norm = (float)i / 256.0f;
        POW_LUT[i] = powf(norm, curve);
    }
    lut_initialized = true;
}

float curve_lookup(float norm, float curve) {
    // Rebuild the table on first use or when the configured curve changes
    if (!lut_initialized || curve != cached_curve) {
        init_pow_lut(curve);
    }

    int idx = (int)(fabsf(norm) * 256.0f);
    if (idx > 256) idx = 256;
    if (idx < 0) idx = 0;
    return POW_LUT[idx];
}

/*******************************************************************************
 * Button mask to index mapping
 ******************************************************************************/
static int mouse_binding(uint16_t binding) {
    if (binding == BINDING_MOUSE_LEFT) return MOUSE_BUTTON_LEFT;
    if (binding == BINDING_MOUSE_RIGHT) return MOUSE_BUTTON_RIGHT;
    if (binding == BINDING_MOUSE_MIDDLE) return MOUSE_BUTTON_CENTER;
    return -1;
}

static int scroll_binding(uint16_t binding) {
    if (binding == BINDING_SCROLL_UP) return 0;
    if (binding == BINDING_SCROLL_DOWN) return 1;
    return -1;
}

static int scroll_direction(int which) {
    return which == 0 ? 1 : -1;
}

static void change_binding_ref(InputState *state, uint16_t binding, bool pressed) {
    int scroll = scroll_binding(binding);
    if (scroll >= 0) {
        // Scrolling has no "up" event: one notch on press, repeats are timed
        // in generate_continuous_movement() while the binding stays held.
        uint8_t *refs = &state->scroll_refs[scroll];
        if (pressed) {
            if ((*refs)++ == 0) {
                state->scroll_held_ms[scroll] = 0.0f;
                send_scroll_event(scroll_direction(scroll));
            }
        } else if (*refs) {
            --(*refs);
        }
        return;
    }

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

// Returns true when this call is a new press of the source (its press edge).
static bool set_source(InputState *state, int source, uint16_t binding, bool pressed) {
    if (state->source_pressed[source] == pressed &&
        (!pressed || state->source_binding[source] == binding)) return false;

    bool press_edge = pressed && !state->source_pressed[source];
    if (state->source_pressed[source])
        change_binding_ref(state, state->source_binding[source], false);
    state->source_binding[source] = binding;
    state->source_pressed[source] = pressed;
    if (pressed) change_binding_ref(state, binding, true);
    return press_edge;
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
    // Button i is source i; the trigger and stick sources start after the last button.
    _Static_assert(sizeof(button_map) / sizeof(button_map[0]) == XBOX_BTN_COUNT,
                   "button_map must list exactly XBOX_BTN_COUNT buttons");

    for (int i = 0; i < XBOX_BTN_COUNT; i++) {
        bool is_pressed = (buttons & button_map[i].mask) != 0;
        bool press_edge = set_source(state, SOURCE_FIRST_BUTTON + i,
                                     button_map[i].keycode, is_pressed);

        // Rumble feedback once per press
        if (press_edge && config->features.rumble.enabled &&
            config->features.rumble.button_feedback && rumble_callback) {
            rumble_callback(rumble_ctx);
        }
    }
}

/*******************************************************************************
 * Trigger Processing
 ******************************************************************************/
void process_triggers(uint16_t left_trigger, uint16_t right_trigger,
                      InputState *state, const ControllerMapping *config) {
    // GIP reports 10-bit pressure. Keep the existing 0-255 config threshold scale.
    bool left_pressed = (left_trigger > 1023 ? 255 : left_trigger >> 2) > config->triggers.threshold;
    bool right_pressed = (right_trigger > 1023 ? 255 : right_trigger >> 2) > config->triggers.threshold;
    // Each trigger holds its configured binding (a key or a mouse button).
    set_source(state, SOURCE_LEFT_TRIGGER, config->triggers.left_trigger_key,
               left_pressed && config->triggers.left_trigger_mode != TRIGGER_MODE_DISABLED);
    set_source(state, SOURCE_RIGHT_TRIGGER, config->triggers.right_trigger_key,
               right_pressed && config->triggers.right_trigger_mode != TRIGGER_MODE_DISABLED);
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
                            const ControllerMapping *config, float dt_scale) {
    // Normalize to -1.0 to 1.0
    float target_x = x / (float)STICK_MAX;
    float target_y = y / (float)STICK_MAX;  // CoreGraphics screen Y grows downward

    // Exponential smoothing. `mouse_smoothing` is the fraction of the previous
    // value kept per nominal tick; raising it to dt_scale keeps the filter's
    // time constant the same when a tick runs long.
    float keep = powf(config->sticks.mouse_smoothing, dt_scale);
    *smoothed_x = (1.0f - keep) * target_x + keep * (*smoothed_x);
    *smoothed_y = (1.0f - keep) * target_y + keep * (*smoothed_y);

    float norm_x = *smoothed_x;
    float norm_y = *smoothed_y;

    // Response curve on the stick's distance from center, keeping its
    // direction, so a diagonal turns as fast as straight left/right.
    float magnitude = sqrtf(norm_x * norm_x + norm_y * norm_y);
    float scale = magnitude > 0.0f
                  ? curve_lookup(magnitude, config->sticks.mouse_curve) / magnitude
                  : 0.0f;
    float curved_x = norm_x * scale;
    float curved_y = norm_y * scale;

    // Scale by sensitivity and elapsed time so speed doesn't depend on loop timing.
    float gain = config->sticks.mouse_sensitivity * MOUSE_PX_PER_TICK * dt_scale;
    float dx = curved_x * gain;
    float dy = curved_y * gain;

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
                                  state, SOURCE_LEFT_STICK);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(left_x, left_y, 0x7E, 0x7D, 0x7B, 0x7C,
                                  state, SOURCE_LEFT_STICK);
            break;
        case STICK_MODE_MOUSE:
            clear_stick_sources(state, SOURCE_LEFT_STICK);
            break;
        case STICK_MODE_DISABLED:
        default:
            clear_stick_sources(state, SOURCE_LEFT_STICK);
            break;
    }

    // Process right stick
    switch (config->sticks.right_stick_mode) {
        case STICK_MODE_WASD:
            process_stick_as_keys(right_x, right_y,
                                  config->sticks.right_up, config->sticks.right_down,
                                  config->sticks.right_left, config->sticks.right_right,
                                  state, SOURCE_RIGHT_STICK);
            break;
        case STICK_MODE_ARROWS:
            process_stick_as_keys(right_x, right_y, 0x7E, 0x7D, 0x7B, 0x7C,
                                  state, SOURCE_RIGHT_STICK);
            break;
        case STICK_MODE_MOUSE:
            clear_stick_sources(state, SOURCE_RIGHT_STICK);
            break;
        case STICK_MODE_DISABLED:
        default:
            clear_stick_sources(state, SOURCE_RIGHT_STICK);
            break;
    }

    // Mouse movement is generated at a fixed rate by the driver loop.
    state->current_left_stick_x = left_x;
    state->current_left_stick_y = left_y;
    state->current_right_stick_x = right_x;
    state->current_right_stick_y = right_y;
}

/*******************************************************************************
 * Packet Decoding
 ******************************************************************************/

/*
 * GIP reports stick +Y as up; everything in this file uses screen convention
 * (+Y down, matching CoreGraphics). Bitwise NOT maps 32767 -> -32768 and
 * -32768 -> 32767, avoiding the int16 overflow that unary minus has at
 * -32768. Linux's xpad driver performs the same conversion.
 */
static int16_t gip_y_to_screen(int16_t y) {
    return (int16_t)~y;
}

void process_input_packet(const GipInputPacket *packet, InputState *state,
                          const ControllerMapping *config,
                          void (*rumble_callback)(void *ctx), void *rumble_ctx) {
    process_buttons(packet->buttons, state, config, rumble_callback, rumble_ctx);
    process_triggers(packet->left_trigger, packet->right_trigger, state, config);
    process_sticks(packet->left_stick_x, gip_y_to_screen(packet->left_stick_y),
                   packet->right_stick_x, gip_y_to_screen(packet->right_stick_y),
                   state, config, config->streaming_mode);
}

/*******************************************************************************
 * Continuous Movement Generation
 ******************************************************************************/
void generate_continuous_movement(InputState *state, const ControllerMapping *config,
                                  bool streaming_mode, float dt_ms) {
    if (dt_ms <= 0.0f) return;
    // Clamp so a long stall (e.g. a reconnect) can't turn into one big jump.
    if (dt_ms > MOTION_MAX_DT_MS) dt_ms = MOTION_MAX_DT_MS;
    float dt_scale = dt_ms / MOTION_TICK_MS;

    int16_t left_x = state->dz_left_stick_x;
    int16_t left_y = state->dz_left_stick_y;
    int16_t right_x = state->dz_right_stick_x;
    int16_t right_y = state->dz_right_stick_y;

    if (config->sticks.left_stick_mode == STICK_MODE_MOUSE) {
        process_stick_as_mouse(left_x, left_y,
                               &state->smoothed_left_x,
                               &state->smoothed_left_y,
                               &state->mouse_dx, &state->mouse_dy,
                               config, dt_scale);
    }

    if (config->sticks.right_stick_mode == STICK_MODE_MOUSE) {
        process_stick_as_mouse(right_x, right_y,
                               &state->smoothed_right_x,
                               &state->smoothed_right_y,
                               &state->mouse_dx, &state->mouse_dy,
                               config, dt_scale);
    }

    // Held scroll bindings repeat after a short delay, like a held key
    for (int i = 0; i < 2; i++) {
        if (!state->scroll_refs[i]) continue;
        float before = state->scroll_held_ms[i];
        float after = before + dt_ms;
        state->scroll_held_ms[i] = after;
        if (after < SCROLL_REPEAT_DELAY_MS) continue;
        // Notches due = repeat slots crossed since the delay elapsed
        int due_before = before < SCROLL_REPEAT_DELAY_MS ? 0
                         : (int)((before - SCROLL_REPEAT_DELAY_MS) / SCROLL_REPEAT_MS) + 1;
        int due_after = (int)((after - SCROLL_REPEAT_DELAY_MS) / SCROLL_REPEAT_MS) + 1;
        for (int k = due_before; k < due_after; k++) {
            send_scroll_event(scroll_direction(i));
        }
    }

    // Send accumulated mouse movement
    if (state->mouse_dx != 0.0f || state->mouse_dy != 0.0f) {
        send_mouse_movement(state->mouse_dx, state->mouse_dy, streaming_mode,
                            state->mouse_left, state->mouse_right, state->mouse_middle);
        state->mouse_dx = 0.0f;
        state->mouse_dy = 0.0f;
    }
}
