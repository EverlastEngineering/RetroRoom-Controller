#ifndef LEDSTRING_H
#define LEDSTRING_H

// Second FastLED strip driver on GP21 (SELECTED_CONSOLE_LED_STRING_DATA).
// Independent of the 8-pixel ring under the rotary knob on GP20
// (NUM_LEDS / RR_FASTLED_DATA_PIN) -- the ring stays driven by
// src/lighting.{h,cpp} and is **untouched** by this module.
//
// The string is a 64-pixel WS2812B strip sized to fit the largest
// example config's [ledPosition, ledPosition+ledWidth) range (MAME in
// example2.json: 27+15=42, rounded up to NUM_SELECTED_CONSOLE_LED_STRING_LEDS=64
// in src/configuration.h).
//
// This module is **only built** when HAS_LEDS is defined (matches the
// guard in src/lighting.h). On boards without the second strip
// (e.g. the legacy wired-only envs) the whole TU compiles out.
//
// Macro dance (mirrors src/lighting.h's pattern) -- configuration.h MUST
// be included BEFORE <FastLED.h> so SELECTED_CONSOLE_LED_STRING_DATA is
// a numeric pin in this TU. FastLED's RP2040 PIO backend uses DATA_PIN
// as a template parameter NAME, which would collide with our #define
// of the same name from configuration.h; we save it under
// RR_FASTLED_STRING_DATA_PIN before #undef'ing DATA_PIN for the FastLED
// include, then restore. The bare DATA_PIN macro is also restored at the
// end so other code in the TU keeps resolving to the ring pin number.

#if defined(HAS_LEDS)
#include "configuration.h"
#pragma push_macro("DATA_PIN")
#define RR_FASTLED_STRING_DATA_PIN SELECTED_CONSOLE_LED_STRING_DATA
#undef DATA_PIN
#define FASTLED_INTERNAL  // silence FastLED's pragma messages
#include <FastLED.h>
#pragma pop_macro("DATA_PIN")
#endif

#if defined(HAS_LEDS)

// Wire the GP21 strip's addLeds call to CRGB selectedLeds[] and push
// black on boot. Mirrors src/lighting.cpp::lighting_init()'s shape but
// on a different pin + buffer. Called from src/main.cpp setup() after
// lighting_init().
extern void ledstring_init();

// Paint the window [ledPosition, ledPosition+ledWidth) of the current
// console (currentConsoleIndex -- index into src/consoles.cpp::consoles)
// with a dim warm white, all other pixels black, push FastLED.show().
//
// Safe to call when HAS_LEDS is defined but the device has no second
// strip wired -- the FastLED controller buffers a frame that won't go
// anywhere; pixels still zero out the ring driver (separate buffer).
//
// Reads the active console via CurrentConsole() in src/consoles.cpp, so
// it must be called after consoleDefinitions() has populated the
// vector. Callers in this repo are limited to:
//   - ledstring_init() at boot (once the first console is loaded)
//   - selectConsole() on every commit (rotary press, NEXT/PREV buttons,
//     /next, /prev, WebSocket console)
extern void ledstring_setConsole(int idx);

// Blank every pixel on the GP21 strip and push the frame to the wire.
// Used on the way down to a reset (the "Rebooting" state armed by a
// POST /consoles.json) so the operator sees the highlight window go
// dark rather than the last-painted console's block freezing on the
// strip through the restart.
//
// Independent of the ring: this does NOT touch CRGB leds[] on GP20 or
// MANUAL_OE_PIN. Callers that want the whole cabinet dark should also
// drive the ring's OE -- this is only the second strip.
extern void ledstring_allOff();

// Fill the entire GP21 strip with the given CRGB color. Used by the host
// tests to verify the buffer fill primitive in isolation; also useful
// for a future "manual clear" endpoint, but no production caller exists
// yet (kept because it's a one-line wrap of fill_solid that keeps the
// host tests pure -- no FastLED FastLED.show() call required on host).
extern void ledstring_fillRange(int fromInclusive, int toExclusive,
                                CRGB color);

#endif  // HAS_LEDS

#endif  // LEDSTRING_H
