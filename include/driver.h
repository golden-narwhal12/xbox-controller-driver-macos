/*******************************************************************************
 * driver.h - Main driver declarations
 ******************************************************************************/

#ifndef DRIVER_H
#define DRIVER_H

#include "types.h"
#include "config.h"
#include "cursor.h"
#include "usb.h"
#include <pthread.h>
#include <time.h>

/*******************************************************************************
 * Connection status, reported to an optional observer (the menu bar)
 ******************************************************************************/
typedef enum {
    DRIVER_STATUS_CONNECTED,
    DRIVER_STATUS_DISCONNECTED,    // Unplugged; reconnect attempts follow
    DRIVER_STATUS_FAILED           // Gave up; the driver is exiting
} DriverStatus;

typedef void (*DriverStatusCallback)(DriverStatus status);

/*******************************************************************************
 * Driver Context
 *
 * Threads and what they own:
 *   packet thread (driver_run's caller): USB, config reload, `verbose`
 *   motion thread: the 10 ms tick
 * Shared fields and their locks are listed in src/core/driver.c.
 ******************************************************************************/
typedef struct {
    UsbContext usb;

    /* Guarded by the state mutex */
    ControllerMapping config;
    InputState input_state;
    GipInput last_input;           // Replayed after a config reload
    bool have_input;

    /* Guarded by the output mutex */
    CursorState cursor;

    /* Packet thread only */
    char config_path[CONFIG_PATH_MAX];
    struct timespec config_last_modified;
    bool verbose;

    pthread_t motion_thread;
    bool motion_thread_started;
    DriverStatusCallback status_callback;
} DriverContext;

/*******************************************************************************
 * Driver Functions
 ******************************************************************************/
int driver_init(DriverContext *ctx, const char *config_path);
/* Called from the packet thread on connection changes. Set before driver_run. */
void driver_set_status_callback(DriverContext *ctx, DriverStatusCallback callback);
int driver_run(DriverContext *ctx);
void driver_cleanup(DriverContext *ctx);

/*******************************************************************************
 * Requests (safe from any thread; driver_request_stop also from a signal)
 ******************************************************************************/
void driver_request_stop(void);
void driver_request_reload(void);
bool driver_should_stop(void);

#endif // DRIVER_H
