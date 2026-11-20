/*******************************************************************************
 * input.c - Controller input to key/mouse intent (pure logic, no OS calls)
 ******************************************************************************/

#include "../../include/input.h"
#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/*******************************************************************************
 * Output queue
 ******************************************************************************/
void out_queue_init(OutQueue *queue) {
    queue->count = 0;
    queue->dropped = 0;
}

static void out_push(OutQueue *queue, OutEvent event) {
    if (queue->count < OUT_QUEUE_CAPACITY) {
        queue->events[queue->count++] = event;
    } else {
        queue->dropped++;
    }
}

/*******************************************************************************
 * Response curve lookup table
 ******************************************************************************/
#define CURVE_LUT_STEPS 256

static float g_curve_lut[CURVE_LUT_STEPS + 1];   // |norm|^curve at i / 256
static float g_curve_lut_exponent = 0.0f;        // 0 = table not built yet

float curve_lookup(float norm, float curve) {
    // Rebuild the table on first use or when the configured curve changes
    if (curve != g_curve_lut_exponent) {
        for (int i = 0; i <= CURVE_LUT_STEPS; i++) {
            g_curve_lut[i] = powf((float)i / CURVE_LUT_STEPS, curve);
        }
        g_curve_lut_exponent = curve;
    }

    float position = fabsf(norm) * CURVE_LUT_STEPS;
    if (!(position < CURVE_LUT_STEPS)) return g_curve_lut[CURVE_LUT_STEPS];
    int index = (int)position;
    float fraction = position - (float)index;
    return g_curve_lut[index] + fraction * (g_curve_lut[index + 1] - g_curve_lut[index]);
}

/*******************************************************************************
 * Bindings and reference counting
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

static void push_scroll(OutQueue *out, int which) {
    out_push(out, (OutEvent){.type = OUT_SCROLL, .lines = which == 0 ? 1 : -1});
}

/* Emits a down event when a binding gains its first holder and an up event
   when it loses its last one. BINDING_NONE (and anything unknown) is a no-op. */
static void change_binding_ref(InputState *state, uint16_t binding, bool pressed,
                               OutQueue *out) {
    int scroll = scroll_binding(binding);
    if (scroll >= 0) {
        // Scrolling has no "up" event: one notch on press, repeats are timed
        // in motion_tick() while the binding stays held.
        uint8_t *refs = &state->scroll_refs[scroll];
        if (pressed) {
            if ((*refs)++ == 0) {
                state->scroll_wait_ms[scroll] = SCROLL_REPEAT_DELAY_MS;
                push_scroll(out, scroll);
            }
        } else if (*refs) {
            --(*refs);
        }
        return;
    }

    int mouse = mouse_binding(binding);
    uint8_t *refs;
    OutEvent event = {.pressed = pressed};
    if (mouse >= 0) {
        refs = &state->mouse_refs[mouse];
        event.type = OUT_MOUSE_BUTTON;
        event.code = (uint16_t)mouse;
    } else if (binding < KEY_COUNT) {
        refs = &state->key_refs[binding];
        event.type = OUT_KEY;
        event.code = binding;
    } else {
        return;
    }

    if (pressed) {
        if ((*refs)++ == 0) out_push(out, event);
    } else if (*refs && --(*refs) == 0) {
        out_push(out, event);
    }
}

/* Returns true when this call is a new press of the source (its press edge). */
static bool set_source(InputState *state, int source, uint16_t binding, bool pressed,
                       OutQueue *out) {
    if (state->source_pressed[source] == pressed &&
        (!pressed || state->source_binding[source] == binding)) return false;

    bool press_edge = pressed && !state->source_pressed[source];
    if (state->source_pressed[source])
        change_binding_ref(state, state->source_binding[source], false, out);
    state->source_binding[source] = binding;
    state->source_pressed[source] = pressed;
    if (pressed) change_binding_ref(state, binding, true, out);
    return press_edge;
}

/*******************************************************************************
 * State management
 ******************************************************************************/
void input_state_init(InputState *state) {
    memset(state, 0, sizeof(*state));
}

