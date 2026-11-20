/*******************************************************************************
 * config.h - Runtime configuration management
 ******************************************************************************/

#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"
#include <stddef.h>
#include <time.h>

/*******************************************************************************
 * Configuration File Paths
 ******************************************************************************/
#define CONFIG_PATH_LOCAL "./controller.json"
#define CONFIG_PATH_PROJECT "./config/controller.json"
#define CONFIG_PATH_USER "/.config/xbox-controller/config.json"
#define CONFIG_PATH_MAX 512
#define CONFIG_PATH_DEFAULTS "(defaults)"
#define CONFIG_MAX_FILE_BYTES (1024 * 1024)

/**
 * Fill in the built-in defaults.
 */
void config_get_defaults(ControllerMapping *mapping);

/**
 * Load configuration from a JSON file.
 *
 * Starts from the defaults, so missing keys keep their default. Unknown keys
 * are ignored. If a key appears twice in an object, the first one wins. A
 * value of the wrong type is ignored with a warning; an out-of-range number
 * is clamped to its range with a warning.
 *
 * Returns 0 on success. Returns -1 if the file cannot be read or is not one
 * well-formed JSON object; `mapping` then holds the defaults.
 */
int config_load(const char *path, ControllerMapping *mapping);

/**
 * Load configuration with automatic path detection.
 * Priority: CLI arg > ./controller.json > ./config/controller.json >
 * ~/.config/xbox-controller/config.json > built-in defaults.
 * An explicit CLI path that cannot be loaded is an error (-1), not a fallback.
 */
int config_load_auto(const char *cli_path, ControllerMapping *mapping,
                     char *loaded_path, size_t path_size);

/**
 * Reload the configuration if the file is newer than *last_modified.
 * Returns 1 if reloaded, 0 if unchanged, -1 on error. On error `mapping` is
 * left untouched. *last_modified is advanced whenever the file was read, so
 * a broken file is reported once, not on every poll.
 */
int config_reload_if_changed(const char *path, ControllerMapping *mapping,
                             struct timespec *last_modified);

/**
 * Parse a key name ("space", "mouse_left", "0x31", ...) to a binding.
 * Returns BINDING_NONE for unknown names.
 */
uint16_t config_parse_key(const char *key_name);

/**
 * Name for a binding ("none" for BINDING_NONE, "unknown" if it has no name).
 */
const char* config_key_name(uint16_t binding);

/**
 * Print current configuration
 */
void config_print(const ControllerMapping *mapping);

#endif // CONFIG_H
