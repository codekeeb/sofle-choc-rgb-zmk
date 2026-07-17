# Sofle Choc Wireless RGB — ZMK firmware · CODE/KEEB

ZMK firmware for the CODE/KEEB **Sofle Choc wireless RGB**: nice!nano v2,
vertical OLED displays, 30 RGB LEDs per half (29 per-key + 1 on the
encoder), 2 rotary encoders and ZMK Studio.

It bundles a **custom RGB effect engine** (vendored and heavily extended
from [zmk-rgb-fx], MIT) and a **fork of the display module**
([codekeeb/zmk-nice-oled], `selectable` branch) with features that don't
exist in the originals.

---

## ⌨️ Controls

### Encoders

| Gesture                    | Base layer          | LOWER            | RAISE       |
| -------------------------- | ------------------- | ---------------- | ----------- |
| **Turn left encoder**      | Volume              | RGB mode ±       | Brightness ±|
| **Turn right encoder**     | Fast page scroll    | Hue ±20°         | Speed ±     |
| **Press left encoder**     | Mute                | RGB on/off       | —           |
| **Press right encoder**    | LED power cut (`EP_TOG`) | Next OLED animation | —      |

### Keys (LOWER layer, left half)

| Key   | Function      |
| ----- | ------------- |
| R     | RGB on/off    |
| T     | Next RGB mode |
| F / G | Hue − / +     |
| V / B | Brightness − /|

### Other

- **Caps Word**: RAISE + G (where CapsLock used to be) — types UPPERCASE
  until the next space.
