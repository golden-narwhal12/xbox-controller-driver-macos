# Makefile for Xbox Controller Driver
# Requires: libusb, CoreGraphics (macOS framework)

CC = gcc
OBJCC = clang
WARNINGS = -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef
CFLAGS = $(WARNINGS) -O2 -I./include
OBJCFLAGS = -Wall -Wextra -Wshadow -O2 -I./include -fobjc-arc
DEPFLAGS = -MMD -MP
LIBUSB_CFLAGS = $(shell pkg-config --cflags libusb-1.0)
LIBUSB_LIBS = $(shell pkg-config --libs libusb-1.0)
FRAMEWORK_FLAGS = -framework CoreGraphics -framework ApplicationServices
COCOA_FLAGS = -framework Cocoa
LDFLAGS = -lm -lpthread

# Source files
SRC_LOG = src/log.c
SRC_CONFIG = src/config/config.c
SRC_EVENT = src/event/event.c
SRC_INPUT = src/input/input.c
SRC_CURSOR = src/input/cursor.c
SRC_TICK = src/input/tick.c
SRC_WINDOW = src/platform/window.c
SRC_CLOCK = src/platform/clock.c
SRC_USB = src/usb/usb.c
SRC_DRIVER = src/core/driver.c
SRC_MAIN_CLI = src/main.c
SRC_MAIN_GUI = src/main_gui.c
SRC_MENUBAR = src/gui/menubar.m

# Object files
OBJ_LOG = build/log.o
OBJ_CONFIG = build/config.o
OBJ_EVENT = build/event.o
OBJ_INPUT = build/input.o
OBJ_CURSOR = build/cursor.o
OBJ_TICK = build/tick.o
OBJ_WINDOW = build/window.o
OBJ_CLOCK = build/clock.o
OBJ_USB = build/usb.o
OBJ_DRIVER = build/driver.o
OBJ_MAIN_CLI = build/main_cli.o
OBJ_MAIN_GUI = build/main_gui.o
OBJ_MENUBAR = build/menubar.o

# Objects shared by both drivers
OBJS_CORE = $(OBJ_LOG) $(OBJ_CONFIG) $(OBJ_EVENT) $(OBJ_INPUT) $(OBJ_CURSOR) $(OBJ_TICK) \
            $(OBJ_WINDOW) $(OBJ_CLOCK) $(OBJ_USB) $(OBJ_DRIVER)

# All objects for CLI driver
OBJS_CLI = $(OBJS_CORE) $(OBJ_MAIN_CLI)

# All objects for GUI driver (default)
OBJS_GUI = $(OBJS_CORE) $(OBJ_MAIN_GUI) $(OBJ_MENUBAR)

# Targets - GUI is the default
all: simulator simulator-cli

# Build directory
build:
	mkdir -p build

# Object file compilation
$(OBJ_LOG): $(SRC_LOG) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_CONFIG): $(SRC_CONFIG) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_EVENT): $(SRC_EVENT) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_INPUT): $(SRC_INPUT) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_CURSOR): $(SRC_CURSOR) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_TICK): $(SRC_TICK) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_WINDOW): $(SRC_WINDOW) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_CLOCK): $(SRC_CLOCK) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_USB): $(SRC_USB) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) $(LIBUSB_CFLAGS) -c $< -o $@

$(OBJ_DRIVER): $(SRC_DRIVER) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) $(LIBUSB_CFLAGS) -c $< -o $@

$(OBJ_MAIN_CLI): $(SRC_MAIN_CLI) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) $(LIBUSB_CFLAGS) -c $< -o $@

$(OBJ_MAIN_GUI): $(SRC_MAIN_GUI) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) $(LIBUSB_CFLAGS) -c $< -o $@

$(OBJ_MENUBAR): $(SRC_MENUBAR) | build
	$(OBJCC) $(OBJCFLAGS) $(DEPFLAGS) -c $< -o $@

# Main simulator with GUI (default)
simulator: $(OBJS_GUI)
	$(CC) $(CFLAGS) $(OBJS_GUI) $(LIBUSB_LIBS) $(FRAMEWORK_FLAGS) $(COCOA_FLAGS) $(LDFLAGS) -o $@
	@echo ""
	@echo "Built simulator successfully!"
	@echo "   Run with: sudo ./simulator"
	@echo ""
	@echo "A menu bar icon will appear when running."
	@echo ""
	@echo "IMPORTANT: Grant Accessibility permissions:"
	@echo "   System Settings -> Privacy & Security -> Accessibility"
	@echo "   Add your terminal app to the allowed list"

# CLI-only simulator (no menu bar)
simulator-cli: $(OBJS_CLI)
	$(CC) $(CFLAGS) $(OBJS_CLI) $(LIBUSB_LIBS) $(FRAMEWORK_FLAGS) $(LDFLAGS) -o $@
	@echo ""
	@echo "Built simulator-cli successfully!"
	@echo "   Run with: sudo ./simulator-cli"
	@echo ""
	@echo "This is the CLI-only version (no menu bar)."

