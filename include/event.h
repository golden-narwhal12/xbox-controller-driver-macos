/*******************************************************************************
 * event.h - Event injection declarations (keyboard/mouse)
 ******************************************************************************/

#ifndef EVENT_H
#define EVENT_H

#include "types.h"

/*******************************************************************************
 * Mouse Button Constants (match CGMouseButton values)
 ******************************************************************************/
#define MOUSE_BUTTON_LEFT   0
#define MOUSE_BUTTON_RIGHT  1
#define MOUSE_BUTTON_CENTER 2

/*******************************************************************************
 * Keyboard Event Functions
 ******************************************************************************/
void send_key_event(uint16_t keycode, bool pressed);

/*******************************************************************************
 * Mouse Event Functions
 ******************************************************************************/
void send_mouse_button_event(int button, bool pressed);
void send_mouse_movement(float dx, float dy, bool streaming_mode);

#endif // EVENT_H
