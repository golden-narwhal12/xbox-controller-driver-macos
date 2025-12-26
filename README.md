# Xbox One Controller Driver for macOS

A userspace driver for Xbox One controllers that translates controller input to keyboard and mouse events. Built from scratch using reverse-engineered GIP protocol. This driver can be used for game streaming (read below).

## What it does

Reads input from Xbox One controllers via USB and converts it to keyboard/mouse input that works system-wide. You can use your Xbox controller in any app that accepts keyboard/mouse.

**Default mapping:**
- Left stick → WASD
- Right stick → Mouse movement
- Triggers → Mouse clicks
- Buttons → Various keys (Space, E, R, etc.)
- D-pad → Arrow keys

All mappings are customizable in `keymapping.h`.

## Requirements

- Should work with new versions of macOS (tested on macOS Tahoe 26.1)
- Xbox One controller with USB cable (Model 1697 confirmed working)
- Homebrew, libusb, pkg-config
- Xcode command line tools

## Installation

```bash
# Install dependencies
brew install libusb pkg-config
xcode-select --install

# Build
make simulator

# Run (requires sudo for USB access)
sudo ./simulator
```

**Important:** macOS requires Accessibility permissions for keyboard/mouse injection. After running once, go to System Settings → Privacy & Security → Accessibility and add your terminal app if not there already.

## Customization

Edit `keymapping.h` to change button mappings, stick behavior, mouse sensitivity, or deadzone. Every setting is documented in that file. After changes, rebuild with `make simulator`.

- Change what buttons do (e.g., A button = Enter instead of Space)
- Adjust mouse sensitivity/deadzone
- Switch stick modes (WASD, arrows, mouse, or disabled)
- Change trigger behavior (mouse buttons or keys)

## For game streaming 

If you want to use this driver while game streaming, please change variable "streaming_mode" in the keymapping.h file to "true" and rebuild the program.

## How it works

1. Communicates directly with controller via libusb
2. Implements Microsoft's GIP (Gaming Input Protocol)
3. Translates analog inputs (sticks/triggers) to digital outputs (keys/mouse)
4. Injects events using macOS Core Graphics API

The controller sends input packets at ~100Hz. We apply deadzones, convert analog stick positions to key presses or mouse deltas, and send the events system-wide.

## Limitations

- **Model 1697 tested** - other Xbox One controllers may have different packet formats
- **No force feedback** - rumble not implemented
- **Accessibility permissions required** - macOS security restriction
- **Not a virtual gamepad** - simulates keyboard/mouse inputs
- **Requires sudo** - needed for USB device access

## Files

- `simulator.c` - Main program with keyboard/mouse injection
- `keymapping.h` - Configuration for all bindings (edit this!)
- `gip.h` - GIP protocol definitions
- `phase3_gip_test.c` - Test program without keyboard/mouse (console output only)
- `phase2_usb_test.c` - USB diagnostics
- `hid_descriptor.h` - HID descriptor (reference)

## Testing without keyboard/mouse virtualization

If you want to see raw controller input without keyboard/mouse injection:

```bash
make xbox_gip_test
sudo ./xbox_gip_test
```

## Troubleshooting

**Keys not working:** Check Accessibility permissions in System Settings. Your terminal must be in the allowed apps list.

**Stick drift or wrong sensitivity:** Adjust `deadzone` in `keymapping.h` (default is 8000 = ~24%). Rebuild after changes.

**Mouse too fast/slow:** Change `mouse_sensitivity` in `keymapping.h`.

## Known issues

- Some third-party Xbox controllers may not work (different vendor/product IDs)
- Controller must be plugged in before starting the program
- No hot-plug detection (restart program if you unplug/replug)

## Why keyboard/mouse instead of a virtual controller?
The ideal solution would be creating a virtual HID gamepad that macOS sees as a real controller. Unfortunately, recent macOS versions block userspace programs from creating virtual HID devices as a security measure. Kernel extensions (kexts) could work around this, but Apple deprecated those and now requires onerous signing/notarization processes.

Keyboard and mouse emulation works because macOS allows it through the Accessibility API (originally designed for assistive technology). It's not perfect—you lose analog precision and can't use the controller in apps that only support gamepads—but it works in most games and apps since they accept keyboard/mouse input anyway.

This is why the driver can read every button press perfectly but has to convert analog sticks to WASD keys and mouse movement.

## Credits

Reverse-engineered from Linux xpad/xow drivers and Microsoft GIP documentation. Protocol packet structure figured out through USB captures and trial-and-error.

## License

MIT License
