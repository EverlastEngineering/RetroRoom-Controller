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
	FILLING,  // a browse is under way: the knob-turn fill
	TRAVEL,   // the step completed: the scripted move onto the target
	PREVIEW,  // the travel finished: the target pulsing
	SELECTING,  // a commit: twinkle, then settle
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
	c.abovePct = LEDSTRING_ABOVE_PCT;
	c.selfPct = LEDSTRING_SELF_PCT;
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

// Browse state. Indices are into src/consoles.cpp::consoles.
int fromIdx = 0;
int toIdx = 0;
// How far the operator is through the current step, in permille of one
// step. Drives the fill; see the DetentGate comment for why it is a
// continuous position rather than a detent count.
int fractionPermille = 0;
int previewIdx = 0;
// The console being committed. Its target picture is resolved when the
// selection effect starts, not per frame.
int selectIdx = 0;

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

// Upper bound on how many console windows we will hand to a frame as
// "the ones above". The strip physically cannot hold more than
// NUM_SELECTED_CONSOLE_LED_STRING_LEDS non-empty windows, and the fixed
// array keeps the frame's layout allocation-free. Sized off the strip
// rather than a magic number so a longer strip cannot silently drop a
// console.
enum { kMaxAboveWindows = NUM_SELECTED_CONSOLE_LED_STRING_LEDS };

// Scratch for the windows above, resolved once per resting or
// selection frame. See collectAboveFor() for why it is a list and not a
// prefix.
retroroom_core::LedRange aboveBuffer[kMaxAboveWindows];

// The windows of every console entirely above `idx`, for the resting
// paint. collectAboveWindows() does the "entirely above" test; this
// only has to know the console list, which the core deliberately does
// not.
int collectAboveFor(int idx, retroroom_core::LedRange* out) {
	const int n = HowManyConsoles();
	if (n <= 0 || idx < 0 || idx >= n) {
		return 0;
	}
	retroroom_core::LedRange all[kMaxAboveWindows];
	int count = 0;
	for (int i = 0; i < n && count < kMaxAboveWindows; ++i) {
		if (i == idx) {
			continue;
		}
		all[count++] = windowFor(i);
	}
	return retroroom_core::collectAboveWindows(all, count, windowFor(idx).start,
											   out, kMaxAboveWindows);
}

// Turn a resolved frame into pixels and push it to the wire. This is
// the shell's entire remaining job: the layout -- which pixels are lit
// and how brightly -- is decided by retroroom_core::computeStripFrame(),
// which is host-tested and replayed by agent-script/ledstring-sim.sh.
// Keeping the decision here instead would mean two copies of the paint
// logic, one of which nobody would ever run.
// Per-animation frame accounting, consumed by reportFrameTiming().
uint32_t frameCount = 0;
uint32_t totalFrameUs = 0;
uint32_t worstFrameUs = 0;

// Turn a resolved frame into pixels and push it to the wire. This is
// the shell's entire remaining job: the layout -- which pixels are lit
// and how brightly -- is decided by retroroom_core::computeStripFrame(),
// which is host-tested and replayed by agent-script/ledstring-sim.sh.
// Keeping the decision here instead would mean two copies of the paint
// logic, one of which nobody would ever run.
void pushFrame(const retroroom_core::StripFrame& frame) {
	const uint32_t t0 = micros();
	int pct[NUM_SELECTED_CONSOLE_LED_STRING_LEDS];
	retroroom_core::computeStripFrame(frame, pct);
	for (int i = 0; i < NUM_SELECTED_CONSOLE_LED_STRING_LEDS; ++i) {
		selectedLeds[i] = scaled(pct[i]);
	}
	FastLED.show();
	// Time the work, not the wait. FastLED.show() is synchronous on the
	// RP2040 PIO backend -- it blocks until the last bit of the frame
	// has clocked out -- so this is the floor on the frame interval.
	const uint32_t cost = micros() - t0;
	if (cost > worstFrameUs) {
		worstFrameUs = cost;
	}
	totalFrameUs += cost;
	++frameCount;
}

// Print what the last animation actually achieved, so the frame rate is
// set from the board rather than guessed at. See the comment on
// LEDSTRING_FRAME_INTERVAL_MS in src/configuration.h.
void reportFrameTiming() {
	if (frameCount == 0) {
		return;
	}
	Serial.print("[ledstring] ");
	Serial.print(frameCount);
	Serial.print(" frames, mean work ");
	Serial.print(totalFrameUs / frameCount);
	Serial.print("us, worst frame ");
	Serial.print(worstFrameUs);
	Serial.print("us");
	frameCount = 0;
	totalFrameUs = 0;
	worstFrameUs = 0;
}

