# 16x2 I2C LCD — console name + tagline display

**Status:** open
**Branch:** session/pico-2-wireless
**File anchor:** [src/configuration.h](../../src/configuration.h), new [src/display.h](../../src/display.h) + [src/display.cpp](../../src/display.cpp), [src/consoles.cpp](../../src/consoles.cpp)
**Related doc:** [pin-map-chart.md](../../pin-map-chart.md), [tag-lines.md](../../tag-lines.md), [todo/open/2026-09-24_console-config-lcd-fields.md](./2026-09-24_console-config-lcd-fields.md), [todo/open/2026-09-24_pin-map-consolidation.md](./2026-09-24_pin-map-consolidation.md), [todo/deferred/2026-09-12_i2c-oled-expansion.md](../deferred/2026-09-12_i2c-oled-expansion.md)

## What
Add an HD44780 16×2 LCD driven through a PCF8574 I2C backpack on
**I2C0 (GP4 SDA / GP5 SCL)** per the consolidated
[pin-map-chart.md](../../pin-map-chart.md). The display shows:

- **Boot welcome screen** — line 1 `RetroRoom`, line 2 `Sit and Play`,
  held for ~3 seconds, then transitions to the live display.
- **Live display** — line 1 = current console name; line 2 = the
  console's `tagline` (per-console field, populated from
  [tag-lines.md](../../tag-lines.md)).
- **Long-line scroll** — if either line exceeds 16 chars, scroll at
  1 char / 250 ms after a 1 s pause (only the overflowing line).
- **Backlight auto-off** — after `lcd.backlightOffAfterMs` ms of no
  `selectConsole()` activity (default 30000 = 30 s); restored on the
  next next/prev/click. Configurable in the JSON config on LittleFS.
- **No consoles loaded** — show `No consoles` / `Check /config`
  instead of the welcome.

Every code path that calls `selectConsole()` (USR button, HTTP
`/next`/`/prev`, WebSocket `next`/`prev`) lights the display for
free.

## Why
Today the only feedback for the operator about which console is
selected is:

1. The single WS2812 LED at `currentConsoleIndex` on the ring — easy
   to miscount when N gets large.
2. The `/state.json` HTTP endpoint — requires a laptop on the bench.

A 16×2 LCD gives a glanceable, headless-readable name + tagline + index
indicator. Cheap (~$5 for the LCD+backpack), trivial to wire (4 wires
— VCC/GND/SDA/SCL), and survives power-cycles the same way the
firmware's other state does.

## How

### 1. I2C bus + pin assignment
**I2C0 on GP4 (SDA) / GP5 (SCL)** per [pin-map-chart.md](../../pin-map-chart.md).
Both pins are currently free. 4.7 kΩ external pull-ups to 3.3V
required on both lines (I2C spec; the LCD backpack usually has its
own pull-ups but a perfboard revision needs the explicit pair).

Add to [src/configuration.h](../../src/configuration.h) inside the
Pico block:

```c
#define HAS_LCD
#define LCD_I2C_SDA_PIN   4
#define LCD_I2C_SCL_PIN   5
#define LCD_I2C_ADDR      0x27  // PCF8574 default; 0x3F on some clones
#define LCD_COLS          16
#define LCD_ROWS          2
#define LCD_WELCOME_MS    3000
#define LCD_BACKLIGHT_OFF_AFTER_MS_DEFAULT 30000  // default if lcd.backlightOffAfterMs missing
```

### 2. Library
Add to `[env:pico2w] lib_deps` in [platformio.ini](../../platformio.ini):

```
mhelm.col/LiquidCrystal_I2C@^1.1.0
```

(The `mhelm.col` fork works on Earle Philhower's RP2040/RP2350 core;
the original `fdebrabander` fork does not. Verify on bench before
committing the version pin.)

### 3. New module `src/display.{h,cpp}`
Exposes:

```c
void display_init();                                  // Wire.begin + LCD init + show welcome
void display_show_console(const char* name,
                          const char* tagline);       // live update, called from selectConsole()
void display_wake();                                  // re-enable backlight on next user action
void display_loop();                                  // non-blocking welcome->live transition
```

`display_init()` calls `Wire.begin(LCD_I2C_SDA_PIN, LCD_I2C_SCL_PIN)`
and prints `RetroRoom\nSit and Play` if `consoles.size() > 0` or
`No consoles\nCheck /config` otherwise.

