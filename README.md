# Xbox One Controller Driver for macOS

![Downloads](https://img.shields.io/github/downloads/golden-narwhal12/xbox-controller-driver-macos/total)

A userspace driver that translates Xbox One controller input to keyboard and mouse events. Works system-wide in any application.

## Features

- Menu bar app with connection status
- JSON configuration with hot-reload (no rebuild needed)
- Automatic reconnection when controller is unplugged/replugged
- Rumble feedback on button press
- Customizable button mappings, stick modes, mouse sensitivity
- Cursor confinement: the controller's mouse stays inside the focused window, and its clicks always land there
- Optional analog movement: a partly pushed stick walks slower (`wasd_pwm`)

## Default Mapping

| Control | Does |
| --- | --- |
| Left stick | W / A / S / D |
| Right stick | Mouse movement |
| Right trigger (RT) | Left click |
| Left trigger (LT) | Right click |
| A / B / X / Y | Space / C / R / F |
| LB / RB | Scroll up / scroll down (hold to repeat) |
| Left stick click / right stick click | Left Shift / Left Control |
| View / Menu | Tab / Escape |
| D-pad | Arrow keys |

Q and E are not bound to anything by default (E is the inventory key in Minecraft); bind them to any button you can spare, for example `"y": "e"`.

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

Edit `config/controller.json` to customize mappings when running from the repository. Changes are picked up automatically while running (within a second), or at once with **Reload Configuration** in the menu. The file is looked for in this order:

1. the path given with `--config`
2. `./controller.json`
3. `./config/controller.json`
4. `~/.config/xbox-controller/config.json`
5. built-in defaults

`config/controller.json` lists every option with its default value. You only need to include the ones you want to change:

```json
{
    "buttons": { "a": "space", "b": "c", "x": "r", "y": "e" },
    "left_stick": { "mode": "wasd_pwm" },
    "right_stick": { "sensitivity": 2.0 },
    "triggers": { "left": "mouse_right", "right": "mouse_left" }
}
```

If the file is not valid JSON it is rejected as a whole and the previous configuration keeps running. Inside a valid file, unknown keys are ignored, a value of the wrong type is ignored with a warning, a number outside its range is clamped to the range with a warning, and if a key appears twice the first one wins.

### Key names

Letters (`a`-`z`), numbers (`0`-`9`), `space`, `tab`, `escape`, `return`, `delete`, `left_shift`, `left_control`, `left_option`, `left_command` (and `right_` versions), `up`, `down`, `left`, `right`, `f1`-`f12`, `mouse_left`, `mouse_right`, `mouse_middle`, `scroll_up`, `scroll_down`, a raw keycode such as `0x31`, and `none` (or `disabled`) to leave a control unbound.

### Options

**`buttons`**: `a`, `b`, `x`, `y`, `lb`, `rb`, `ls`, `rs`, `view`, `menu`, `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`. Each takes a key name.

**`left_stick`** and **`right_stick`**

| Option | Default | Range | Meaning |
| --- | --- | --- | --- |
| `mode` | `wasd` (left), `mouse` (right) | `wasd`, `wasd_pwm`, `arrows`, `arrows_pwm`, `mouse`, `disabled` | What the stick does. The `_pwm` modes are described below. |
| `up`, `down`, `left`, `right` | W/S/A/D (left), I/K/J/L (right) | key names | Keys used in the `wasd` modes. |
| `deadzone` | 8000 | 0 - 30000 | Radius around center that reads as "not pushed" (stick range is 32767). `left_stick.deadzone` applies to both sticks unless `right_stick.deadzone` is also given. |
| `threshold` | 0.30 | 0.01 - 0.95 | Key modes: how far the stick must be pushed to press its key, as a fraction of the travel outside the deadzone. |
| `hysteresis` | 0.05 | 0 - 0.5 | Key modes: the key is released at `threshold - hysteresis`, so a stick held near the threshold does not flicker. |
| `pwm_period_ms` | 100 | 30 - 500 | PWM modes: length of one pulse cycle. |
| `pwm_full_on` | 0.90 | above `threshold`, up to 1.0 | PWM modes: at or beyond this tilt the key is simply held. |
| `pwm_min_pulse_ms` | 20 | 5 - half the period | PWM modes: no key press or gap is ever shorter than this. |

**`right_stick`** only (these apply to whichever stick is in `mouse` mode)

| Option | Default | Range | Meaning |
| --- | --- | --- | --- |
| `sensitivity` | 1.5 | 0.05 - 100 | Cursor speed. 1.0 is 1500 points per second at full tilt. |
| `curve` | 1.8 | 0.1 - 10 | Response curve exponent. Higher means finer control near the center. |
| `smoothing` | 0.3 | 0 - 0.95 | How much of the previous motion is kept each 10 ms. 0 is no smoothing. |

**`triggers`**

| Option | Default | Range | Meaning |
| --- | --- | --- | --- |
| `left`, `right` | `mouse_right`, `mouse_left` | key names, or `disabled` | What each trigger holds down while pulled. |
| `threshold` | 127 | 0 - 255 | Middle of the press/release band (127 is half pull). |
| `hysteresis` | 26 | 0 - 255 | Width of that band. The trigger presses above `threshold + hysteresis/2` and releases below `threshold - hysteresis/2`: about 55% and 45% of the pull with the defaults. A full pull always presses and a fully released trigger always releases. |

**`features.rumble`**

| Option | Default | Range | Meaning |
| --- | --- | --- | --- |
| `enabled` | true | | Master switch. |
| `button_feedback` | true | | Short pulse on each button press. |
| `intensity` | 100 | 0 - 255 | Motor strength. |
| `duration_ms` | 50 | 0 - 2550 | Pulse length. |

**`advanced`**

| Option | Default | Range | Meaning |
| --- | --- | --- | --- |
| `confine_cursor` | `window` | `window`, `display`, `off` | Where controller mouse motion and clicks may go. See below. |
| `confine_margin` | 12 | 0 - 200 | Points kept clear inside the window's edges, fullscreen windows included. Keeps clicks off the window's resize border and keeps the cursor off the screen edge (where macOS reveals the menu bar and Dock). Values under about 8 bring back accidental window resizing. |
| `confine_top_inset` | 28 | 0 - 400 | Extra points kept clear at the top (the title bar), so a drag cannot move or close the window. Not applied to fullscreen windows. |
| `recenter_when_captured` | false | | When motion hits the window edge, jump back to the middle of the window instead. Only useful in games that hide the cursor; elsewhere the jump is visible. |
| `streaming_mode` | false | | Post relative motion only and never move the cursor. See below. |
| `console_output` | true | | Print each controller report to the terminal. |
| `log_level` | `info` | `debug`, `info`, `warn`, `error`, `none` | `debug` also prints timing statistics once a second. |

## Analog movement (`wasd_pwm`)

A keyboard key is either down or up, so a game cannot tell a gentle push from a full one. With `"mode": "wasd_pwm"` the driver taps the key instead: a stick pushed halfway holds W for half of every 100 ms, a full push holds it continuously, and most games average that into slower walking.

- Below `threshold` the key is up; at or beyond `pwm_full_on` it is held with no pulsing.
- In between, the share of time the key is down rises in a straight line from 0% to 100%.
- Each direction has its own share, so pushing forward and slightly right holds W more than D. A full diagonal holds both.
- Presses and gaps are never shorter than `pwm_min_pulse_ms`, so a game running at 60 fps does not miss them. Very light pushes become one short tap every few cycles.
- Returning the stick to center releases the key immediately.

How smooth this feels depends on the game. If movement stutters, try a shorter `pwm_period_ms` (60) or raise `threshold`; if light pushes do nothing, lower `pwm_min_pulse_ms`.

## Cursor confinement

By default (`"confine_cursor": "window"`) the controller can only move the cursor inside the focused window of the frontmost app, and its clicks always land in that window:

- Stick motion stops just short of the window's edges (12 points inside, and below the title bar), so a click can never grab the window's resize border. It follows the window when it is moved or resized, and switches to the new window when you switch apps.
- If the cursor is outside the window when you click (you nudged the real mouse, or the window just shrank), the cursor is moved to the nearest point inside the window first, then the click is sent there. A controller click never reaches the desktop or another app.
- Games that capture the mouse keep turning for as long as you hold the stick, even with the cursor against an edge: every motion event carries the full movement.
- Only the controller is confined. The real mouse or trackpad moves freely, and the driver does nothing while only the mouse is moving.
- With no focused window (the desktop), motion is limited to the displays instead.

Because of this, in the default mode the controller cannot reach the menu bar, the Dock, other windows, or a window's title bar. To use the controller as a general desktop mouse, set `"confine_cursor": "display"` (anywhere on any display, the behavior of earlier versions) or `"off"` (no limits at all).

The driver finds the focused window through the Accessibility API, using the same permission it needs to send keys. Without that permission it falls back to the topmost window on screen.

## Build Targets

```bash
make simulator       # GUI version with menu bar (recommended)
make simulator-cli   # CLI-only version
make test            # Run unit tests
make test-asan       # Unit tests under AddressSanitizer + UBSan
make test-tsan       # Driver thread tests under ThreadSanitizer
make test-window     # Opens a real window to check window tracking (needs a GUI session)
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

In streaming mode the driver posts relative mouse motion and leaves the macOS cursor where it is; the streaming app forwards the motion to the remote machine. In normal mode the cursor moves, and the same relative motion is attached to every event, which is what lets games that capture the mouse keep turning. Movement while a mouse button is held is posted as a drag in both modes. Clicks are confined to the focused window in both modes.

## Troubleshooting

**Keys not working:** Add your terminal to Accessibility permissions in System Settings > Privacy & Security > Accessibility.

**The cursor will not leave the window / cannot reach the menu bar or Dock:** That is cursor confinement. Set `"advanced": { "confine_cursor": "display" }`.

**The cursor is confined to the wrong area, or not confined at all:** The driver needs Accessibility permission to read the focused window (the log shows "No Accessibility permission" when it is missing). A few apps do not report their windows; the driver then uses the topmost window on screen. If the top of a borderless game window is unreachable, set `"confine_top_inset": 0`.

**The window resizes itself when I click, or looking around stutters in fullscreen:** The cursor is too close to the window or screen edge. Make sure `confine_margin` is at its default (12) or larger. If fullscreen still stutters, try `"recenter_when_captured": true`.

**Stick drift:** Increase the deadzone value in your config (default 8000, about 24% of the stick's travel).

**Mouse too fast/slow:** Adjust sensitivity (default 1.5, higher = faster).

**WASD needs too much / too little push:** Adjust `left_stick.threshold` (default 0.30; lower presses sooner).

**Controller not found:** Make sure it's plugged in and you're running with sudo.

## Limitations

- Needs `sudo` to open the USB device
- Simulates keyboard/mouse, not a virtual gamepad
- Some games that require a real controller won't work
- Analog movement (`wasd_pwm`) is an approximation; how well it works depends on how the game samples the keyboard
- In the default confinement mode, menus and pop-ups that extend outside the focused window cannot be reached with the controller

## How It Works

The driver communicates with the controller via libusb using Microsoft's GIP protocol, then injects keyboard/mouse events through the macOS Accessibility API. This approach works because macOS blocks virtual HID device creation but allows event injection for assistive technology.

## License

MIT