void input_state_release_all(InputState *state, OutQueue *out) {
    for (int key = 0; key < KEY_COUNT; key++) {
        if (state->key_refs[key])
            out_push(out, (OutEvent){.type = OUT_KEY, .code = (uint16_t)key});
    }
    for (int button = 0; button < MOUSE_BUTTON_COUNT; button++) {
        if (state->mouse_refs[button])
            out_push(out, (OutEvent){.type = OUT_MOUSE_BUTTON, .code = (uint16_t)button});
    }
    input_state_init(state);
}

/*******************************************************************************
 * Report decoding
 ******************************************************************************/
static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool gip_parse_input(const uint8_t *data, size_t length, GipInput *out) {
    if (length < GIP_INPUT_LENGTH || data[0] != GIP_CMD_INPUT) return false;
    out->buttons       = read_le16(data + GIP_INPUT_OFF_BUTTONS);
    out->left_trigger  = read_le16(data + GIP_INPUT_OFF_LEFT_TRIGGER);
    out->right_trigger = read_le16(data + GIP_INPUT_OFF_RIGHT_TRIGGER);
    out->left_stick_x  = (int16_t)read_le16(data + GIP_INPUT_OFF_LEFT_X);
    out->left_stick_y  = (int16_t)read_le16(data + GIP_INPUT_OFF_LEFT_Y);
    out->right_stick_x = (int16_t)read_le16(data + GIP_INPUT_OFF_RIGHT_X);
    out->right_stick_y = (int16_t)read_le16(data + GIP_INPUT_OFF_RIGHT_Y);
    return true;
}

/*******************************************************************************
 * Buttons
 ******************************************************************************/
static const uint16_t BUTTON_MASKS[] = {
    [SOURCE_BTN_A]          = XBOX_BTN_A,
    [SOURCE_BTN_B]          = XBOX_BTN_B,
    [SOURCE_BTN_X]          = XBOX_BTN_X,
    [SOURCE_BTN_Y]          = XBOX_BTN_Y,
    [SOURCE_BTN_LB]         = XBOX_BTN_LB,
    [SOURCE_BTN_RB]         = XBOX_BTN_RB,
    [SOURCE_BTN_LS]         = XBOX_BTN_LS,
    [SOURCE_BTN_RS]         = XBOX_BTN_RS,
    [SOURCE_BTN_VIEW]       = XBOX_BTN_VIEW,
    [SOURCE_BTN_MENU]       = XBOX_BTN_MENU,
    [SOURCE_BTN_DPAD_UP]    = XBOX_BTN_DPAD_UP,
    [SOURCE_BTN_DPAD_DOWN]  = XBOX_BTN_DPAD_DOWN,
    [SOURCE_BTN_DPAD_LEFT]  = XBOX_BTN_DPAD_LEFT,
    [SOURCE_BTN_DPAD_RIGHT] = XBOX_BTN_DPAD_RIGHT,
};
_Static_assert(sizeof(BUTTON_MASKS) / sizeof(BUTTON_MASKS[0]) == SOURCE_BUTTON_COUNT,
               "BUTTON_MASKS must list exactly SOURCE_BUTTON_COUNT buttons");

static int process_buttons(uint16_t buttons, InputState *state,
                           const ControllerMapping *config, OutQueue *out) {
    int press_edges = 0;
    for (int source = 0; source < SOURCE_BUTTON_COUNT; source++) {
        bool pressed = (buttons & BUTTON_MASKS[source]) != 0;
        if (set_source(state, source, config->bindings[source], pressed, out)) press_edges++;
    }
    return press_edges;
}

/*******************************************************************************
 * Triggers
 *
 * The config threshold is the midpoint of a press/release band on a 0-255
 * scale; the comparison runs on the raw 10-bit value. A trigger presses above
 * the top of the band and releases at or below the bottom, so a finger resting
 * near the threshold cannot chatter.
 ******************************************************************************/
static bool trigger_pressed(uint16_t raw, bool was_pressed, const TriggerConfig *config) {
    const float scale = (float)TRIGGER_RAW_MAX / TRIGGER_CONFIG_MAX;
    float center = config->threshold * scale;
    float half_band = config->hysteresis * scale * 0.5f;
    // A full squeeze always presses and a fully released trigger always releases.
    float press_above = clampf(center + half_band, 0.0f, TRIGGER_RAW_MAX - 1.0f);
    float release_at = clampf(center - half_band, 0.0f, press_above);

    float value = raw > TRIGGER_RAW_MAX ? (float)TRIGGER_RAW_MAX : (float)raw;
    return was_pressed ? value > release_at : value > press_above;
}

