# zmk-behavior-socd

A [ZMK](https://zmk.dev) external module that provides **SOCD (Simultaneous Opposing Cardinal Directions)** filtering for digital mechanical keyboards — commonly known as **Razer Snap Tap**.

When two opposing directional keys (e.g. A and D) are held at the same time, a configurable resolution strategy decides which key is sent to the computer. This enables instant direction changes ("counter-strafing") in games without needing to perfectly time key releases.

> **⚠️ Check game rules before using.** Some games (notably Counter-Strike 2) have banned SOCD filtering. It is your responsibility to disable SOCD where prohibited.

## Features

- 🎮 **Snap Tap** (last input priority) — the most recently pressed key wins; when released, the held opposing key reactivates
- ⚖️ **Neutral** — both keys cancel, neither is sent
- 🏆 **Key priority** — one key always wins over the other
- 🔧 **Multiple pairs** — define as many opposing key pairs as you need
- ⌨️ **Digital switches** — works with any standard mechanical switch, no Hall Effect hardware required

## Installation

### 1. Add to your `west.yml`

In your ZMK config repository, edit `config/west.yml` and add this module:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: brokenax3
      url-base: https://github.com/brokenax3
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    # Add the SOCD module:
    - name: zmk-behavior-socd
      remote: brokenax3
      revision: main
  self:
    path: config
```

### 2. Configure your keymap

#### Option A: Use the predefined WASD helper

Add this to the top of your `.keymap` file:

```dts
#include <behaviors/socd_wasd.dtsi>
```

This gives you two predefined SOCD pairs:
- `&socd_ad 0` / `&socd_ad 1` — A/D with last-input-priority
- `&socd_ws 0` / `&socd_ws 1` — W/S with last-input-priority

Then use them in your keymap:

```dts
/ {
    keymap {
        compatible = "zmk,keymap";
        default_layer {
            bindings = <
                &kp Q       &socd_ws 0  &kp E       &kp R       ...
                &socd_ad 0  &socd_ws 1  &socd_ad 1  &kp F       ...
            >;
        };
    };
};
```

#### Option B: Define custom SOCD pairs

```dts
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/socd.h>

/ {
    behaviors {
        socd_ad: socd_ad {
            compatible = "zmk,behavior-socd";
            #binding-cells = <1>;
            key0 = <A>;
            key1 = <D>;
            resolution = <SOCD_LAST>;
        };

        socd_ws: socd_ws {
            compatible = "zmk,behavior-socd";
            #binding-cells = <1>;
            key0 = <W>;
            key1 = <S>;
            resolution = <SOCD_NEUTRAL>;
        };
    };

    keymap {
        compatible = "zmk,keymap";
        default_layer {
            bindings = <
                ...  &socd_ws 0  ...
                &socd_ad 0  &socd_ws 1  &socd_ad 1
                ...
            >;
        };
    };
};
```

## Resolution Strategies

| Strategy | Value | Behavior |
|---|---|---|
| `SOCD_LAST` | `0` | **Recommended / "Snap Tap"**. Last key pressed wins. When released, the held opposing key reactivates. Tapping D while holding A sends `A→D→A`. |
| `SOCD_NEUTRAL` | `1` | Both keys cancel — neither is sent while both are held. When one is released, the remaining key activates. |
| `SOCD_KEY0_WINS` | `2` | The first key in the pair always takes priority. |
| `SOCD_KEY1_WINS` | `3` | The second key in the pair always takes priority. |

## How it works

Each SOCD behavior instance defines a pair of opposing keys. The behavior uses a **one-parameter binding** where parameter `0` activates key0 and parameter `1` activates key1.

On each press or release, the behavior updates held state and computes the desired output: key0, key1, or neither. It emits keycode events only when that output changes, releasing the previous winner before pressing the new one. A still-held suppressed key reactivates when the policy permits it.

Outputs use the same encoded-keycode event API as ZMK's `&kp`, with the triggering event's timestamp. This lets normal ZMK processing (including Caps Word, sticky keys, and Key Repeat) see the resolved output. Suppressed physical inputs do not emit keycodes. `key0` and `key1` accept encoded ZMK keycodes, including modifier-wrapped and consumer-page values; ordinary upstream modifier limitations still apply.

On split keyboards, resolution runs on the central, so a pair can span both halves. Last-input priority follows callback processing order; equal-time inputs have no special tie-breaking rule.

### Supported usage and limitations

- Use only selectors `0` and `1` and the four documented resolution values. Invalid selectors/configurations are rejected; invalid resolutions also fail devicetree validation.
- Each side must have **one concurrently active input source**. Duplicate press/release callbacks are ignored, but this is not reference counting: two physical positions must not hold the same instance/side concurrently.
- The two outputs must have distinct HID usages, even if their encoded modifiers differ. Invalid pairs are rejected at device initialization.
- Avoid simultaneously active output usages shared with other SOCD pairs, ordinary `&kp` bindings, or macros. ZMK's global HID state is not source-owned, so another producer can release an output this pair still considers active. This module filters only its own bindings, not all keys on the keyboard.
- Output state tracks logical events dispatched to ZMK listeners, **not host delivery**. Dispatch errors are logged and returned; a failed release stops the winner switch. Events are not automatically retried because listeners may already have processed them. There is no transport retry or host-state recovery guarantee.
- Release-before-press can produce an intermediate neutral HID report. The module intentionally does not batch reports or bypass normal event processing.

## Tests

Run the dependency-free host regression suite with a C11 compiler and Python 3:

```sh
python3 tests/run.py
```

For AddressSanitizer and UndefinedBehaviorSanitizer (when supported by the compiler):

```sh
python3 tests/run.py --sanitize
```

The harness compiles the actual behavior source against minimal ZMK/Zephyr stubs. It checks exhaustive balanced input sequences for all four policies, ordered output edges, duplicate callbacks, invalid inputs, independent instances, encoded keycode forwarding, timestamps, and dispatch errors. It does not emulate ZMK listeners, HID transport, or devicetree validation.

Before release, also build against your pinned ZMK revision for standalone, split-central, and split-peripheral targets, and verify Caps Word, lazy sticky keys, Key Repeat, cross-half pairs, and USB/BLE report ordering on real firmware.

## License

MIT — see [LICENSE](LICENSE).
