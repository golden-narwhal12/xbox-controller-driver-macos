/*******************************************************************************
 * log.h - Logging macros and declarations
 ******************************************************************************/

#ifndef LOG_H
#define LOG_H

#include "types.h"
#include <stdio.h>

/*******************************************************************************
 * Logging Functions
 ******************************************************************************/
void log_init(LogLevel level, const char *file_path);
void log_cleanup(void);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);
void log_message(LogLevel level, const char *file, int line, const char *fmt, ...);

/*******************************************************************************
 * Logging Macros
 ******************************************************************************/
#define LOG_DEBUG(fmt, ...) \
    log_message(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    log_message(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    log_message(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    log_message(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // LOG_H
