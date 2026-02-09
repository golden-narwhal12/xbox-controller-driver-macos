/*******************************************************************************
 * input.h - Input processing declarations
 ******************************************************************************/

#ifndef INPUT_H
#define INPUT_H

#include "types.h"

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
void process_triggers(uint8_t left_trigger, uint8_t right_trigger,
                      InputState *state, const ControllerMapping *config);

/*******************************************************************************
 * Stick Processing
 ******************************************************************************/
void process_stick_as_keys(int16_t x, int16_t y,
                           uint16_t key_up, uint16_t key_down,
                           uint16_t key_left, uint16_t key_right,
                           InputState *state);

void process_stick_as_mouse(int16_t x, int16_t y,
                            float *smoothed_x, float *smoothed_y,
                            float *mouse_dx, float *mouse_dy,
                            const ControllerMapping *config);

void process_sticks(int16_t left_x, int16_t left_y,
                    int16_t right_x, int16_t right_y,
                    InputState *state, const ControllerMapping *config,
                    bool streaming_mode);

/*******************************************************************************
 * Continuous Movement
 ******************************************************************************/
void generate_continuous_movement(InputState *state, const ControllerMapping *config,
                                  bool streaming_mode);

/*******************************************************************************
 * State Management
 ******************************************************************************/
void input_state_init(InputState *state);
void input_state_release_all(InputState *state);

#endif // INPUT_H
