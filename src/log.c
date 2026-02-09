/*******************************************************************************
 * log.c - Logging implementation with levels and optional file output
 ******************************************************************************/

#include "../include/log.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>

static LogLevel g_log_level = LOG_LEVEL_INFO;
static FILE *g_log_file = NULL;

static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "NONE"
};

static const char *level_colors[] = {
    "\033[36m",  // DEBUG - Cyan
    "\033[32m",  // INFO - Green
    "\033[33m",  // WARN - Yellow
    "\033[31m",  // ERROR - Red
    ""           // NONE
};

static const char *color_reset = "\033[0m";

void log_init(LogLevel level, const char *file_path) {
    g_log_level = level;

    if (file_path != NULL) {
        g_log_file = fopen(file_path, "a");
        if (g_log_file == NULL) {
            fprintf(stderr, "Warning: Could not open log file: %s\n", file_path);
        }
    }
}

void log_cleanup(void) {
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void log_set_level(LogLevel level) {
    g_log_level = level;
}

LogLevel log_get_level(void) {
    return g_log_level;
}

void log_message(LogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < g_log_level) {
        return;
    }

    // Get timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    // Extract filename from path
    const char *filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    // Format message
    va_list args;
    char message[1024];
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // Print to stderr with colors
    fprintf(stderr, "%s[%s]%s [%s] %s:%d: %s\n",
            level_colors[level],
            level_names[level],
            color_reset,
            timestamp,
            filename,
            line,
            message);

    // Also write to log file if open (without colors)
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%s] [%s] %s:%d: %s\n",
                level_names[level],
                timestamp,
                filename,
                line,
                message);
        fflush(g_log_file);
    }
}
