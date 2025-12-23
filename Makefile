# Makefile for Xbox One Controller Driver

CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = $(shell pkg-config --cflags --libs libusb-1.0) -framework IOKit -framework CoreFoundation

# Targets
all: xbox_usb_test xbox_gip_test xbox_driver

xbox_usb_test: phase2_usb_test.c
	$(CC) $(CFLAGS) -o xbox_usb_test phase2_usb_test.c $(LIBS)
	@echo "\n✅ Built xbox_usb_test"
	@echo "Run with: sudo ./xbox_usb_test"

xbox_gip_test: phase3_gip_test.c gip.h
	$(CC) $(CFLAGS) -o xbox_gip_test phase3_gip_test.c $(LIBS)
	@echo "\n✅ Built xbox_gip_test"
	@echo "Run with: sudo ./xbox_gip_test"

xbox_driver: xbox_driver.c gip.h hid_descriptor.h virtual_hid.h
	$(CC) $(CFLAGS) -o xbox_driver xbox_driver.c $(LIBS)
	@echo "\n✅ Built complete driver"
	@echo "Run with: sudo ./xbox_driver"

clean:
	rm -f xbox_usb_test xbox_gip_test xbox_driver

install: xbox_driver
	sudo cp xbox_driver /usr/local/bin/
	sudo chmod +x /usr/local/bin/xbox_driver
	sudo cp com.xbox.controller.plist /Library/LaunchDaemons/
	sudo launchctl load /Library/LaunchDaemons/com.xbox.controller.plist
	@echo "\n✅ Driver installed and running!"

uninstall:
	sudo launchctl unload /Library/LaunchDaemons/com.xbox.controller.plist
	sudo rm /Library/LaunchDaemons/com.xbox.controller.plist
	sudo rm /usr/local/bin/xbox_driver
	@echo "\n✅ Driver uninstalled"

.PHONY: all clean install uninstall
