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
// a numeric pin in this TU. 

#if defined(HAS_LEDS)
#include "configuration.h"
#define FASTLED_INTERNAL  // silence FastLED's pragma messages
#include <FastLED.h>
#endif

#if defined(HAS_LEDS)

// Wire the GP21 strip's addLeds call to CRGB selectedLeds[] and push
// black on boot. Mirrors src/lighting.cpp::lighting_init()'s shape but
// on a different pin + buffer. Called from src/main.cpp setup() after
// lighting_init().
extern void ledstring_init();

// Paint the resting state for `idx`: every pixel above the console's
// window dimly lit, the console's own window at full, everything below
// dark. Called from ledstring_init() at boot and whenever the strip
// leaves an animation.
//
// Safe to call when HAS_LEDS is defined but the device has no second
// strip wired -- the FastLED controller buffers a frame that won't go
// anywhere; pixels still zero out the ring driver (separate buffer).
//
// Reads the active console via CurrentConsole() in src/consoles.cpp, so
// it must be called after consoleDefinitions() has populated the
// vector. Callers in this repo are limited to:
//   - ledstring_init() at boot (once the first console is loaded)
//   - controls_browseReset() when an abandoned browse is abandoned
//     back to the selected console
extern void ledstring_setConsole(int idx);

// Pump the in-flight animation, if there is one. Call from loop().
//
// Returns immediately when the strip is resting, so the common case
// costs a single comparison and no PIO traffic. While an animation is
// running it repaints at LEDSTRING_FRAME_INTERVAL_MS and, for the
// selection effect, ends itself back in the resting paint.
//
// No-op when HAS_LEDS is undefined.
extern void ledstring_loop();

// ---- browse ------------------------------------------------------------

// The operator turned a detent but has not yet committed to a console:
// draw the blob part-way between `fromIdx` and `toIdx`.
//
// `fractionPermille` is the browse position from
// retroroom_core::DetentGate::onDetent(): 0 puts the blob on `fromIdx`,
// 1000 lands it centred on `toIdx`, and the value is continuous so
// turning the knob back walks the blob back the way it came. It is
// deliberately *not* constrained to [0, 1000] -- the shell clamps.
extern void ledstring_browseProgress(int fromIdx, int toIdx,
                                     int fractionPermille);

// The browse snapped onto `idx`: stop the blob and pulse the console's
// JSON-defined window, showing which console a click would select.
// Replaces the static paint that used to be the only browse feedback
// (which was a serial line and nothing else).
extern void ledstring_browseSnap(int idx);

// The browse was abandoned (the ring gave up, or a commit happened):
// drop back to the resting paint for the selected console.
extern void ledstring_browseClear();

// ---- selection ---------------------------------------------------------

// Play the selection effect for `idx`: the whole strip twinkles, then
// collapses in under LEDSTRING_SELECT_EFFECT_MS to just the pixels
// above the selected console. Ends back in the resting paint.
//
// Called from selectConsole(), so it covers every commit path: the
// rotary click, NEXT/PREV buttons, /next, /prev, and the WebSocket
// console message. It cancels any browse animation already in flight.
extern void ledstring_selectEffect(int idx);

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