# Test targets
#
# Every suite links the real sources (TEST_SRCS). The only code replaced is the
# OS itself: tests/fake_platform.h implements include/platform.h.
TEST_SRCS = $(SRC_INPUT) $(SRC_CURSOR) $(SRC_TICK) $(SRC_CONFIG) $(SRC_LOG)
TEST_SUITES = test_input test_input_runtime test_pwm test_cursor test_tick test_config test_lut test_driver
TEST_BINS = $(TEST_SUITES:%=build/%)

build/test_%: tests/test_%.c $(TEST_SRCS) $(wildcard include/*.h) $(wildcard tests/*.h) | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< $(TEST_SRCS) -o $@ -lm

# The driver suite also links the real driver.c, against a scripted USB layer
# (so no libusb at link time, only its header).
build/test_driver: tests/test_driver.c $(TEST_SRCS) $(SRC_DRIVER) $(wildcard include/*.h) $(wildcard tests/*.h) | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $(LIBUSB_CFLAGS) $< $(TEST_SRCS) $(SRC_DRIVER) -o $@ -lm -lpthread

test: $(TEST_BINS)
	@for suite in $(TEST_SUITES); do ./build/$$suite || exit 1; done
	@echo ""
	@echo "All tests completed!"

# One suite at a time: make test-input, make test-cursor, ...
test-%: build/test_%
	@./$<

test-input-runtime: build/test_input_runtime
	@./$<

# The same suites under AddressSanitizer + UndefinedBehaviorSanitizer
test-asan:
	$(MAKE) -B test TEST_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"

# The driver thread suite under ThreadSanitizer: reports any data race between
# the packet thread and the motion thread
test-tsan:
	$(MAKE) -B build/test_driver TEST_FLAGS="-fsanitize=thread -g"
	@./build/test_driver
	@$(MAKE) -B build/test_driver >/dev/null

# Opt-in integration test: opens a real window, moves and resizes it, and
# checks the window tracker reports matching frames. Not part of `make test`
# because it needs a GUI session (and Accessibility permission to exercise the
# Accessibility path; without it the window-list fallback is tested).
test-window: tests/integration_window.m $(SRC_WINDOW) $(SRC_LOG) | build
	$(OBJCC) $(OBJCFLAGS) tests/integration_window.m $(SRC_WINDOW) $(SRC_LOG) \
		$(FRAMEWORK_FLAGS) $(COCOA_FLAGS) -lpthread -o build/integration_window
	@./build/integration_window

# Release package (zip of binaries + default config), for GitHub Releases
ARCH = $(shell uname -m)
DIST_NAME = xbox-controller-macos-$(ARCH)

dist: simulator simulator-cli
	rm -rf dist/$(DIST_NAME)
	mkdir -p dist/$(DIST_NAME)
	cp simulator simulator-cli README.md LICENSE dist/$(DIST_NAME)/
	cp config/controller.json dist/$(DIST_NAME)/controller.json
	cd dist && zip -qr $(DIST_NAME).zip $(DIST_NAME)
	@echo "Created dist/$(DIST_NAME).zip"

# Clean
clean:
	rm -rf build dist
	rm -f simulator simulator-cli
	@echo "Cleaned up build artifacts"

# Install dependencies (homebrew)
deps:
	@echo "Installing dependencies..."
	brew install libusb pkg-config
	@echo "Dependencies installed"

# Install configuration
install-config:
	@mkdir -p ~/.config/xbox-controller
	@cp config/controller.json ~/.config/xbox-controller/config.json
	@echo "Configuration installed to ~/.config/xbox-controller/config.json"

# Help
help:
	@echo "Xbox Controller Driver - Build System"
	@echo "======================================"
	@echo ""
	@echo "Build Targets:"
	@echo "  make all            - Build all programs"
	@echo "  make simulator      - Build the simulator with menu bar GUI (default)"
	@echo "  make simulator-cli  - Build the CLI-only simulator (no menu bar)"
	@echo ""
	@echo "Test Targets:"
	@echo "  make test           - Run all unit tests"
	@echo "  make test-<suite>   - Run one suite: input, input-runtime, pwm, cursor,"
	@echo "                        tick, config, lut, driver"
	@echo "  make test-asan      - Run all unit tests under ASan + UBSan"
	@echo "  make test-tsan      - Run the driver thread tests under ThreadSanitizer"
	@echo "  make test-window    - Integration test of window tracking (needs a GUI session)"
	@echo ""
	@echo "Usage:"
	@echo "  sudo ./simulator          - Run with menu bar GUI"
	@echo "  sudo ./simulator-cli      - Run CLI-only version"
	@echo "  ./simulator --config path - Use custom config file"
	@echo "  ./simulator --help        - Show help"
	@echo ""
	@echo "Configuration:"
	@echo "  Edit config/controller.json to customize button bindings"
	@echo "  Or use --config flag to specify a custom config file"
	@echo "  Or place config at ~/.config/xbox-controller/config.json"
	@echo ""
	@echo "Other Targets:"
	@echo "  make clean          - Remove all built files"
	@echo "  make deps           - Install dependencies (libusb, pkg-config)"
	@echo "  make install-config - Install config to ~/.config/xbox-controller/"
	@echo ""
	@echo "Note: Requires accessibility permissions for keyboard/mouse input"

.PHONY: all dist clean deps help test test-asan test-tsan test-window install-config

-include $(wildcard build/*.d)
