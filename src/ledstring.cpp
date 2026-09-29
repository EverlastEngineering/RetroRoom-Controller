// Second FastLED strip driver on GP21 (SELECTED_CONSOLE_LED_STRING_DATA).
// See src/ledstring.h for the design rationale.
//
// This translation unit is excluded from the [env:test_native] host
// build -- it depends on FastLED's RP2040 PIO backend and the Arduino
// core, neither of which exist on the host. The paint *math* is lifted
// into lib/LedStringPaint precisely so it can be unit-tested on the
// host; see test/test_ledstring and test/test_ledstring_browse.

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

// CRGB color constants for the strip. The lit color is no longer a
// constant here: it is LEDSTRING_COLOR_* in src/configuration.h, scaled
// per-frame by whatever is being drawn (resting stack, blob, pulse,
// twinkle). Only the off color stays literal.
static const CRGB LEDSTRING_OFF_COLOR = CRGB::Black;

// ===========================================================================
// Browse + selection animation
// ===========================================================================
//
// The strip has one of four looks, and exactly one is live at a time:
//
//   RESTING  - the selected console's stack, lit down to the selection.
//              Nothing is repainting; ledstring_loop() is a no-op.
//   TRANSIT  - a browse is under way. Both console windows are dim and
//              a blob creeps between them as detents accumulate.
//   PREVIEW  - the browse snapped onto a console. Its window pulses.
//   SELECTING- a commit just happened. The whole strip twinkles, then
//              collapses to the resting paint for the new selection.
//
// The browse logic that decides *when* to move between these lives in
// lib/LedStringPaint (host-tested); this file only turns those decisions
// into pixels. Every timing and brightness comes from
// src/configuration.h, assembled into the config struct below.

