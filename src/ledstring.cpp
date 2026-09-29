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

// Assemble the colour palette from src/configuration.h. The core
// resolves each pixel to a *role* and this says what that role looks
// like, so the colour choices all live in one place instead of being
// spread through here as percentages of one base hue.
retroroom_core::RolePalette buildPalette() {
	retroroom_core::RolePalette p;
	p.colors[static_cast<int>(retroroom_core::LedRole::OFF)] = {0, 0, 0};
	p.colors[static_cast<int>(retroroom_core::LedRole::STACK)] = {
		LEDSTRING_COLOR_STACK_R, LEDSTRING_COLOR_STACK_G,
		LEDSTRING_COLOR_STACK_B};
	p.colors[static_cast<int>(retroroom_core::LedRole::LEAVING)] = {
		LEDSTRING_COLOR_LEAVING_R, LEDSTRING_COLOR_LEAVING_G,
		LEDSTRING_COLOR_LEAVING_B};
	p.colors[static_cast<int>(retroroom_core::LedRole::FILL)] = {
		LEDSTRING_COLOR_FILL_R, LEDSTRING_COLOR_FILL_G, LEDSTRING_COLOR_FILL_B};
	p.colors[static_cast<int>(retroroom_core::LedRole::TRAVEL)] = {
		LEDSTRING_COLOR_TRAVEL_R, LEDSTRING_COLOR_TRAVEL_G,
		LEDSTRING_COLOR_TRAVEL_B};
	p.colors[static_cast<int>(retroroom_core::LedRole::PROPOSAL)] = {
		LEDSTRING_COLOR_PROPOSAL_R, LEDSTRING_COLOR_PROPOSAL_G,
		LEDSTRING_COLOR_PROPOSAL_B};
	p.colors[static_cast<int>(retroroom_core::LedRole::SELECTED)] = {
		LEDSTRING_COLOR_SELECTED_R, LEDSTRING_COLOR_SELECTED_G,
		LEDSTRING_COLOR_SELECTED_B};
	return p;
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
// Progress through the current step, for the knob-turn fill. Distinct
// from the gate's raw position: the last *drawn* detent is 100%.
int stepPermille = 0;
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
	retroroom_core::StripPixel px[NUM_SELECTED_CONSOLE_LED_STRING_LEDS];
	retroroom_core::computeStripFrame(frame, px);
	for (int i = 0; i < NUM_SELECTED_CONSOLE_LED_STRING_LEDS; ++i) {
		const retroroom_core::LedColor c = retroroom_core::resolvePixel(frame, px[i]);
		selectedLeds[i] = CRGB(static_cast<uint8_t>(c.r),
							  static_cast<uint8_t>(c.g),
							  static_cast<uint8_t>(c.b));
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
	f.dimPct = LEDSTRING_DIM_PCT;
	f.travelPct = LEDSTRING_SELF_PCT;
	f.minFillLeds = LEDSTRING_FILL_MIN_LEDS;
	f.travelMs = LEDSTRING_TRAVEL_MS;
	f.travelPeakWidth = LEDSTRING_TRAVEL_PEAK_WIDTH;
	// The console that is actually selected, which stays dim through
	// every browse state. Not the one the browse is on -- that is the
	// whole distinction the role exists for.
	f.activeWindow = windowFor(currentConsoleIndex);
	f.palette = buildPalette();
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
// For a step within a shelf that is the edge of the console being left
// that the block travels away from -- trailing going forward, leading
// coming back. The core owns that rule, because getting it wrong is
// invisible going forward and reads as a mis-start coming back, and a
// rule like that belongs where the tests can see it.
//
// For a step *between* shelves it is the far end of the destination
// shelf: the one end of it the string does not arrive at, so the block
// sweeps the whole shelf and lands on the console. The knob goes one
// way and the light goes the other, which is odd and deliberate.
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
		return retroroom_core::travelEntryFor(leave, windowFor(to));
	}
	// Crossed the bridge between shelves. The block enters the shelf it
	// is landing on at the end the string does *not* arrive at, and
	// sweeps the whole of that shelf to the target -- so a step that is
	// two pixels wide in index space still reads as the long way round
	// the cabinet that it physically is.
	//
	// Which end that is depends on the direction of the crossing, and
	// this used to ignore that. The shelves are strung as one chain, so
	// the bridge joins the high end of the lower shelf to the low end
	// of the upper one: stepping *up* enters at the top of the shelf,
	// stepping back *down* enters at its bottom. Always taking the high
	// end looked right going up and collapsed going back, because the
	// console you are returning to sits at the bridge end of its own
	// shelf and so had nothing left to sweep.
	const int targetShelf = consoles[to].shelf;
	int lo = -1;
	int hi = -1;
	for (int i = 0; i < HowManyConsoles(); ++i) {
		if (consoles[i].shelf != targetShelf) {
			continue;
		}
		const retroroom_core::LedRange w = windowFor(i);
		if (lo < 0 || w.start < lo) {
			lo = w.start;
		}
		if (hi < 0 || w.start + w.width > hi) {
			hi = w.start + w.width;
		}
	}
	if (lo < 0) {
		return leaveEnd;
	}
	// The source is on the far side of that whole span, so its end
	// against the span's start says which side of it we are on.
	return (leaveEnd <= lo) ? hi : lo;
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
	// The leading edge walks across the gap as the operator turns. The
	// floor was applied by computeFillGeometry() and is not re-applied
	// here, so the run is the real gap at every angle of the knob.
	f.fillLead = retroroom_core::scaleFillLead(f.fillAnchor, f.fillLead,
											   f.fillForward, stepPermille);
	// The candidate keeps pulsing while the operator fills onward. It
	// is still the console a press would select, and it is what the fill
	// is running away from; making it part of the fill meant it stopped
	// pulsing the moment the knob moved again, so the console being
	// offered vanished one detent after it appeared.
	//
	// No pulse before the first snap: the anchor is the console already
	// selected, and that one is shown as selected, not as a proposal.
	if (fromIdx >= 0 && fromIdx < HowManyConsoles() &&
		fromIdx != currentConsoleIndex) {
		f.candidateWindow = windowFor(fromIdx);
	}
	// The pulse phase runs from the last snap, not from the detent, so
	// the candidate breathes continuously across a whole step instead of
	// restarting on every click.
	f.elapsedMs = (uint32_t)(millis() - animStartMs);
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
	f.from = windowFor(previewIdx);
	f.to = f.from;
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
		// Not a no-op any more, and the comment that used to say so was
		// the bug. The fill's *length* is the detent gate's position and
		// only changes when a detent arrives -- but the candidate
		// sitting behind it is pulsing, and that is a function of the
		// clock. Painting only on a detent froze the pulse at whatever
		// level the last click happened to land on, so the console
		// stopped breathing the instant the operator touched the knob
		// and went back to breathing when they stopped.
		//
		// This is the second time a mode here has been justified as
		// "nothing to advance" and been wrong: the frame stopped being
		// static before the loop was told. The pulse is computed from
		// elapsedMs, so the test is whether the frame is time-dependent
		// at all, and it now is.
		paintFilling();
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

void ledstring_browseProgress(int from, int to, int fraction, int progress) {
	// A detent in flight outranks anything still playing: the operator
	// has already moved on.
	mode = StripMode::FILLING;
	fromIdx = from;
	toIdx = to;
	fractionPermille = fraction;
	stepPermille = progress;
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