// Shared frame setup: the strip size and the brightness knobs, which
// come straight from src/configuration.h. The per-effect fields are
// filled in by the callers below.
retroroom_core::StripFrame baseFrame() {
	retroroom_core::StripFrame f;
	f.totalLeds = NUM_SELECTED_CONSOLE_LED_STRING_LEDS;
	f.abovePct = LEDSTRING_ABOVE_PCT;
	f.selfPct = LEDSTRING_SELF_PCT;
	f.fromPct = LEDSTRING_BROWSE_FROM_PCT;
	f.toPct = LEDSTRING_BROWSE_TO_PCT;
	f.blobPct = LEDSTRING_BLOB_PCT;
	f.blobWidth = LEDSTRING_BLOB_WIDTH;
	f.fillPct = LEDSTRING_FILL_PCT;
	f.dimPct = LEDSTRING_ABOVE_PCT;
	f.travelPct = LEDSTRING_SELF_PCT;
	f.minFillLeds = LEDSTRING_FILL_MIN_LEDS;
	f.travelMs = LEDSTRING_TRAVEL_MS;
	f.travelPeakWidth = LEDSTRING_TRAVEL_PEAK_WIDTH;
	f.pulseMinPct = LEDSTRING_PREVIEW_PULSE_MIN_PCT;
	f.pulseMaxPct = LEDSTRING_PREVIEW_PULSE_MAX_PCT;
	f.pulsePeriodMs = LEDSTRING_PREVIEW_PULSE_MS;
	f.select = kEffect;
	return f;
}

// The resting paint: the consoles above, dim, the selected console
// bright, everything else dark. The gaps between consoles' windows are
// the physical space between shelves and stay dark -- filling them
// makes the strip read as one continuous bar.
void paintResting(int idx) {
	retroroom_core::StripFrame f = baseFrame();
	f.effect = retroroom_core::StripEffect::RESTING;
	if (HowManyConsoles() > 0 && idx >= 0 && idx < HowManyConsoles()) {
		f.from = windowFor(idx);
		f.aboveCount = collectAboveFor(idx, aboveBuffer);
		f.aboveWindows = aboveBuffer;
	}
	pushFrame(f);
}

// The knob-turn progression indicator. The fill runs from the console
// being left to the console being reached, normalised to whatever space
// is actually between them (floored -- see computeFillEnd), and the
// console being left is dim throughout.
// The pixel the travelling block enters at.
//
// For a step within a shelf that is the trailing edge of the console
// being left -- the block peels off it and crosses the gap. For a step
// *between* shelves it is the far end of the destination shelf, so the
// block sweeps the whole shelf and lands on the console. The knob goes
// one way and the light goes the other, which is odd and deliberate.
//
// The shelves are strung as one continuous chain, so pixel order alone
// cannot tell you where one ends -- this is why the console's `shelf`
// field exists.
int travelEntryFor(int from, int to) {
	const retroroom_core::LedRange leave = windowFor(from);
	const int leaveEnd = leave.start + leave.width;
	if (from < 0 || from >= HowManyConsoles() || to < 0 ||
		to >= HowManyConsoles()) {
		return leaveEnd;
	}
	if (consoles[from].shelf == consoles[to].shelf) {
		return leaveEnd;
	}
	// Crossed a shelf: enter at the far end of the shelf we are landing
	// on, so the block sweeps all of it.
	const int targetShelf = consoles[to].shelf;
	int far = windowFor(to).start + windowFor(to).width;
	for (int i = 0; i < HowManyConsoles(); ++i) {
		if (consoles[i].shelf != targetShelf) {
			continue;
		}
		const retroroom_core::LedRange w = windowFor(i);
		if (w.start + w.width > far) {
			far = w.start + w.width;
		}
	}
	return far;
}

// Resolve a frame's browse path: which way the block comes from, and how
// far the knob-turn fill runs. Shared by the fill and the travel so the
// two cannot disagree about where the step is going.
void applyBrowsePath(retroroom_core::StripFrame& f, int from, int to) {
	const retroroom_core::LedRange target = windowFor(to);
	retroroom_core::computeTravelPath(windowFor(from), travelEntryFor(from, to),
									 target, LEDSTRING_TRAVEL_SPARK_LEDS, f);
}