namespace {

// What is on the strip right now.
enum class StripMode {
	RESTING,
	TRANSIT,
	PREVIEW,
	SELECTING,
};

// Assemble the "feel" knobs from configuration.h exactly once. See
// src/configuration.h for what each one is for and what it trades off.
retroroom_core::SelectionEffectConfig effectConfig() {
	retroroom_core::SelectionEffectConfig c;
	c.totalMs = LEDSTRING_SELECT_EFFECT_MS;
	// A twinkle longer than the whole effect would leave no room to
	// settle, which is how you end up with a permanently twinkling
	// strip. Clamp rather than trust the two values to agree.
	c.twinkleMs = (LEDSTRING_SELECT_TWINKLE_MS < LEDSTRING_SELECT_EFFECT_MS)
					 ? LEDSTRING_SELECT_TWINKLE_MS
					 : LEDSTRING_SELECT_EFFECT_MS;
	c.staggerMs = LEDSTRING_SELECT_STAGGER_MS;
	c.twinkleMin = LEDSTRING_SELECT_TWINKLE_MIN_PCT;
	c.twinkleMax = LEDSTRING_SELECT_TWINKLE_MAX_PCT;
	c.keepScale = LEDSTRING_SELF_PCT;
	return c;
}

const retroroom_core::SelectionEffectConfig kEffect = effectConfig();

const CRGB kStripColor =
	CRGB(LEDSTRING_COLOR_R, LEDSTRING_COLOR_G, LEDSTRING_COLOR_B);

// Scale the base color by a percentage. Everything on the strip is the
// base hue at some intensity, so retuning a brightness never re-picks a
// color. Scales are clamped rather than allowed to wrap into another
// channel.
CRGB scaled(int percent) {
	if (percent < 0) {
		percent = 0;
	}
	if (percent > 100) {
		percent = 100;
	}
	return CRGB(static_cast<uint8_t>(kStripColor.r * percent / 100),
				static_cast<uint8_t>(kStripColor.g * percent / 100),
				static_cast<uint8_t>(kStripColor.b * percent / 100));
}

StripMode mode = StripMode::RESTING;

// TRANSIT / PREVIEW targets. Indices are into src/consoles.cpp::consoles.
int fromIdx = 0;
int toIdx = 0;
int fractionPermille = 0;
int previewIdx = 0;
// SELECTING caches the console being committed and the pixel index the
// strip collapses to. Both are resolved when the effect starts so a
// config change part-way through cannot repaint the strip against a
// different console list.
int selectIdx = 0;
int selectKeepEnd = 0;

// millis() when the live animation started, and when the last frame
// went out. Both are unsigned-subtracted so a millis() wraparound is
// handled by the usual arithmetic.
uint32_t animStartMs = 0;
uint32_t lastFrameMs = 0;

// A console's clamped LED window. Defensive about the index: the
// console accessors index without checking, and this runs from an
// animation tick where a stale index would read past the end of the
// vector.
retroroom_core::LedRange windowFor(int idx) {
	const int n = HowManyConsoles();
	if (n <= 0 || idx < 0 || idx >= n) {
		return {0, 0};
	}
	const retroroom_core::Console& c = consoles[idx];
	return retroroom_core::computeConsoleWindow(
		c.led_position, c.led_width, NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
}

int keepEndFor(int idx) {
	const retroroom_core::Console& c = consoles[idx];
	return retroroom_core::computeKeepEnd(
		c.led_position, c.led_width, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
		LEDSTRING_KEEP_INCLUDES_SELECTED != 0);
}

// The resting paint: the stack above the console dim, the console's own
// window at full, everything below dark.
void paintResting(int idx) {
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  LEDSTRING_OFF_COLOR);
	const int n = HowManyConsoles();
	if (n <= 0 || idx < 0 || idx >= n) {
		return;
	}
	// [0, keepEnd) is what "above" means: the strip is indexed top of
	// the cabinet downward, so everything before the selected console's
	// window is above it. With LEDSTRING_KEEP_INCLUDES_SELECTED the
	// prefix runs over the console's own window too, and the brighter
	// fill immediately below paints over it -- which is the point, the
	// selection should be the brightest thing on the strip.
	const retroroom_core::LedRange w = windowFor(idx);
	const int keepEnd = keepEndFor(idx);
	ledstring_fillRange(0, keepEnd, scaled(LEDSTRING_ABOVE_PCT));
	if (w.width > 0) {
		ledstring_fillRange(w.start, w.start + w.width,
							scaled(LEDSTRING_SELF_PCT));
	}
	FastLED.show();
}

void paintTransit() {
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  LEDSTRING_OFF_COLOR);
	const retroroom_core::LedRange a = windowFor(fromIdx);
	const retroroom_core::LedRange b = windowFor(toIdx);
	ledstring_fillRange(a.start, a.start + a.width,
						scaled(LEDSTRING_BROWSE_FROM_PCT));
	ledstring_fillRange(b.start, b.start + b.width,
						scaled(LEDSTRING_BROWSE_TO_PCT));
	// The blob goes down last and at full brightness so it is
	// unmistakably the thing moving. The fraction is clamped here rather
	// than in the core so an out-of-range value degrades to "the blob
	// sits at one end" instead of extrapolating off the strip.
	int f = fractionPermille;
	if (f < 0) {
		f = 0;
	}
	if (f > 1000) {
		f = 1000;
	}
	const retroroom_core::LedRange blob = retroroom_core::computeBlobWindow(
		a.start, a.width, b.start, b.width, f, LEDSTRING_BLOB_WIDTH,
		NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
	ledstring_fillRange(blob.start, blob.start + blob.width,
						scaled(LEDSTRING_BLOB_PCT));
	FastLED.show();
}

void paintPreview(uint32_t elapsedMs) {
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  LEDSTRING_OFF_COLOR);
	const retroroom_core::LedRange w = windowFor(previewIdx);
	if (w.width > 0) {
		ledstring_fillRange(
			w.start, w.start + w.width,
			scaled(retroroom_core::computePulseScale(
				elapsedMs, LEDSTRING_PREVIEW_PULSE_MS,
				LEDSTRING_PREVIEW_PULSE_MIN_PCT,
				LEDSTRING_PREVIEW_PULSE_MAX_PCT)));
	}
	FastLED.show();
}

