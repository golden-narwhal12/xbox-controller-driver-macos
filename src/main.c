/*******************************************************************************
 * main.c - Entry point for the Xbox Controller Simulator
 ******************************************************************************/

#include "../include/driver.h"
#include <stdio.h>
#include <string.h>

static void print_usage(const char *program) {
    printf("Usage: %s [OPTIONS]\n\n", program);
    printf("Options:\n");
    printf("  --config <path>    Load configuration from specified file\n");
    printf("  --help             Show this help message\n");
    printf("\n");
    printf("Configuration file search order:\n");
    printf("  1. --config argument\n");
    printf("  2. ./controller.json\n");
    printf("  3. ~/.config/xbox-controller/config.json\n");
    printf("  4. Built-in defaults\n");
}

int main(int argc, char *argv[]) {
    const char *config_path = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Initialize driver
    DriverContext ctx;
    if (driver_init(&ctx, config_path) != 0) {
        fprintf(stderr, "Failed to initialize driver\n");
        return 1;
    }

    // Run driver
    int result = driver_run(&ctx);

    // Cleanup
    driver_cleanup(&ctx);

    return result;
}
