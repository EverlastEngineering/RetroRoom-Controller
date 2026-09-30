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
// Advance the ring one tick. Returns true on the single tick where a
// fade completed, which is the cue for the browsed cursor to revert to
// the selected console.
extern bool lighting_loop();
// Report the proximity pad's current reading. A hand at the knob holds
// the ring lit and suppresses the idle timeout; the hand leaving starts a
// fade immediately rather than waiting the timeout out.
//
// The argument is the pad's *state*, not an edge: lib/RingPaint detects
// the edges itself, so there is one answer to "did the hand just arrive"
// rather than two that can disagree.
extern void lightRingSetProximityHold(bool held);
// A commit: one strike of the whole ring, then the force-off. This is
// the call every selection path should make -- selectConsole() is the
// single commit point, so putting the strike here covers the rotary
// press, NEXT/PREV, /next, /prev and the post-boot restore at once.
// led.ringFlashMs = 0 makes it the plain force-off with no flash.
//
// A commit also ends the interaction even if a hand is still resting on
// the pad, and it stays ended until the hand lifts and returns.
extern void lightRingSelectStrike();
// A rotary detent. Moves the ring's free-running spinner pixel, which is
// not a console index and never has been.
extern void ringLEDNext();
extern void ringLEDPrevious();
#endif

#endif