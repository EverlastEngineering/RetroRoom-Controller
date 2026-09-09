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
#pragma push_macro("DATA_PIN")
#pragma push_macro("RGB_ORDER")
#define RR_FASTLED_DATA_PIN DATA_PIN
#define RR_FASTLED_RGB_ORDER RGB_ORDER
#undef DATA_PIN
#undef RGB_ORDER
#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>
#pragma pop_macro("RGB_ORDER")
#pragma pop_macro("DATA_PIN")
#endif

#if defined(HAS_LEDS)
extern void lighting_init();
extern void lightSingle(int led);
extern void ringLEDNext();
extern void ringLEDPrevious();
extern void lightRing(bool lit);
// Continuous smoke-test cycle (red -> white -> blue). Used by loop() on
// the pico_yd env to exercise the FastLED PIO driver after lighting_init().
// Returns immediately if the cycle period hasn't elapsed.
extern void lightCycleTick();
#endif

#endif