static void process_triggers(uint16_t left, uint16_t right, InputState *state,
                             const ControllerMapping *config, OutQueue *out) {
    const uint16_t raw[TRIGGER_COUNT] = {left, right};
    for (int i = 0; i < TRIGGER_COUNT; i++) {
        int source = SOURCE_LEFT_TRIGGER + i;
        // source_pressed is the hysteresis state, even for an unbound trigger
        bool pressed = trigger_pressed(raw[i], state->source_pressed[source],
                                       &config->triggers);
        set_source(state, source, config->bindings[source], pressed, out);
    }
}

/*******************************************************************************
 * Sticks
 ******************************************************************************/
Vec2 stick_deadzone(int16_t x, int16_t y, int16_t deadzone) {
    float fx = (float)x, fy = (float)y;
    float magnitude = sqrtf(fx * fx + fy * fy);
    float dz = clampf((float)deadzone, 0.0f, (float)STICK_MAX - 1.0f);
    if (magnitude <= dz || magnitude == 0.0f) return (Vec2){0.0f, 0.0f};

    // Rescale so output starts at 0 on the deadzone edge instead of jumping,
    // and clamp: -32768 and the corners of the square exceed STICK_MAX.
    float scaled = (magnitude - dz) / ((float)STICK_MAX - dz);
    if (scaled > 1.0f) scaled = 1.0f;
    float k = scaled / magnitude;
    return (Vec2){fx * k, fy * k};
}

Vec2 stick_to_square(Vec2 v) {
    float ax = fabsf(v.x), ay = fabsf(v.y);
    float largest = ax > ay ? ax : ay;
    if (largest == 0.0f) return v;
    float k = sqrtf(v.x * v.x + v.y * v.y) / largest;
    return (Vec2){clampf(v.x * k, -1.0f, 1.0f), clampf(v.y * k, -1.0f, 1.0f)};
}

static bool stick_drives_keys(const StickConfig *stick) {
    return stick->mode == STICK_MODE_WASD || stick->mode == STICK_MODE_ARROWS;
}

static int stick_source(int stick, int direction) {
    return (stick == STICK_LEFT ? SOURCE_LEFT_STICK : SOURCE_RIGHT_STICK) + direction;
}

static uint16_t stick_binding(const ControllerMapping *config, int stick, int direction) {
    static const uint16_t ARROWS[STICK_DIR_COUNT] = {
        [STICK_DIR_UP] = KEY_UP_ARROW,     [STICK_DIR_DOWN] = KEY_DOWN_ARROW,
        [STICK_DIR_LEFT] = KEY_LEFT_ARROW, [STICK_DIR_RIGHT] = KEY_RIGHT_ARROW,
    };
    if (config->stick[stick].mode == STICK_MODE_ARROWS) return ARROWS[direction];
    return config->bindings[stick_source(stick, direction)];
}

/* How far the stick is pushed toward each direction (negative = away). */
static void stick_direction_values(Vec2 stick, float along[STICK_DIR_COUNT]) {
    Vec2 square = stick_to_square(stick);
    along[STICK_DIR_UP] = -square.y;       // screen convention: +Y is down
    along[STICK_DIR_DOWN] = square.y;
    along[STICK_DIR_LEFT] = -square.x;
    along[STICK_DIR_RIGHT] = square.x;
}

static void process_stick(int16_t raw_x, int16_t raw_y_screen, int stick,
                          InputState *state, const ControllerMapping *config,
                          OutQueue *out) {
    const StickConfig *sc = &config->stick[stick];
    state->stick[stick] = stick_deadzone(raw_x, raw_y_screen, sc->deadzone);

    if (!stick_drives_keys(sc)) {
        for (int d = 0; d < STICK_DIR_COUNT; d++) {
            set_source(state, stick_source(stick, d), BINDING_NONE, false, out);
            state->dir_active[stick][d] = false;
        }
        return;
    }

    float along[STICK_DIR_COUNT];
    stick_direction_values(state->stick[stick], along);
    float release_at = sc->threshold - sc->hysteresis;
    if (release_at < 0.0f) release_at = 0.0f;

    for (int d = 0; d < STICK_DIR_COUNT; d++) {
        bool active = state->dir_active[stick][d] ? along[d] > release_at
                                                  : along[d] >= sc->threshold;
        state->dir_active[stick][d] = active;

        int source = stick_source(stick, d);
        if (!sc->pwm) {
            set_source(state, source, stick_binding(config, stick, d), active, out);
        } else if (!active) {
            // PWM presses happen on the motion clock; letting go is immediate.
            set_source(state, source, BINDING_NONE, false, out);
        }
    }
}

