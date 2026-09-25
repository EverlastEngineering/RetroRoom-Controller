# I2C OLED / encoder expansion

**Status:** deferred (waiting on the perfboard)
**Branch:** session/merge-pico-json

## What
Add an SSD1306 OLED (or similar) on I2C0 (GP4/GP5) or I2C1 (GP2/GP3)
to show the current console name, IR codes loaded, and WiFi status
(once the CYW43 work lands).

## Why
Currently the only feedback channel is the WS2812 ring + serial log.
On a real device, an OLED would let the operator see what's going on
without needing a serial monitor. The I2C pins are free on the
perfboard header so the wiring is straightforward.

## How (when the perfboard lands)
1. Add `Wire` (I2C) init in `setup()` after `consoleDefinitions()`.
2. Add `Adafruit_SSD1306` to `lib_deps` for `[env:pico_base]` and
   `[env:picow]`.
3. New file [src/display.h](../../src/display.h) + [src/display.cpp](../../src/display.cpp) that exposes
   `display_show_console(const Console& c)` etc.
4. Call `display_show_console` from `selectConsole()`.
5. Add a `<HAS_OLED>` build flag and stub mode for envs that don't have
   one (YD, picotest).

## Related
- [todo/open/2026-09-12_cyw43-picow-wifi.md](../open/2026-09-12_cyw43-picow-wifi.md) -- once
  WiFi lands the OLED is the natural place to show IP + status.
- [pin-map-chart.md](../../pin-map-chart.md) -- I2C pins (GP2/GP3 I2C1, GP4/GP5 I2C0) reserved for this.
