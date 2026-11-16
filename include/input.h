/*******************************************************************************
 * input.h - Input processing declarations
 ******************************************************************************/

#ifndef INPUT_H
#define INPUT_H

#include "types.h"
#include "gip_protocol.h"

/*******************************************************************************
 * Packet Processing
 *
 * Decodes one GIP input packet into this layer's conventions (stick +Y is
 * converted from GIP's "up" to screen "down") and runs buttons, triggers and
 * sticks through the translation pipeline.
 ******************************************************************************/
void process_input_packet(const GipInputPacket *packet, InputState *state,
                          const ControllerMapping *config,
                          void (*rumble_callback)(void *ctx), void *rumble_ctx);

/*******************************************************************************
 * Response Curve
 *
 * |norm|^curve from a 257-entry lookup table over [0, 1] (truncating, no
 * interpolation). Inputs beyond 1 return the table's last entry.
 ******************************************************************************/
float curve_lookup(float norm, float curve);

/*******************************************************************************
 * Deadzone Processing
 ******************************************************************************/
void apply_deadzone(int16_t *x, int16_t *y, int16_t deadzone);

/*******************************************************************************
 * Button Processing
 ******************************************************************************/
void process_buttons(uint16_t buttons, InputState *state, const ControllerMapping *config,
                     void (*rumble_callback)(void *ctx), void *rumble_ctx);

/*******************************************************************************
 * Trigger Processing
 ******************************************************************************/
void process_triggers(uint16_t left_trigger, uint16_t right_trigger,
                      InputState *state, const ControllerMapping *config);

/*******************************************************************************
 * Stick Processing
 ******************************************************************************/
void process_stick_as_keys(int16_t x, int16_t y,
                           uint16_t key_up, uint16_t key_down,
                           uint16_t key_left, uint16_t key_right,
                           InputState *state, int source_base);

void process_stick_as_mouse(int16_t x, int16_t y,
                            float *smoothed_x, float *smoothed_y,
                            float *mouse_dx, float *mouse_dy,
                            const ControllerMapping *config, float dt_scale);

void process_sticks(int16_t left_x, int16_t left_y,
                    int16_t right_x, int16_t right_y,
                    InputState *state, const ControllerMapping *config,
                    bool streaming_mode);

/*******************************************************************************
 * Continuous Movement
 *
 * Called on a timer. dt_ms is the measured time since the previous call;
 * motion and smoothing are scaled by it (clamped to MOTION_MAX_DT_MS).
 ******************************************************************************/
void generate_continuous_movement(InputState *state, const ControllerMapping *config,
                                  bool streaming_mode, float dt_ms);

/*******************************************************************************
 * State Management
 ******************************************************************************/
void input_state_init(InputState *state);
void input_state_release_all(InputState *state);

#endif // INPUT_H
