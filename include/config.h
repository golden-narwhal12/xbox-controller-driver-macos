/*******************************************************************************
 * config.h - Runtime configuration management
 ******************************************************************************/

#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"
#include <time.h>

/*******************************************************************************
 * Configuration File Paths
 ******************************************************************************/
#define CONFIG_PATH_LOCAL "./controller.json"
#define CONFIG_PATH_USER "/.config/xbox-controller/config.json"
#define CONFIG_PATH_MAX 512

/*******************************************************************************
 * Configuration Functions
 ******************************************************************************/

/**
 * Get default controller mapping (compile-time defaults from keymapping.h)
 */
void config_get_defaults(ControllerMapping *mapping);

/**
 * Load configuration from file
 * Returns 0 on success, -1 on error (defaults are used)
 */
int config_load(const char *path, ControllerMapping *mapping);

/**
 * Load configuration with automatic path detection
 * Priority: CLI arg > ./controller.json > ~/.config/xbox-controller/config.json > defaults
 */
int config_load_auto(const char *cli_path, ControllerMapping *mapping, char *loaded_path, size_t path_size);

/**
 * Reload configuration if file has changed
 * Returns 1 if reloaded, 0 if unchanged, -1 on error
 */
int config_reload_if_changed(const char *path, ControllerMapping *mapping, time_t *last_modified);

/**
 * Save current configuration to file
 */
int config_save(const char *path, const ControllerMapping *mapping);

/**
 * Parse key name to keycode
 * Returns keycode or 0xFFFF on error
 */
uint16_t config_parse_key(const char *key_name);

/**
 * Get key name from keycode
 */
const char* config_key_name(uint16_t keycode);

/**
 * Print current configuration
 */
void config_print(const ControllerMapping *mapping);

#endif // CONFIG_H
