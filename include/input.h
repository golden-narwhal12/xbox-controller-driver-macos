/*******************************************************************************
 * input.h - Controller input to key/mouse intent (pure logic, no OS calls)
 *
 * Everything here is deterministic: raw report bytes and elapsed time go in,
 * OutEvents come out on a queue. Posting those events is the caller's job
 * (cursor_dispatch), which is what lets tests run the real code.
 ******************************************************************************/

#ifndef INPUT_H
#define INPUT_H

#include "types.h"
#include "gip_protocol.h"

/*******************************************************************************
 * Output queue
 ******************************************************************************/
void out_queue_init(OutQueue *queue);

/*******************************************************************************
 * State
 ******************************************************************************/
void input_state_init(InputState *state);

/* Queue a release for every held key and mouse button, then reset the state.
   Used on unplug, config reload and shutdown. */
void input_state_release_all(InputState *state, OutQueue *out);

/*******************************************************************************
 * Report decoding
 *
 * Decodes a raw GIP input report (little-endian on the wire). Returns false
 * when the buffer is not an input report or is too short.
 ******************************************************************************/
bool gip_parse_input(const uint8_t *data, size_t length, GipInput *out);

/*******************************************************************************
 * Report processing
 *
 * Runs buttons, triggers and sticks through the translation pipeline.
 * Returns the number of buttons newly pressed by this report (for rumble).
 ******************************************************************************/
int process_input(const GipInput *input, InputState *state,
                  const ControllerMapping *config, OutQueue *out);

/*******************************************************************************
 * Motion tick
 *
 * Called on the motion clock. dt_ms is the measured time since the previous
 * call (clamped to MOTION_MAX_DT_MS). Drives stick-to-mouse motion and held
 * scroll repeat, neither of which depends on report arrival.
 ******************************************************************************/
void motion_tick(InputState *state, const ControllerMapping *config,
                 float dt_ms, OutQueue *out);

/*******************************************************************************
 * Building blocks (exposed for tests)
 ******************************************************************************/

/* Scaled radial deadzone: 0 inside the deadzone, then magnitude rises linearly
   from 0 at the edge to 1 at full tilt, keeping the stick's direction. */
Vec2 stick_deadzone(int16_t x, int16_t y, int16_t deadzone);

/* Stretches the unit circle onto the unit square so a full diagonal reads as
   (1, 1) rather than (0.71, 0.71). Used when a stick drives two keys. */
Vec2 stick_to_square(Vec2 v);

/* |norm|^curve from a 257-entry table over [0, 1], linearly interpolated.
   Inputs beyond 1 return the table's last entry. */
float curve_lookup(float norm, float curve);

#endif // INPUT_H
