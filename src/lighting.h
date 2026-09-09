#ifndef LIGHTING_H
#define LIGHTING_H

// FastLED 3.7+ broke the addLeds template signature and dropped the
// NEOPIXEL/NEOPIXEL-class-template alias used by the legacy code. Until the
// FastLED API migration lands (next session), gate the include so the rest of
// the firmware builds on boards without the LED ring wired up.
#if defined(HAS_LEDS)
#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>
#endif

#include "configuration.h"

#if defined(HAS_LEDS)
extern void lighting_init();
extern void lightSingle(int led);
extern void ringLEDNext();
extern void ringLEDPrevious();
extern void lightRing(bool lit);
#endif

#endif