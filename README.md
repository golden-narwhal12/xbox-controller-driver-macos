# Xbox One Controller Driver for macOS

A userspace driver that translates Xbox One controller input to keyboard and mouse events. Works system-wide in any application.

## Features

- Menu bar app with connection status
- JSON configuration with hot-reload (no rebuild needed)
- Automatic reconnection when controller is unplugged/replugged
- Rumble feedback on button press
- Customizable button mappings, stick modes, mouse sensitivity

## Default Mapping

- Left stick: WASD keys
- Right stick: Mouse movement
- Triggers: Mouse clicks (left/right)
- A/B/X/Y: Space, C, R, F
- Bumpers: Q, E
- D-pad: Arrow keys

## Requirements

- macOS (tested on Tahoe 26.1 and 26.5)
- A supported USB controller (see below)
- Homebrew with libusb and pkg-config

### Supported controllers

Other wired GIP (Xbox One / Series) pads can be added by listing their exact
VID:PID in `SUPPORTED_DEVICES[]` in `src/usb/usb.c` (only add controllers you
can test -- some models need extra init packets). Confirmed working:

| Controller | VID:PID | Tested |
| --- | --- | --- |
| Xbox One Controller (Model 1697) | `045e:02dd` | ✅ |
| PDP Wired Controller for Xbox Series X\|S | `0e6f:02de` | ✅ |

## Installation

```bash
brew install libusb pkg-config
make simulator
sudo ./simulator
```

Grant Accessibility permissions when prompted: System Settings > Privacy & Security > Accessibility > add your terminal app.

## Configuration

Edit `config/controller.json` to customize mappings. Changes are picked up automatically while running.

```json
{
  "buttons": { "a": "space", "b": "c", "x": "r", "y": "f" },
  "left_stick": { "mode": "wasd", "deadzone": 8000 },
  "right_stick": { "mode": "mouse", "sensitivity": 1.5, "curve": 1.8 },
  "triggers": { "left": "mouse_left", "right": "mouse_right" }
}
```

You can also place your config at `~/.config/xbox-controller/config.json`.

Key names: letters (a-z), numbers (0-9), space, tab, escape, return, left_shift, left_control, up, down, left, right, f1-f12, mouse_left, mouse_right.

## Build Targets

```bash
make simulator       # GUI version with menu bar (recommended)
make simulator-cli   # CLI-only version
make test            # Run unit tests
make clean           # Remove build artifacts
```

## Streaming Mode

For game streaming apps like Moonlight or Parsec, enable streaming mode in your config:

```json
{
  "advanced": { "streaming_mode": true }
}
```

## Troubleshooting

**Keys not working:** Add your terminal to Accessibility permissions in System Settings.

**Stick drift:** Increase the deadzone value in your config (default 8000, about 24%).

**Mouse too fast/slow:** Adjust sensitivity (default 1.5, higher = faster).

**Controller not found:** Make sure it's plugged in and you're running with sudo.

## Limitations

- Simulates keyboard/mouse, not a virtual gamepad
- Some games that require a real controller won't work

## How It Works

The driver communicates with the controller via libusb using Microsoft's GIP protocol, then injects keyboard/mouse events through the macOS Accessibility API. This approach works because macOS blocks virtual HID device creation but allows event injection for assistive technology.

## License

MIT