void paintSelecting(uint32_t elapsedMs) {
	for (int i = 0; i < NUM_SELECTED_CONSOLE_LED_STRING_LEDS; ++i) {
		selectedLeds[i] = scaled(retroroom_core::computeSelectScale(
			i, selectKeepEnd, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			elapsedMs, kEffect));
	}
	FastLED.show();
}

}  // namespace
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
	// Same fill as the resting paint's reset-to-black step, but without
	// anything to paint afterwards. show() is synchronous on the RP2040
	// PIO backend -- it blocks until the last bit of the frame has
	// clocked out -- so by the time this returns the strip is
	// genuinely dark, which is what lets the caller reset the MCU
	// immediately afterwards.
	//
	// Cancels any running animation first. Without that, the next
	// ledstring_loop() tick would repaint over the blank and the
	// operator would see the strip light up again on its way down to
	// the reboot.
	mode = StripMode::RESTING;
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
	FastLED.addLeds<WS2812B, SELECTED_CONSOLE_LED_STRING_DATA, GRB>(
		selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
	// Push black on boot. The strip powers up dark; ledstring_setConsole()
	// paints the active console's window after consoleDefinitions() lands
	// its first console.
	fill_solid(selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS,
			  CRGB::Black);
	FastLED.show();
}

// Paint the active console's [ledPosition, ledPosition+ledWidth) window
// and the stack above it; see ledstring.h for the full contract and
// ledstring_browseProgress() / ledstring_selectEffect() for the animated
// states that replaced the single detent-per-console paint.

void ledstring_setConsole(int idx) {
	// Resting paint for a console we are not animating toward. Also the
	// escape hatch that cancels whatever animation was running, which is
	// what ledstring_browseClear() and ledstring_selectEffect() rely on
	// to take over from it.
	mode = StripMode::RESTING;
	paintResting(idx);
}

void ledstring_loop() {
	if (mode == StripMode::RESTING) {
		return;
	}
	const uint32_t now = millis();
	if ((uint32_t)(now - lastFrameMs) < LEDSTRING_FRAME_INTERVAL_MS) {
		return;
	}
	lastFrameMs = now;

	switch (mode) {
	case StripMode::TRANSIT:
		// Deliberately a no-op. The blob's position belongs to the
		// detent gate and only changes when a detent arrives, which
		// repaints immediately -- there is nothing to advance here.
		break;
	case StripMode::PREVIEW:
		paintPreview((uint32_t)(now - animStartMs));
		break;
	case StripMode::SELECTING: {
		const uint32_t elapsed = (uint32_t)(now - animStartMs);
		paintSelecting(elapsed);
		if (elapsed >= kEffect.totalMs) {
			// Hand back to the resting paint rather than leaving the
			// last animated frame frozen on the strip. Routing through
			// paintResting also means the end of the effect is
			// pixel-identical to the boot paint.
			mode = StripMode::RESTING;
			paintResting(selectIdx);
		}
		break;
	}
	case StripMode::RESTING:
		break;
	}
}

void ledstring_browseProgress(int from, int to, int fraction) {
	// A detent in flight outranks a selection effect still playing out:
	// the operator has already moved on.
	mode = StripMode::TRANSIT;
	fromIdx = from;
	toIdx = to;
	fractionPermille = fraction;
	paintTransit();
}

void ledstring_browseSnap(int idx) {
	mode = StripMode::PREVIEW;
	previewIdx = idx;
	animStartMs = millis();
	// Paint straight away rather than waiting for the next
	// ledstring_loop() tick. The snap is the operator's confirmation
	// that a detent was accepted, so it has to land on the detent.
	paintPreview(0);
}

void ledstring_browseClear() {
	mode = StripMode::RESTING;
	paintResting(currentConsoleIndex);
}

void ledstring_selectEffect(int idx) {
	const int n = HowManyConsoles();
	if (n <= 0 || idx < 0 || idx >= n) {
		return;
	}
	mode = StripMode::SELECTING;
	selectIdx = idx;
	// Resolved now rather than at the end of the effect, so the pixels
	// that light up are the ones that were actually clicked on even if
	// the console list is reloaded underneath the animation.
	selectKeepEnd = keepEndFor(idx);
	animStartMs = millis();
	paintSelecting(0);
}

#endif  // ARDUINO && HAS_LEDS
