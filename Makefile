# Makefile for Xbox Controller Driver
# Requires: libusb, CoreGraphics (macOS framework)

CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBUSB_FLAGS = $(shell pkg-config --cflags --libs libusb-1.0)
FRAMEWORK_FLAGS = -framework CoreGraphics -framework ApplicationServices

# Targets
all: xbox_usb_test xbox_gip_test simulator

# Phase 2: Basic USB test
xbox_usb_test: phase2_usb_test.c
	$(CC) $(CFLAGS) $< $(LIBUSB_FLAGS) -o $@

# Phase 3: GIP protocol test (read-only)
xbox_gip_test: phase3_gip_test.c gip.h
	$(CC) $(CFLAGS) $< $(LIBUSB_FLAGS) -o $@

# Simulator: Full keyboard/mouse emulator with customizable bindings
simulator: simulator.c gip.h keymapping.h
	$(CC) $(CFLAGS) $< $(LIBUSB_FLAGS) $(FRAMEWORK_FLAGS) -o $@ -lm
	@echo ""
	@echo "✅ Built simulator successfully!"
	@echo "   Run with: sudo ./simulator"
	@echo ""
	@echo "⚠️  IMPORTANT: Grant Accessibility permissions:"
	@echo "   System Settings → Privacy & Security → Accessibility"
	@echo "   Add your terminal app to the allowed list"
	@echo ""
	@echo "To customize key bindings, edit keymapping.h and rebuild"

# Clean
clean:
	rm -f xbox_usb_test xbox_gip_test simulator
	@echo "🧹 Cleaned up build artifacts"

# Install dependencies (homebrew)
deps:
	@echo "Installing dependencies..."
	brew install libusb pkg-config
	@echo "✅ Dependencies installed"

# Help
help:
	@echo "Xbox Controller Driver - Build System"
	@echo "======================================"
	@echo ""
	@echo "Build Targets:"
	@echo "  make all            - Build all programs"
	@echo "  make simulator      - Build the keyboard/mouse simulator (recommended)"
	@echo "  make xbox_gip_test  - Build GIP test (console output only)"
	@echo "  make xbox_usb_test  - Build USB test (diagnostics)"
	@echo ""
	@echo "Usage:"
	@echo "  sudo ./simulator       - Run the full simulator"
	@echo "  sudo ./xbox_gip_test   - Test controller input (no keyboard/mouse)"
	@echo ""
	@echo "Configuration:"
	@echo "  Edit keymapping.h to customize button bindings"
	@echo "  Rebuild with 'make simulator' after changes"
	@echo ""
	@echo "Other Targets:"
	@echo "  make clean - Remove all built files"
	@echo "  make deps  - Install dependencies (libusb, pkg-config)"
	@echo ""
	@echo "Note: Requires accessibility permissions for keyboard/mouse input"

.PHONY: all clean deps help
