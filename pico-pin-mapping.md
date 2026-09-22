# Pin mapping — RetroRoom-Controller

The source of truth for which physical pins on the **Raspberry Pi Pico**
connect to which RetroRoom peripherals. The authoritative source code is
[`src/configuration.h`](src/configuration.h) — this file translates those
`#define`s into wiring instructions and a KiCad-friendly net list.

> **Status:** ESP8266 (NodeMCU v2) support was dropped on
> `session/merge-pico-json`. The Pico branch is the only target. All pin
> choices below reflect the **current** `src/configuration.h`. They are
> initial, conservative defaults; they'll move once the perfboard
> revision lands.

---

## 1. Per-role name table

Every `#define` in `src/configuration.h` is one **role**. The role is
abstract; the physical pin is per-board. The complete role vocabulary
as the codebase uses it today:

| Role (`#define`)            | What it is                                          | Used in                                   |
| --------------------------- | --------------------------------------------------- | ----------------------------------------- |
| `NUM_LEDS`                  | FastLED ring length (8)                              | `src/lighting.cpp`                        |
| `ARM_PIN`                   | StackSelector ARM pin (high-active)                 | `src/stackselector.cpp`                   |
| `CYCLE_PIN`                 | StackSelector CYCLE pin                              | `src/stackselector.cpp`                   |
| `ENABLE_PIN`                | StackSelector ENABLE pin                             | `src/stackselector.cpp`                   |
| `ROTARY_PIN_IN1`            | Rotary encoder A                                     | `src/controls.cpp` (ISR `checkPosition()`) |
| `ROTARY_PIN_IN2`            | Rotary encoder B                                     | `src/controls.cpp` (ISR `checkPosition()`) |
| `DATA_PIN`                  | FastLED ring data in (NeoPixel DIN)                   | `src/lighting.cpp` (FastLED.addLeds)       |
| `TOUCH_SENSOR_PIN`          | Capacitive touch sensor output / button input         | `src/controls.cpp` (`EasyButton touchSensor`) |
| `ROTARY_SELECTOR_PIN`       | Rotary encoder push-button (the click that selects)  | `src/controls.cpp` (rotarySelector)        |
| `IR_CONTROL_PIN`            | IR LED data (sender). Z3t0/IRremote 4.x driver, PIO-driven on Pico. | `src/ircontrol.cpp::ir_control_init()` |
| `MANUAL_OE_PIN`             | Manual output-enable (drives a MOSFET / LED OE pin). GP12. | `src/state.cpp`, `src/main.cpp` (gated on `#ifdef MANUAL_OE_PIN`) |

---

## 1a. On-board LED blink convention

`LED_BUILTIN` (GP25 on Pico / Pico-W / YD-RP2040, GP64 on Pico 2 W) is
wired active-high — driving `HIGH` lights it, `LOW` turns it off. The
firmware drives it via `state.cpp::setLed()` (solid on / off) and
`state.cpp::flashLedTick()` (per-loop blink, called from
`main.cpp::loop()`). `flashLedTick()` picks its cadence from the
network state, giving the operator a clear visual status from across
the room:

| Cadence                                    | Meaning                                       | How to recover                         |
| ------------------------------------------ | --------------------------------------------- | -------------------------------------- |
| **Fast flash** (1 Hz, 500 ms on/off)       | **Needs wifi config.** SoftAP / captive-portal mode or no wifi configured. | Connect to `RetroRoom-Setup`, fill in `/setup`. |
| **Slow blink** (~3 s period, 250 ms pulse, 2750 ms gap) | **Online and happy.** STA mode with an IP, server running, waiting for UI commands. | (no action — this is the healthy state) |
| **Solid ON**                               | Manual `ledOn()` (e.g. `/ledOn`, WS `ledOn`).  | `/ledOff` to return to idle.           |
| **Solid OFF**                              | Manual `ledOff()` (e.g. `/ledOff`, WS `ledOff`). | `/ledOn` to return to idle.           |

The `flash` flag is the source of truth for the blink driver:
`flash=true` enables the cadence (fast or slow depending on
`network_inStaMode()`); `flash=false` idles the LED on whatever
`setLed()` last wrote. `ledOn()` / `ledOff()` and the `/flash`
handler all clear `flash` when the operator takes manual control, so
the LED immediately stops blinking and pins to the solid state.

The fast-vs-slow convention lets the operator tell at a glance
whether the device needs wifi config without opening the serial
monitor: a fast flash means "go to `/setup`," a slow blink means
"everything is fine."

---

## 2. Raspberry Pi Pico — perfboard target

This is the wiring the codebase currently expects on a Pico running off
the standard 40-pin header. Pin choices avoid strapping pins (GP0/GP1
share UART0 with the on-board USB-CDC; GP25 is the on-board LED); all
peripherals are on GP2-GP10 to leave I2C, SPI, and the second UART
free for future expansion.

