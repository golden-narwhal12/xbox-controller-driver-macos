# Xbox One Controller Driver for macOS

This driver is currently a proof-of-concept and CANNOT be used in games, or really give input to anything other than the command line. 

This is a userspace driver for Xbox One controllers on macOS, implementing the GIP (Gaming Input Protocol) from scratch.

This program was only tested with a Model 1697 xbox controller. Other models may run into issues.

## What This Does

Reads input from Xbox One controllers via USB and displays button presses, trigger values, and analog stick positions in real-time in the command line.

## Requirements

- macOS (tested on macOS Tahoe 26.1)
- Xcode Command Line Tools
- Homebrew
- libusb

## Installation

```bash
# Install dependencies
brew install libusb pkg-config
xcode-select --install

# Build the driver
make xbox_gip_test

# Run it
sudo ./xbox_gip_test
```

## Usage

1. Plug in your Xbox One controller via USB
2. Run `sudo ./xbox_gip_test`
3. Press buttons and move sticks - you'll see the input displayed in real-time
4. Press Ctrl+C to exit

## What Works

- ✅ Full USB communication with Xbox One controllers
- ✅ Complete GIP protocol implementation
- ✅ Real-time reading of all inputs (buttons, triggers, analog sticks)
- ✅ Proper initialization and power management

## Limitations

Creating a virtual HID device to make the controller usable in games is blocked by macOS security policies in recent versions. This driver successfully reads all controller input but cannot inject it as a virtual gamepad.

## Technical Details

- Uses libusb for direct USB communication
- Implements Microsoft's Gaming Input Protocol (GIP)
- Bypasses macOS's buggy native GIP implementation for Model 1697 XBOX Controller
- Written in C with no external dependencies beyond libusb

## Files

- `gip.h` - GIP protocol definitions and structures
- `hid_descriptor.h` - HID descriptor for gamepad layout
- `phase2_usb_test.c` - USB communication test
- `phase3_gip_test.c` - Main driver (reads controller input)
- `Makefile` - Build configuration

## Next Steps

I am looking into how to create a virtual HID device, so that the controller can actually have functionality. 

## License

MIT License

## Credits

Built by studying the Linux xpad and xow drivers, and through reverse engineering of the GIP protocol.
