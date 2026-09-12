# 5-second boot fade

**Status:** open
**Branch:** session/merge-pico-json
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
