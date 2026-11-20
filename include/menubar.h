/*******************************************************************************
 * menubar.h - Menu bar application interface (C interface for Objective-C)
 ******************************************************************************/

#ifndef MENUBAR_H
#define MENUBAR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Callback Types
 ******************************************************************************/
typedef void (*MenuBarReloadCallback)(void *context);
typedef void (*MenuBarQuitCallback)(void *context);

/*******************************************************************************
 * Menu Bar Functions
 ******************************************************************************/

/**
 * Initialize the menu bar application
 * Must be called from the main thread before menubar_run
 */
void menubar_init(void *context, MenuBarReloadCallback reload_cb, MenuBarQuitCallback quit_cb);

/**
 * Set the status text displayed in the menu
 * @param status "Connected", "Disconnected", "Reconnecting...", etc.
 */
void menubar_set_status(const char *status);

/**
 * Set connection state (changes icon appearance)
 */
void menubar_set_connected(bool connected);

/**
 * Run the menu bar application event loop
 * Must be called from the main thread
 * Blocks until the user chooses Quit or menubar_quit() is called, then
 * returns so the caller can clean up
 */
void menubar_run(void);

/**
 * Make menubar_run() return
 * Can be called from any thread
 */
void menubar_quit(void);

#ifdef __cplusplus
}
#endif

#endif // MENUBAR_H