`display_loop()` is called from `loop()`; it tracks `millis()` and
transitions from welcome → live after `LCD_WELCOME_MS` elapses, runs
the per-line scroll logic, and turns the backlight off after
`backlightOffAfterMs` of inactivity.

`display_wake()` resets the backlight-off timer; called from
`selectConsole()` so the next/prev/click handlers all wake the
display for free.

### 4. Hook into `selectConsole()`
In [src/consoles.cpp](../../src/consoles.cpp)'s `selectConsole(const
Console& c)` (around line 135 — see `currentConsoleSelectedAtMs`
neighbour), add:

```c
#if defined(HAS_LCD)
display_show_console(c.name.c_str(), c.tagline.c_str());
display_wake();
#endif
```

`c.tagline` is a new optional `std::string` on the `Console` struct
in `lib/ConsoleConfig` — see
[todo/open/2026-09-24_console-config-lcd-fields.md](./2026-09-24_console-config-lcd-fields.md)
for the schema change. Default empty string if the field is absent.

### 5. `setup()` wiring
In [src/main.cpp](../../src/main.cpp)`setup()`, after
`consoleDefinitions()` and before `selectStack_init()`:

```c
#if defined(HAS_LCD)
display_init();
#endif
```

`display_loop()` is added to `loop()` next to `flashLedTick()` /
`heartbeatTick()`.

### 6. Stub mode for envs without LCD
When `HAS_LCD` is not defined, `src/display.{h,cpp}` compiles out to
empty inline functions (`display_init()`,
`display_show_console(...)`, `display_wake()`, `display_loop()` all
become `((void)0)`) so `selectConsole()`'s `#if defined` guard is the
only change in [src/consoles.cpp](../../src/consoles.cpp).

This matters because the `[env:test_native]` Unity env doesn't link
`Wire` or the LCD library — stubbing the whole module avoids dragging
PCF8574 into host tests.

## Open questions
- **Welcome timeout.** 3 s for `RetroRoom` / `Sit and Play`. Bump to
  5 s if the operator wants more time to read it.
- **Scroll cadence.** 1 char / 250 ms after a 1 s pause. If the
  tagline is very long (the `Play in ours.` line from `tag-lines.md`
  is 19 chars; `UR NOT e` is 8; `Born to play.` is 14 — most fit),
  scrolling kicks in only on the longer entries. Pause resets on
  each `selectConsole()` so a freshly-selected console starts with
  the tagline visible.
- **Backlight wake on what.** `selectConsole()` only — what about the
  HTTP `/state.json` polling from the web UI? That can spam the wake
  timer. Consider waking only on user-driven actions (next/prev/click
  / WS message), not on read-only state fetches.
- **OLED coexistence.** The deferred SSD1306 OLED item
  ([todo/deferred/2026-09-12_i2c-oled-expansion.md](../deferred/2026-09-12_i2c-oled-expansion.md))
  targets the same I2C0 pins (GP4/GP5). If both ever ship, the LCD
  takes I2C0 + the OLED moves to I2C1 (GP2/GP3). Defer resolution
  until/unless the OLED item moves out of `deferred/`.

## Dependencies
- `[env:pico2w] lib_deps` in [platformio.ini](../../platformio.ini) —
  add the LCD lib above.
- `Wire` is part of Earle Philhower's arduino-pico core; no extra
  dep.
- `Console::tagline` (new field) — see
  [todo/open/2026-09-24_console-config-lcd-fields.md](./2026-09-24_console-config-lcd-fields.md).
- `lcd.backlightOffAfterMs` (new top-level field) — same schema
  todo.

## Related
- [pin-map-chart.md](../../pin-map-chart.md) — GP4/GP5 = I2C0 SDA/SCL.
- [tag-lines.md](../../tag-lines.md) — per-console taglines.
- [todo/open/2026-09-24_console-config-lcd-fields.md](./2026-09-24_console-config-lcd-fields.md) — schema change this depends on.
- [todo/open/2026-09-24_pin-map-consolidation.md](./2026-09-24_pin-map-consolidation.md) — establishes `pin-map-chart.md` as the source of truth.
- [todo/deferred/2026-09-12_i2c-oled-expansion.md](../deferred/2026-09-12_i2c-oled-expansion.md) — alternative display tech on the same pins.
- [src/consoles.cpp](../../src/consoles.cpp) `selectConsole()` — the update hook.
- [src/main.cpp](../../src/main.cpp) `setup()` + `loop()` — wiring + display_loop call site.