- **ZMK Studio**: the left half ships Studio; connect over USB, open
  [zmk.studio](https://zmk.studio) and unlock with LOWER + Z
  (`studio_unlock`) to **remap live without flashing**.

RGB settings (mode, hue, brightness, speed) and the selected OLED
animation **persist in flash**: they survive power cycles and reflashes
(only `settings_reset` clears them). The on/off switch does **not**
persist: the RGB always starts **off** at boot, regardless of how it was
left before power-off.

---

## 🌈 RGB system

Coordinate-based animation engine: every LED has an (x,y) position on a
**continuous canvas from 0 to 240 spanning both halves** (left 0-117,
right 123-240), so horizontal gradients cross the seam without a jump.
The maps live in `config/sofle_left.overlay` and
`config/sofle_right.overlay`.

### Modes (cycle with LOWER + left encoder)

| #  | Mode         | Description                                                 |
| -- | ------------ | ----------------------------------------------------------- |
| 1  | Gradient     | 3-color gradient animated diagonally                         |
| 2  | Ripple       | Blue waves from each pressed key (per half)                  |
| 3  | Sparkle      | Cyan/magenta sparkles                                        |
| 4  | Solid        | Uniform color cycling amber↔pink                             |
| 5  | Fire         | Vertical red-orange-yellow gradient, fast                    |
| 6  | Ocean        | Blue-cyan-green horizontal, very slow                        |
| 7  | Gold sparkle | Fast golden sparkles                                         |
| 8  | Pink ripple  | Faster, wider pink waves                                     |
| 9  | Sunset       | **Static**: orange-coral-pink-purple-blue (S100, tight arc)  |
| 10 | Heatmap      | Each key lights up when pressed and fades out (~1.2 s)       |

### Global controls (affect every mode)

- **Hue**: a 0-359° offset applied in the HSL→RGB conversion — rotates the
  full palette of any mode without destroying it. 20° steps.
- **Brightness**: 5 steps (minimum 1: turning off is the toggle's job — a
  persisted brightness of 0 left the keyboard black forever).
- **Speed**: 5 steps (0.25×–4×) scaling the animation tick period —
  speeds up/slows down all effects equally.
- **Per-layer tint**: LOWER paints **all thumb LEDs pink**. RAISE paints
  the thumbs **purple** and shows the **Bluetooth panel** (BT_CLR red,
  profiles 0-4 yellow, **active profile green**) plus the **arrow cluster
  (I/J/K/L) in orange-yellow** on the right half. Fixed colors (they don't
  rotate with the hue offset); the state travels to the right half over
  the split behavior channel (`rgblay`).
- **Auto-off**: after 60 s idle (`CONFIG_ZMK_IDLE_TIMEOUT`) the effects
  stop and the strip goes black; any keypress revives it. Each half
  manages its own idle state.

### Editing palettes and speeds

Every mode is a node in both `sofle_*.overlay` files (edit BOTH!):

- `colors = <HSL(hue, saturation, lightness) ...>` — hue 0-359
  (0 red, 60 yellow, 120 green, 180 cyan, 240 blue, 300 magenta);
  S=100 and L=50 is the purest, most vivid color (L>50 washes to white).
- `duration` — seconds per cycle (gradient/solid/sparkle), ms of wave
  travel (ripple) or ms of fade-out (heatmap). Lower = faster.
- `gradient-width` — with N colors there are N segments (the last wraps
  to the first); to show the full arc without repeating on the keyboard
  (240 units): `width = 240 * N / (N-1)`.
- For warm gradients: red (330°-30°) is perceptually flat — inserting an
  intermediate stop (e.g. coral 355°) gives more visible hues on that
  stretch (that's how Sunset is built).

### Architecture and fixes over upstream zmk-rgb-fx

Code lives in `src/` + `dts/` + `include/` (the repo is also a Zephyr
module). On top of upstream we fixed/added: a typo that voided
`key-pixels`, the ripple distance table left unfilled, global `locality`
on the behavior, starting the incoming effect on mode change, sanitizing
the persisted state, and the hue/speed/per-layer-tint/auto-off/heatmap
features described above.

---

## 🖥️ OLED displays

Module: [codekeeb/zmk-nice-oled] `selectable` branch (fork of
[mctechnology17/zmk-nice-oled] with custom improvements).

- **Left (central)**: graphic battery, BT/USB output, layer, profile, and
  **Bongo Cat** banging along to your WPM.
- **Right (peripheral)**: graphic battery + **runtime-switchable animation**
  (LOWER + press right encoder, persists):
  1. Rotating gem/crystal · 2. Cat · 3. 3D head · 4. Spaceman ·
  5. Pokemon · 6. **CODE/KEEB logo** (static)
- **Graphic battery**: battery icon with proportional fill + bolt while
  charging (`NICE_OLED_WIDGET_BATTERY_GRAPHIC`), instead of the number.

Fork improvements over the upstream module: runtime animation selector
with persistence (behavior `&oledanim` + event + settings), graphic
battery, a missing default for `ANIMATION_PERIPHERAL_MS` (which broke the
build with Smart Battery), and the logo asset.

---

## 🔋 Battery

- **Real percentage**: custom driver (`src/battery_nrf_vddh_curve.c`) with
  an interpolated LiPo discharge curve (21 points) instead of ZMK's
  4.20→3.45 V straight line, which underreports charge for most of the
  battery's life. The % depends ONLY on voltage: the cell's mAh don't
  matter.
- With USB connected, VDDH sees the charger and reads ~100%: only reliable
  on battery.
- With RGB on, the voltage sags under load and the % drops a few points;
  it recovers when RGB is off. Physics, not a bug.
- `CONFIG_BOARD_ENABLE_DCDC_HV=y`: ZMK ≥ v0.3 disabled it by default (for
  inductor-less clones); on boards with an inductor it's worth enabling.
- The **initial RGB brightness is low (20%)** on purpose: 30 LEDs at full
  power draw ~500 mA and can collapse the rail with small cells.

---

## 🔧 Hardware: data pins per workshop PCB variant

> **Don't mix them up!** Each variant uses a different data pin.

| PCB                                 | LED data pin | LEDs per half |
| ----------------------------------- | ------------ | ------------- |
| Sofle RGB MX wireless               | P0.06        | 29            |
| Sofle Choc wired                    | P0.06        | 29            |
| **Sofle Choc wireless** (this repo) | **P0.08**    | **30** (5th = encoder LED) |

Chain order (this variant): inner column top-to-bottom (1-4), **encoder
LED (5th)**, thumbs, then serpentine out to the pinky.

> ⚠️ **The keymap's `&spi3`/pinctrl block is NOT redundant**: ZMK's `sofle`
> shield (≥ v0.3) ships its own `boards/nice_nano_v2.overlay` with MOSI on
> P0.06 and chain 36, applied AFTER the config overlay. The keymap is the
> last thing to enter the devicetree: only from there can the real pin
> (P0.08) be enforced. It was once deleted as "duplicate" and cost a full
> day of debugging (verified by reading the live `PSEL.MOSI` of SPIM3 over
> USB logging).

---

## 🏗️ Builds

Push to `main` → GitHub Actions publishes the `firmware` artifact with:
`sofle_left` (with ZMK Studio), `sofle_right`, `settings_reset`, and two
USB-logging debug variants (`sofle_left_usb_logging`,
`sofle_right_usb_logging`).

**Pinned versions** for reproducible builds (`config/west.yml`):

- **ZMK `v0.3`** (the `main` branch moved to Zephyr 4.1 in Dec 2025 and
  renamed the boards; builds break by themselves if not pinned). The
  workflow is also pinned to `@v0.3`.
- **Zephyr pinned by SHA**: the `v3.5.0+zmk-fixes` branch of ZMK's Zephyr
  fork is a moving target (it got a Nordic HAL bump in 2025); the project
  is overridden from this manifest (top-level wins over the import).
- **zmk-nice-oled**: our own fork, `selectable` branch.

### Flashing

Double-tap reset → USB drive → copy the `.uf2`. After firmware changes
with weird state: `settings_reset` on both halves and reflash (this also
clears BT pairings).

### Debugging

Flash the `*_usb_logging` variant, connect over USB and read the serial
port (VID 0x1D50, 115200). It prints the RGB pipeline (`fx tick` with the
values being sent), the `ext_power` state, and **the real SPIM3 registers**
(`PSEL.MOSI` = physical pin in use) — the tool that solved the RGB
blackout.

---

## 🚑 Known issues and lessons

| Symptom | Cause / fix |
| --- | --- |
| RGB/OLED keys only act on the left half | The split transport truncates the behavior name to **8 characters**. Behavior nodes must always be ≤8 (`rgbfx`, `rgblay`, `oledanim`). |
| Halves "connected" but the right one doesn't type | Half-formed bond (log: `Link is not encrypted`). Re-pair with **every other workshop ZMK keyboard powered off** (foreign centrals hijack peripherals). |
| Dead LEDs with SPI apparently OK | Data pin overridden by the shield overlay (see note above) or rail with no power. Verify with the logging build (SPIM3 registers). |
| Black RGB that doesn't respond to anything | Toxic persisted state (brightness 0 / off). It's now sanitized at boot; if needed, `settings_reset`. |
| Build broken after creating a new repo | ZMK not pinned. Copy the `west.yml` from here. |

---

## Credits

- RGB engine based on [zmk-rgb-fx] by Kuba Birecki (MIT) — heavily modified.
- Displays based on [mctechnology17/zmk-nice-oled] (MIT) — via our own fork.
- [ZMK Firmware](https://zmk.dev) `v0.3`.

[zmk-rgb-fx]: https://github.com/crystalplanet/zmk-rgb-fx
[codekeeb/zmk-nice-oled]: https://github.com/codekeeb/zmk-nice-oled/tree/selectable
[mctechnology17/zmk-nice-oled]: https://github.com/mctechnology17/zmk-nice-oled
