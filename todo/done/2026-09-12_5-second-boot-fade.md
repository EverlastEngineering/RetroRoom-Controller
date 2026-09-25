# 5-second boot fade

**Status:** done (moved past — never implemented as designed)
**Completed:** 2026-09-23
**Branch:** session/pico-2-wireless
**File anchor:** [src/lighting.cpp](../../src/lighting.cpp) `lighting_init()`
**Related doc:** none yet

## What
Add a single-pixel "I'm alive" white flash at boot on the WS2812 ring
so the operator gets visual confirmation that the firmware actually
got past `setup()`.

## Why
Currently [src/lighting.cpp](../../src/lighting.cpp)`lighting_init()` clears
the ring (`CRGB::Black`) and waits for `lightSingle()` / `selectConsole()`
to paint something. On the YD-RP2040 the onboard WS2812 stays dark
until something drives it; on the perfboard the LED ring stays dark
too. Hard to tell from the bench whether the firmware is alive or hung.

## How
In `lighting_init()`, after `FastLED.setBrightness(LED_BRIGHTNESS)`
and the existing `fill_solid(..., CRGB::Black)` clear, add a brief
single-pixel white pulse at index 0 then re-clear:

```c
// "I'm alive" pulse -- lets the operator confirm boot without
// having to open a serial monitor. Skipped on YD-RP2040 since
// its onboard LED would just look like a glitch.
fill_solid(leds, NUM_LEDS, CRGB::White);
leds[0] = CRGB::Red;
FastLED.show();
delay(250);
fill_solid(leds, NUM_LEDS, CRGB::Black);
FastLED.show();
```

(Paint all-white underneath and index 0 red so you get a visible
"red dot on white" pulse that's unambiguous on any string.)

## Status note
- Skip this entire pulse on `[env:pico_yd]` because the YD onboard
  WS2812 has only 1 visible pixel and a 250ms bright pulse looks like
  a glitch. The above `#if defined(ARDUINO_YD_RP2040)` guard (or
  similar) lets the perfboard env see the pulse and the YD env skip.

## Resolution (2026-09-23)
**Closed without implementing the described pulse.** Two replacements
landed during the pico-2-wireless bring-up that satisfy the underlying
goal ("operator can confirm firmware is alive without opening a serial
monitor"):

1. **1 Hz on-board LED heartbeat** on `LED_BUILTIN` (commit `a3408be`,
   referenced in `2026-09-21_pico-2-w-platform.md`'s "Gotchas" section).
   On the Pico 2 W `LED_BUILTIN` is GP64; on the original Pico/Pico-W
   it's GP25. Either way it's a single-pixel pulse that's visible from
   the bench regardless of whether the external WS2812 ring is wired up.
2. **YD-RP2040 `lightCycle()` smoke test** in
   [src/lighting.cpp](../../src/lighting.cpp) (`lightCycleTick()`,
   toggled by USR button). Visible animation on the onboard WS2812
   for fast board bring-up, gated on `ARDUINO_YD_RP2040`.

Both serve the original intent (visible boot liveness) without
needing the single-pixel ring pulse this file described. The
single-pixel pulse was never merged.