| Role (`#define`)            | Pico GP | Notes                                       |
| --------------------------- | ------- | ------------------------------------------- |
| `ARM_PIN`                   | GP8     | StackSelector ARM (high-active).            |
| `ROTARY_PIN_IN1`            | GP2     | Rotary encoder A.                          |
| `ROTARY_PIN_IN2`            | GP3     | Rotary encoder B.                          |
| `DATA_PIN`                  | GP4     | FastLED ring DIN. **Add 470 Ω series resistor close to the first LED.** |
| `TOUCH_SENSOR_PIN`          | GP5     | Capacitive touch / YD-RP2040 USR button. RP2040 needs an **external 10 kΩ pull-up to 3.3V** (RP2040 internal pull-up is weak). |
| `ROTARY_SELECTOR_PIN`       | GP6     | Rotary encoder push-button.                 |
| `IR_CONTROL_PIN`            | GP7     | IR sender data (z3t0/IRremote 4.x PIO-driven). 38 kHz carrier, 940 nm IR LED + series resistor (~33 Ω for 5 V). |
| `CYCLE_PIN`                 | GP9     | StackSelector CYCLE.                        |
| `ENABLE_PIN`                | GP10    | StackSelector ENABLE.                       |
| `MANUAL_OE_PIN`             | GP12    | Manual output-enable MOSFET gate (idle high). |

### Pico perfboard pin map (silkscreen labels)

The Pico's silkscreen labels each physical pin with its GP number, so the
table above is the same as the silkscreen map. Two exceptions:

| GP  | Silkscreen | Why                                                 |
| --- | ---------- | ---------------------------------------------------- |
| 25  | LED        | On-board LED. Driven by `LED_BUILTIN`; reserved.    |
| 30  | (RUN)      | Reset pin — never wire to anything.                 |

### Pico KiCad-friendly net list

```
NET "ROTARY_A"     -> GP2
NET "ROTARY_B"     -> GP3
NET "LED_DATA"     -> GP4   (FastLED ring, add 470 Ω series resistor)
NET "TOUCH_OUT"    -> GP5
NET "ROTARY_BTN"   -> GP6
NET "IR_LED_DATA"  -> GP7
NET "STK_ARM"      -> GP8
NET "STK_CYCLE"    -> GP9
NET "STK_ENABLE"   -> GP10
NET "MANUAL_OE"    -> GP12
```

### Pico wiring notes (read before breadboarding)

1. **GP4 (DATA_PIN / FastLED) needs a 470 Ω series resistor** close to
   the first LED. NeoPixels' internal pull-up provides the idle-high,
   so no external pull-up is required.
2. **GP5 (TOUCH_SENSOR_PIN) needs an external 10 kΩ pull-up to 3.3V.**
   The RP2040's internal pull-up is too weak for reliable capacitive
   se`MANUAL_OE_PIN` is GP12 on the perfboard** (set in `src/configuration.h`).
   Drives an external output-enable MOSFET (idle-high) that blanks the
   LED ring for power-saving / standby. `src/state.cpp` writes
   `digitalWrite(MANUAL_OE_PIN, !state)` and `analogWrite(MANUAL_OE_PIN, 127)`
   inside `#ifdef MANUAL_OE_PIN` guards. `src/main.cpp` calls
   `pinMode(MANUAL_OE_PIN, OUTPUT)` and `analogWriteFreq(40000)` during
   setup. To temporarily disable, comment out the `#define` and the
   `#ifdef`-guarded blocks become no-ops in `src/configuration.h`. Suggested pin: GP12 (free,
   away from UART).
4. **GP0 / GP1 are UART0 TX/RX** for the on-board USB-CDC bridge. Don't
   drive them as GPIO unless you can afford to lose the serial monitor.
5. **GP25 is the on-board LED.** Reserved for `LED_BUILTIN`-driven
   status output. Safe to reuse once the firmware is past `setup()`.

---

## 3. YD-RP2040 dev board (current bench box)

VCC-GND Studio YD-RP2040 — pinout-compatible with the standard Pico,
plus a few extras:

- **Onboard WS2812 RGB LED** on **`GPIO23`** (the silkscreen labels
  this pad as "RGB" — be aware that some early boards label the pads
  as "R58" due to a font-rendering quirk; if the WS2812 is dead, the
  RGB pads may need a solder bridge; see
  https://gist.github.com/probonopd/2a1e8ce64142fdefe20a89d0d540ab0d)
- **USR button** on **`GPIO24`** (active-low, internal pull-up enabled
  via `EasyButton(TOUCH_SENSOR_PIN, 35, true, false)`)
- **Blue LED** on **`GPIO25`** (same pin as Pico's green LED)
- **PWR LED** tied to 3.3V rail (always-on)

YD-RP2040 pin map (selected via `-D ARDUINO_YD_RP2040` in
`[env:pico_yd]`):

| Role (`#define`)            | YD-RP2040 GP | Notes                                       |
| --------------------------- | ------------ | ------------------------------------------- |
| `DATA_PIN`                  | **GP23**     | YD-RP2040 onboard WS2812 (`PIN_NEOPIXEL`). Note: differs from the perfboard Pico block above (GP4). |
| `TOUCH_SENSOR_PIN`          | **GP24**     | YD-RP2040 USR button (`PIN_USRKEY`). On the perfboard this would be the capacitive touch input. |
| `ROTARY_PIN_IN1`            | GP2          | Same as perfboard.                         |
| `ROTARY_PIN_IN2`            | GP3          | Same.                                       |
| `ROTARY_SELECTOR_PIN`       | GP6          | Same.                                       |
| `IR_CONTROL_PIN`            | GP7          | Same; currently unused.                     |
| `ARM_PIN`                   | GP8          | Same.                                       |
| `CYCLE_PIN`                 | GP9          | Same.                                       |
| `ENABLE_PIN`                | GP10         | Same.                                       |

> **Smoke-test hookup:** the YD-RP2040's onboard WS2812 on GP23 is
> the target the smoke-test WS2812 cycle (when enabled) and the
> FastLED PIO diagnostic hits. With the WS2812 + USR button wired
> to known pins and a pull-up on GP5 if you add the touch frontend,
> the perfboard schematic becomes a straightforward drop-in for the
> YD's pinout.

---

## 4. Open questions (waiting on perfboard revision)

These need answers before the next perfboard cut, in priority order:

1. **IR blaster re-add.** `src/ircontrol.{cpp,h}` was deleted on
   `session/merge-pico-json` (commit `c6005c7`). The ESP-coupled
   `crankyoldgit/IRremoteESP8266@^2.8.5` is gone from
   `platformio.ini`. Re-add using `z3t0/IRremote@^4.7.1` (already in
   `lib_deps` on all Pico envs) — the v4 API uses
   `IrSender.sendPulseDistanceWidthRaw()` for raw SIRC codes. Until
   then, the IR layer is gone and `selectConsole()` only logs the
   `tvInput` hex instead of blasting it. (See [TODO.md](TODO.md).)
2. **`MANUAL_OE_PIN` on the Pico.** Where does the manual-LED-OE
   circuit actually wis GP12 on the perfboard** (now wired; see
   Section 2 above). When the perfboard revision lands, verify the
   MOSFET circuit can idle-high on GP12 without pulling too much
   current on boot (RP2040 GPIO default state is high-impedance; the
   `pinMode(MANUAL_OE_PIN, OUTPUT)` call in setup() explicitly drives it)he role names imply a
   single module. If the design moves to multiple modules,
   `selectStack()` in `src/stackselector.cpp` needs to know the chain
   length. Track as `STACK_MODULES` config (TODO) rather than
   hard-coding to `consoles.size()`.
4. **I2C for an OLED / encoder expansion.** GP2/GP3 (I2C1 SDA/SCL)
   and GP4/GP5 (I2C0 SDA/SCL) are free. An SSD1306 OLED would
   integrate cleanly with `lighting.h`.
5. **External 10 kΩ pull-up on `TOUCH_SENSOR_PIN` (GP5).** Confirmed
   above. The perfboard schematic must include this.
6. **Pico-W WiFi (CYW43).** `[env:picow]` is a placeholder. When
   implementation lands, add `cyw43-driver` (or similar) to
   `lib_deps` and `-D HAS_WIFI` to `[env:picow] build_flags`. The
   intended anchor is `src/network.{h,cpp}`. See [TODO.md](TODO.md)
   for the full plan.

---

## Source-of-truth anchors

- [`src/configuration.h`](src/configuration.h) — every `#define` resolves
  to a GPIO number or stays undefined.
- [`src/lighting.{h,cpp}`](src/lighting.cpp) — `DATA_PIN` consumer (FastLED ring).
- [`src/controls.{h,cpp}`](src/controls.cpp) — `ROTARY_PIN_IN1/IN2/SELECTOR_PIN` + `TOUCH_SENSOR_PIN` consumer (rotary ISR + `EasyButton touchSensor`).
- [`src/stackselector.{h,cpp}`](src/stackselector.cpp) — `ARM_PIN` / `CYCLE_PIN` / `ENABLE_PIN` consumer.
- [`src/state.{h,cpp}`](src/state.cpp) — `MANUAL_OE_PIN` consumer (gated, currently inactive).
- [TODO.md](TODO.md) — open work on this branch.
- [LOG.md](LOG.md) — recent decisions.
