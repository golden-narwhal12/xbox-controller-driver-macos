/*******************************************************************************
 * driver.h - Main driver declarations
 ******************************************************************************/

#ifndef DRIVER_H
#define DRIVER_H

#include "types.h"
#include "usb.h"
#include <time.h>

/*******************************************************************************
 * Driver Context
 ******************************************************************************/
typedef struct {
    UsbContext usb;
    ControllerMapping config;
    InputState input_state;
    char config_path[512];
    time_t config_last_modified;
    bool verbose;
} DriverContext;

/*******************************************************************************
 * Driver Functions
 ******************************************************************************/
int driver_init(DriverContext *ctx, const char *config_path);
void driver_cleanup(DriverContext *ctx);
int driver_run(DriverContext *ctx);

/*******************************************************************************
 * Input Loop
 ******************************************************************************/
void driver_input_loop(DriverContext *ctx);

/*******************************************************************************
 * Signal Handling
 ******************************************************************************/
void driver_request_stop(void);
bool driver_should_stop(void);

#endif // DRIVER_H
