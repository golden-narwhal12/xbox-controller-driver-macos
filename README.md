# Xbox One Controller Driver for macOS

![Downloads](https://img.shields.io/github/downloads/golden-narwhal12/xbox-controller-driver-macos/total)

A userspace driver that translates Xbox One controller input to keyboard and mouse events. Works system-wide in any application.

## Features

- Menu bar app with connection status
- JSON configuration with hot-reload (no rebuild needed)
- Automatic reconnection when controller is unplugged/replugged
- Rumble feedback on button press
- Customizable button mappings, stick modes, mouse sensitivity
- Keeps the controller's cursor and clicks inside the game window

## Default Mapping

- Left stick: WASD keys
- Right stick: Mouse movement
- Right trigger: Left click
- Left trigger: Right click
- A/B/X/Y: Space, C, R, F
- Bumpers: scroll (RB = scroll down; LB = scroll up)
- D-pad: Arrow keys

## Requirements

- macOS (tested on 26.1, 26.5, 27.2)
- A supported USB controller (see below)
- Homebrew with libusb and pkg-config

### Supported controllers

Other wired GIP (Xbox One / Series) pads can be added by listing their exact
VID:PID in `SUPPORTED_DEVICES[]` in `src/usb/usb.c` (only add controllers you
can test -- some models need extra init packets). Confirmed working:

| Controller                                             | VID:PID     | Tested |
| ------------------------------------------------------ | ----------- | ------ |
| Xbox One Controller (Model 1697)                       | `045e:02dd` | ✅     |
| PDP Wired Controller for Xbox Series X\|S (White Camo) | `0e6f:02de` | ✅     |
| PDP Wired Controller for Xbox Series X\|S (Blue Camo)  | `0e6f:02d8` | ✅     |
| PowerA/BDA XB1 Classic Controller                      | `24c6:581a` | ✅     |

## Installation

### Prebuilt binary

Download the latest `xbox-controller-macos-arm64.zip` from the [Releases page](https://github.com/golden-narwhal12/xbox-controller-driver-macos/releases/latest) (Apple Silicon), then:

```bash
brew install libusb
unzip xbox-controller-macos-arm64.zip && cd xbox-controller-macos-arm64
xattr -d com.apple.quarantine simulator simulator-cli   # binaries are unsigned
sudo ./simulator
```

### Build from source

```bash
brew install libusb pkg-config
make simulator
sudo ./simulator
```

Grant Accessibility permissions when prompted: System Settings > Privacy & Security > Accessibility > add your terminal app.

## Configuration

Edit `config/controller.json` to customize mappings when running from the repository. Changes are picked up automatically while running. An explicit `--config` path or a `./controller.json` file takes priority; outside the repository you can use `~/.config/xbox-controller/config.json`.

```json
{
    "buttons": { "a": "space", "b": "c", "x": "r", "y": "f" },
    "left_stick": { "mode": "wasd", "deadzone": 8000 },
    "right_stick": { "mode": "mouse", "sensitivity": 1.5, "curve": 1.8 },
    "triggers": { "left": "mouse_right", "right": "mouse_left" }
}
```

You can also place your config at `~/.config/xbox-controller/config.json`.

Key names: letters (a-z), numbers (0-9), space, tab, escape, return, left_shift, left_control, up, down, left, right, f1-f12, mouse_left, mouse_right, mouse_middle, scroll_up, scroll_down.

Buttons (including `dpad_up`, `dpad_down`, `dpad_left`, and `dpad_right`) and triggers can map to keys or mouse buttons; any of them can also be `disabled`. Each stick can also specify `up`, `down`, `left`, and `right` key names when using a keyboard mode. Trigger `threshold` remains on a 0-255 scale; the driver's 10-bit trigger readings are scaled to match it. A held trigger keeps its mouse button or key pressed until release.

`config/controller.json` lists every option with its default. Options added in this version:

| Option | Default | Meaning |
| --- | --- | --- |
| `left_stick.threshold`, `right_stick.threshold` | 0.30 | How far a stick in `wasd`/`arrows` mode must be pushed (outside the deadzone) to press its key |
| `left_stick.hysteresis`, `right_stick.hysteresis` | 0.05 | The key releases at `threshold - hysteresis`, so a stick resting near the threshold doesn't flicker |
| `right_stick.deadzone` | same as `left_stick.deadzone` | Separate deadzone for the right stick |
| `triggers.hysteresis` | 26 | Width of the press/release band around `threshold`: a trigger presses at about 55% and releases at about 45% |
| `advanced.confine_cursor` | `window` | `window`, `display` or `off` (see Cursor Confinement) |
| `advanced.confine_margin` | 12 | Points kept clear inside the window edges |
| `advanced.confine_top_inset` | 28 | Extra points kept clear at the top (title bar); not applied in fullscreen |
| `advanced.recenter_when_captured` | false | At the window edge, jump the cursor back to the middle of the window |

Out-of-range values are clamped and a value of the wrong type is ignored; both are reported in the log. A file that isn't valid JSON is rejected and the previous configuration keeps running.

## Build Targets

```bash
make simulator       # GUI version with menu bar (recommended)
make simulator-cli   # CLI-only version
make test            # Run unit tests
make test-asan       # Unit tests under AddressSanitizer + UBSan
make test-tsan       # Driver thread tests under ThreadSanitizer
make test-window     # Check window tracking against a real window (needs a GUI session)
make dist            # Package a release zip in dist/
make clean           # Remove build artifacts
```

## Streaming Mode

For game streaming apps like Moonlight or Parsec, enable streaming mode in your config:

```json
{
    "advanced": { "streaming_mode": true }
}
```

This posts relative mouse deltas so a held look stick can keep turning in apps that capture relative mouse input. Normal mode moves the macOS cursor. Movement while a mouse button is held is posted as a drag, which is needed for aiming or drawing gestures in some apps.

## Cursor Confinement

By default the controller's cursor stays inside the focused window of the frontmost app, and its clicks always land in that window. If the cursor is outside the window when you click (for example after moving the real mouse), it is moved inside first, so a controller click never reaches the desktop or another app. Only controller input is affected; the real mouse moves freely.

The window is found through the Accessibility API, using the same permission the driver already needs. With no focused window (the desktop), the cursor is limited to the displays.

To reach the menu bar, Dock or other windows with the controller, set `"advanced": { "confine_cursor": "display" }` (the previous behavior) or `"off"`.

## Troubleshooting

**Keys not working:** Add your terminal to Accessibility permissions in System Settings.

**Stick drift:** Increase the deadzone value in your config (default 8000, about 24%).

**Mouse too fast/slow:** Adjust sensitivity (default 1.5, higher = faster).

**Controller not found:** Make sure it's plugged in and you're running with sudo.

**Controller can't reach the menu bar, Dock or other windows:** That's cursor confinement; set `confine_cursor` to `display`.

**The window resizes itself when clicking:** Keep `confine_margin` at 12 or higher so clicks stay off the window's resize border.

## Limitations

- Simulates keyboard/mouse, not a virtual gamepad
- Some games that require a real controller won't work
- Looking around relies on the game reading mouse deltas (most games that capture the mouse do)
- Keys are on or off, so sticks and triggers can't give a game partial (analog) movement

## How It Works

The driver communicates with the controller via libusb using Microsoft's GIP protocol, then injects keyboard/mouse events through the macOS Accessibility API. This approach works because macOS blocks virtual HID device creation but allows event injection for assistive technology.

## License

MIT
