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
      url-base: https://github.com/brokenax3/zmk-behavior-socd
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

When a key is pressed:
1. The behavior checks if the opposing key is currently held
2. If so, it applies the configured resolution strategy
3. It directly manages the HID keyboard report (register/unregister keycodes)

When a key is released:
1. The keycode is unregistered from the HID report
2. If the opposing key is still physically held, it gets reactivated (re-registered)

## License

MIT — see [LICENSE](LICENSE).