/*
 * GIP reports stick +Y as up; everything else uses screen convention (+Y down,
 * matching CoreGraphics). Bitwise NOT maps 32767 -> -32768 and -32768 -> 32767,
 * avoiding the int16 overflow that unary minus has at -32768. Linux's xpad
 * driver performs the same conversion.
 */
static int16_t gip_y_to_screen(int16_t y) {
    return (int16_t)~y;
}

int process_input(const GipInput *input, InputState *state,
                  const ControllerMapping *config, OutQueue *out) {
    int press_edges = process_buttons(input->buttons, state, config, out);
    process_triggers(input->left_trigger, input->right_trigger, state, config, out);
    process_stick(input->left_stick_x, gip_y_to_screen(input->left_stick_y),
                  STICK_LEFT, state, config, out);
    process_stick(input->right_stick_x, gip_y_to_screen(input->right_stick_y),
                  STICK_RIGHT, state, config, out);
    return press_edges;
}

/*******************************************************************************
 * Motion tick: stick as mouse
 *
 * Order matters: smooth the (deadzoned) stick, apply the response curve to its
 * distance from center, then scale by sensitivity and elapsed time.
 ******************************************************************************/
static void stick_mouse_delta(Vec2 target, Vec2 *smoothed, const MouseConfig *mouse,
                              float dt_scale, float *dx, float *dy) {
    // Exponential smoothing. `smoothing` is the fraction of the previous value
    // kept per nominal tick; raising it to dt_scale keeps the filter's time
    // constant the same when a tick runs long.
    float keep = powf(mouse->smoothing, dt_scale);
    smoothed->x = (1.0f - keep) * target.x + keep * smoothed->x;
    smoothed->y = (1.0f - keep) * target.y + keep * smoothed->y;

    // After the stick is released the filter decays toward zero. The tail is
    // harmless: cursor_move() posts nothing until a whole point accumulates.
    float magnitude = sqrtf(smoothed->x * smoothed->x + smoothed->y * smoothed->y);
    if (magnitude == 0.0f) return;

    // Response curve on the stick's distance from center, keeping its
    // direction, so a diagonal turns as fast as straight left/right.
    float scale = curve_lookup(magnitude, mouse->curve) / magnitude;
    float gain = mouse->sensitivity * MOUSE_PX_PER_TICK * dt_scale;
    *dx += smoothed->x * scale * gain;
    *dy += smoothed->y * scale * gain;
}

/*******************************************************************************
 * Motion tick: stick PWM
 *
 * A partly pushed stick holds its key for part of each period. Per direction:
 *   duty = (tilt - threshold) / (pwm_full_on - threshold), clamped to 0..1.
 * Each period the key goes down at the start and comes up after its planned
 * on-time. Ticks are 10-14 ms apart, so one period can only hit the plan to
 * the nearest tick; `debt_ms` carries the difference into the next period,
 * which makes the long-run duty exact. A key never changes sooner than
 * pwm_min_pulse_ms after its last change, so no on- or off-time is shorter
 * than that. Stretching a pulse or a gap this way shows up as debt, so very
 * small duties become a minimum-length pulse every few periods and very large
 * ones a minimum-length gap every few periods; the average stays right.
 ******************************************************************************/