// The knob-turn progression indicator.
//
// The fill's *length* is the gate's progress, so the indicator walks
// across the gap as the operator turns rather than appearing at its end.
// The floor is applied first, by computeTravelPath, and the progress then
// scales that floored run -- so a step across a shelf still has a run
// worth watching, and four detents still walk all of it.
void paintFilling() {
	retroroom_core::StripFrame f = baseFrame();
	f.effect = retroroom_core::StripEffect::FILLING;
	f.from = windowFor(fromIdx);
	applyBrowsePath(f, fromIdx, toIdx);

	const int start = f.fillFrom.start + f.fillFrom.width;
	const int full = retroroom_core::computeFillEnd(
		f.fillFrom, f.fillTo, f.minFillLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
	int reach = start + ((full - start) * fractionPermille) / 1000;
	if (reach < start) {
		reach = start;
	}
	f.fillTo = {reach, 0};
	f.minFillLeds = 0;  // already floored; do not floor it again
	pushFrame(f);
}

// The scripted travel: the block of light leaving the console behind and
// landing exactly on the one the operator picked. Time-driven, so it is
// smooth regardless of how the knob got to the end of the step -- which
// is the whole reason it replaced the knob-driven blob.
void paintTravel(uint32_t elapsedMs) {
	retroroom_core::StripFrame f = baseFrame();
	f.effect = retroroom_core::StripEffect::TRAVEL;
	applyBrowsePath(f, fromIdx, toIdx);
	f.elapsedMs = elapsedMs;
	pushFrame(f);
}

void paintPreview(uint32_t elapsedMs) {
	retroroom_core::StripFrame f = baseFrame();
	f.effect = retroroom_core::StripEffect::PREVIEW;
	f.to = windowFor(previewIdx);
	f.elapsedMs = elapsedMs;
	pushFrame(f);
}

void paintSelecting(uint32_t elapsedMs) {
	retroroom_core::StripFrame f = baseFrame();
	f.effect = retroroom_core::StripEffect::SELECTING;
	// The consoles above are resolved to *once*, when the effect starts,
	// not per frame. A config reload mid-animation would otherwise
	// repaint the twinkle against a different console list.
	f.from = windowFor(selectIdx);
	f.aboveCount = collectAboveFor(selectIdx, aboveBuffer);
	f.aboveWindows = aboveBuffer;
	f.elapsedMs = elapsedMs;
	pushFrame(f);
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
	case StripMode::FILLING:
		// Deliberately a no-op. The fill's length is the detent gate's
		// position and only changes when a detent arrives, which
		// repaints immediately -- there is nothing to advance here.
		break;
	case StripMode::TRAVEL: {
		// The one effect that runs on a clock rather than on the knob.
		// This is what makes it an animation instead of a readout.
		const uint32_t elapsed = (uint32_t)(now - animStartMs);
		paintTravel(elapsed);
		if (elapsed >= LEDSTRING_TRAVEL_MS) {
			// Hand over to the pulsing preview. The last travel frame is
			// already exactly the target window, so the handover is
			// invisible.
			mode = StripMode::PREVIEW;
			animStartMs = now;
		}
		break;
	}
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
			reportFrameTiming();
		}
		break;
	}
	case StripMode::RESTING:
		break;
	}
}

void ledstring_browseProgress(int from, int to, int fraction) {
	// A detent in flight outranks anything still playing: the operator
	// has already moved on.
	mode = StripMode::FILLING;
	fromIdx = from;
	toIdx = to;
	fractionPermille = fraction;
	paintFilling();
}

void ledstring_browseSnap(int from, int to) {
	// The step completed. Start the scripted travel rather than jumping
	// straight to the preview -- the movement is the confirmation that a
	// detent was accepted, and where the light went tells the operator
	// which way they were turning as well as that they landed.
	mode = StripMode::TRAVEL;
	fromIdx = from;
	toIdx = to;
	previewIdx = to;
	animStartMs = millis();
	// Paint straight away rather than waiting for the next
	// ledstring_loop() tick, so the travel starts on the detent that
	// caused it.
	paintTravel(0);
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
	animStartMs = millis();
	paintSelecting(0);
}

#endif  // ARDUINO && HAS_LEDS
