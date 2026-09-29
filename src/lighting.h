#ifndef LIGHTING_H
#define LIGHTING_H

// configuration.h MUST be included BEFORE <FastLED.h> so DATA_PIN is
// defined to a numeric pin in this TU.
//
// FastLED 3.10+ on RP2040 still uses DATA_PIN as a template parameter NAME
// in its ClocklessController class, which collides with our #define DATA_PIN
// 4. To avoid that, save DATA_PIN under RR_FASTLED_DATA_PIN before
// #undef'ing DATA_PIN for the FastLED include, then restore. We do the
// same for RGB_ORDER (defined as an enum value, not a macro, by FastLED's
// chipsets header -- but the template signature uses the name as a
// template parameter name, so a #define of the same name from anywhere
// would also break).
//
// lighting.cpp::lighting_init() uses RR_FASTLED_DATA_PIN in its addLeds
// call; the bare DATA_PIN macro is also restored so other code in the TU
// (e.g. controls.cpp's reference to DATA_PIN if it ever needs it) still
// resolves to the pin number.

#if defined(HAS_LEDS)
#include "configuration.h"
#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>
#endif

#if defined(HAS_LEDS)
extern void lighting_init();
// Advances the lightRing(false) fade toward completion. lightRing()
// only steps the fade one increment per call, so the caller that asked
// for the ring to go dark has to keep asking; this does it from loop().
extern void lighting_loop();
extern void lightSingle(int led);
extern void ringLEDNext();
extern void ringLEDPrevious();
// lightRing(true) snaps the ring on. lightRing(false) starts a fade to
// black that finishes only once lighting_loop() has pumped it enough.
extern void lightRing(bool lit);
// Continuous smoke-test cycle (red -> green -> blue). Used by loop() on
// the pico_yd env to exercise the FastLED PIO driver after lighting_init().
// Returns immediately if the cycle period hasn't elapsed.
extern void lightCycleTick();
// Query / toggle the cycle. Not currently bound to any control.
extern bool lightCycleIsEnabled();
extern void lightCycleToggle();
#endif

#endif