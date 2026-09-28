// Second FastLED strip driver on GP21 (SELECTED_CONSOLE_LED_STRING_DATA).
// See src/ledstring.h for the design rationale.
//
// This translation unit is excluded from the [env:test_native] host
// build -- it depends on FastLED's RP2040 PIO backend and the Arduino
// core, neither of which exist on the host. The paint *math* is lifted
// into lib/LedStringPaint precisely so it can be unit-tested on the
// host; see test/test_ledstring.

#include "ledstring.h"

#if defined(ARDUINO) && defined(HAS_LEDS)

#include <LedStringPaint.h>
#include <ConsoleConfig.h>  // for retroroom_core::Console (led_position, led_width)
#include "consoles.h"        // for CurrentConsole() — held in src/consoles.cpp

// Independent of the ring's CRGB leds[NUM_LEDS] in src/lighting.cpp.
// Sized at NUM_SELECTED_CONSOLE_LED_STRING_LEDS (64 today, per
// src/configuration.h) so the buffer is large enough for the largest
// example config's MAME 15-LED-wide block at offset 27 + 15 = 42.
CRGB selectedLeds[NUM_SELECTED_CONSOLE_LED_STRING_LEDS];

// CRGB color constants for the steady-state highlight. Dim warm white
// (CRGB(48, 36, 24)) reads as a soft beige-on-black in person without
// blowing out a dark room. Brightness is later scaled by FastLED's
// global brightness knob, so we don't divide here. Tuned on the bench
// once the perfboard is wired; the values are deliberately conservative
// so a misconfiguration can't glare the operator.
static const CRGB LEDSTRING_OFF_COLOR = CRGB::Black;
static const CRGB LEDSTRING_ON_COLOR = CRGB(48, 36, 24);

void ledstring_fillRange(int fromInclusive, int toExclusive, CRGB color) {
	// Pure host-testable interface in lib/LedStringPaint; this is a
	// thin wrapper that operates on the live selectedLeds[] buffer.
	// No-op when the range is empty (caller-clamped).
	if (fromInclusive >= toExclusive) {
		return;
	}
	for (int i = fromInclusive; i < toExclusive; ++i) {
		if (i >= 0 && i < NUM_SELECTED_CONSOLE_LED_STRING_LEDS) {
			selectedLeds[i] = color;
		}
	}
}

void ledstring_allOff() {
	// Same fill as ledstring_setConsole()'s reset-to-black step, but
	// without a window to paint afterwards. show() is synchronous on
	// the RP2040 PIO backend -- it blocks until the last bit of the
	// frame has clocked out -- so by the time this returns the strip
	// is genuinely dark, which is what lets the caller reset the MCU
	// immediately afterwards.
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  LEDSTRING_OFF_COLOR);
	FastLED.show();
}

void ledstring_init() {
	// addLeds() binds a second FastLED controller to GP21. The
	// controller is independent of the ring (CRGB leds[] on GP20 via
	// RR_FASTLED_DATA_PIN), so each strip gets its own PIO program and
	// its own show() cadence. On RP2350 the rp2040 PIO backend
	// transparently supports this (rpcommon/platforms/arm/rp2040).
	FastLED.addLeds<WS2812B, RR_FASTLED_STRING_DATA_PIN, GRB>(
		selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
	// Push black on boot. The strip powers up dark; ledstring_setConsole()
	// paints the active console's window after consoleDefinitions() lands
	// its first console.
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  CRGB::Black);
	FastLED.show();
}

// Paint the active console's [ledPosition, ledPosition+ledWidth) window
// in the steady-state highlight color; all other pixels black. Called
// from selectConsole() on every commit AND from ledstring_init() once
// the first console has been parsed.
//
// Reads through CurrentConsole() -- so this function MUST be called after
// consoleDefinitions() has populated src/consoles.cpp::consoles (the
// contract already enforced by main.cpp's setup() ordering).
//
// No animation: a hard "snap to new zone" paint. The pull-tween
// animation between zones is tracked under
// todo/open/2026-09-25_led-string-light-shows_DRAFT.md (S1) and is NOT
// part of this commit.
void ledstring_setConsole(int idx) {
	(void)idx;  // idx is the new currentConsoleIndex; the live data we
				// paint is from CurrentConsole(). kept in the signature
				// for forward-compatibility with S1 (which will read the
				// previous index from a small shadow cache to animate the
				// tween) and for symmetry with lightSingle(idx) on the
				// ring side.

	// Compute the paint math via the host-testable helper. Returns the
	// inclusive-exclusive window to light, clamped to
	// NUM_SELECTED_CONSOLE_LED_STRING_LEDS; zero window when the
	// console has no ledWidth (defensive) or the strip hasn't been
	// sized to fit (defensive against a misconfigured ledPosition).
	const retroroom_core::Console& c = CurrentConsole();
	retroroom_core::LedRange range =
		retroroom_core::computeConsoleWindow(c.led_position, c.led_width,
											NUM_SELECTED_CONSOLE_LED_STRING_LEDS);

	// Always start from black so a previously-lit adjacent zone doesn't
	// bleed into the new one when the new window is smaller.
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  LEDSTRING_OFF_COLOR);
	if (range.width > 0) {
		ledstring_fillRange(range.start, range.start + range.width,
						   LEDSTRING_ON_COLOR);
	}
	FastLED.show();
}

#endif  // ARDUINO && HAS_LEDS