static void pwm_tick(int stick, InputState *state, const ControllerMapping *config,
                     float dt_ms, OutQueue *out) {
    const StickConfig *sc = &config->stick[stick];
    PwmStick *pwm = &state->pwm[stick];

    bool any_active = false;
    for (int d = 0; d < STICK_DIR_COUNT; d++) any_active |= state->dir_active[stick][d];
    if (!stick_drives_keys(sc) || !sc->pwm || !any_active) {
        // Idle: the next push starts a fresh period, so it presses at once.
        memset(pwm, 0, sizeof(*pwm));
        return;
    }

    float period = sc->pwm_period_ms;
    bool period_start = false;
    if (!pwm->running) {
        pwm->running = true;
        pwm->phase_ms = 0.0f;
    } else {
        pwm->phase_ms += dt_ms;
        if (pwm->phase_ms >= period) {
            pwm->phase_ms = fmodf(pwm->phase_ms, period);
            period_start = true;
        }
    }

    float along[STICK_DIR_COUNT];
    stick_direction_values(state->stick[stick], along);
    float duty_span = sc->pwm_full_on - sc->threshold;

    for (int d = 0; d < STICK_DIR_COUNT; d++) {
        PwmAxis *axis = &pwm->dir[d];
        if (!state->dir_active[stick][d]) {
            memset(axis, 0, sizeof(*axis));   // process_stick already released it
            continue;
        }

        int source = stick_source(stick, d);
        bool down = state->source_pressed[source];
        float duty = duty_span > 0.0f
                     ? clampf((along[d] - sc->threshold) / duty_span, 0.0f, 1.0f)
                     : 1.0f;

        bool starting = !axis->running;
        if (starting) {
            axis->running = true;
            axis->since_change_ms = period;   // first press is not delayed
        } else {
            // Account for the interval that just ended
            axis->debt_ms = clampf(axis->debt_ms + duty * dt_ms - (down ? dt_ms : 0.0f),
                                   -period, period);
            axis->since_change_ms += dt_ms;
            if (down) axis->on_ms += dt_ms;
        }

        bool want_down;
        if (duty >= 1.0f) {
            want_down = true;                 // held: no pulses, no releases
            axis->debt_ms = 0.0f;
            axis->plan_ms = period;
        } else {
            if (starting || period_start) {
                // A direction joining mid-period only has the rest of it
                float window = starting ? period - pwm->phase_ms : period;
                axis->plan_ms = duty * window + axis->debt_ms;
                axis->on_ms = 0.0f;
            }
            // Release on the tick nearest the plan; a full-period plan holds.
            want_down = axis->plan_ms >= period ||
                        axis->on_ms + dt_ms * 0.5f < axis->plan_ms;
        }

        if (want_down != down && axis->since_change_ms >= sc->pwm_min_pulse_ms) {
            set_source(state, source, stick_binding(config, stick, d), want_down, out);
            axis->since_change_ms = 0.0f;
            // One pulse per period: once released, stay up until the next
            // period starts (a shorter tick must not re-trigger the test above).
            if (!want_down) axis->plan_ms = 0.0f;
        }
    }
}

/*******************************************************************************
 * Motion tick
 ******************************************************************************/
static int held_mouse_button(const InputState *state) {
    for (int button = 0; button < MOUSE_BUTTON_COUNT; button++) {
        if (state->mouse_refs[button]) return button;   // left, right, middle
    }
    return MOUSE_BUTTON_NONE;
}

void motion_tick(InputState *state, const ControllerMapping *config,
                 float dt_ms, OutQueue *out) {
    if (!(dt_ms > 0.0f)) return;
    // Clamp so a long stall can't turn into one big jump.
    if (dt_ms > MOTION_MAX_DT_MS) dt_ms = MOTION_MAX_DT_MS;
    float dt_scale = dt_ms / MOTION_TICK_MS;

    float dx = 0.0f, dy = 0.0f;
    for (int stick = 0; stick < STICK_COUNT; stick++) {
        if (config->stick[stick].mode == STICK_MODE_MOUSE) {
            stick_mouse_delta(state->stick[stick], &state->smoothed[stick],
                              &config->mouse, dt_scale, &dx, &dy);
        }
        pwm_tick(stick, state, config, dt_ms, out);
    }

    // Held scroll bindings repeat after a short delay, like a held key
    for (int i = 0; i < 2; i++) {
        if (!state->scroll_refs[i]) continue;
        state->scroll_wait_ms[i] -= dt_ms;
        while (state->scroll_wait_ms[i] <= 0.0f) {
            push_scroll(out, i);
            state->scroll_wait_ms[i] += SCROLL_REPEAT_MS;
        }
    }

    if (dx != 0.0f || dy != 0.0f) {
        out_push(out, (OutEvent){.type = OUT_MOUSE_MOVE, .dx = dx, .dy = dy,
                                 .drag_button = held_mouse_button(state)});
    }
}
