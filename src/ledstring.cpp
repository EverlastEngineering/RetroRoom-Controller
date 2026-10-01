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
// The buffer is sized at the *build* capacity, not the configured
// length, and FastLED is told the configured length. So the allocation
// is fixed and the string can be a different length without touching
// anything but the config -- which is the whole point of putting
// totalLeds in the JSON. The spare pixels are never written and never
// clocked out.
//
// A heap buffer sized at load would avoid the slack, and would also
// make a malformed length a way to write off the end of the heap.
CRGB selectedLeds[retroroom_core::kLedStripCapacity];

// CRGB colour constants for the strip. The lit colour is no longer a
// constant here: it is a colour in the config's `led` block, scaled
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

// The commit's two halves, from the config's `led` block.
//
// Rebuilt on each use rather than cached in a namespace-scope constant,
// and that is deliberate. This used to be:
//
//     const SelectionEffectConfig kEffect = effectConfig();
//
// at namespace scope. That is a *static initialiser*, so it ran during
// C++ static init -- before main(), therefore long before
// consoleDefinitions(), therefore while ledFeel was still
// defaultLedFeel(). led.explodeMs and led.igniteMs were pinned at
// 400/200 for the life of the process and no config could move them.
//
// It was invisible here because every *other* feel value in this file is
// read live, per frame, by baseFrame() below. One stale constant among
// a dozen correct reads looks exactly like a working feature that
// ignores its config. And no reordering of setup() can repair it: the
// capture happens before setup() is entered, so the fix has to be "do
// not read ledFeel at static-init time", not "read it later".
//
// The rule, learned the hard way in this file and in controls_init():
// a value copied out of ledFeel during static initialisation predates
// the config, and there is no boot order that fixes that.
retroroom_core::SelectionEffectConfig effectConfig() {
	retroroom_core::SelectionEffectConfig c;
	// The total is the sum, not a third number. Two phases that each
	// think they own the end of the effect is how the loop and the
	// config drift apart.
	c.explodeMs = ledFeel.explodeMs;
	c.igniteMs = ledFeel.igniteMs;
	c.totalMs = c.explodeMs + c.igniteMs;
	c.abovePct = ledFeel.abovePct;
	c.selfPct = ledFeel.selfPct;
	return c;
}

// Assemble the colour palette from src/configuration.h. The core
// resolves each pixel to a *role* and this says what that role looks
// like, so the colour choices all live in one place instead of being
// spread through here as percentages of one base hue.
retroroom_core::RolePalette buildPalette() {
	retroroom_core::RolePalette p;
	p.colors[static_cast<int>(retroroom_core::LedRole::OFF)] = {0, 0, 0};
	p.colors[static_cast<int>(retroroom_core::LedRole::STACK)] = {
		ledFeel.colorR[retroroom_core::kRoleStack], ledFeel.colorG[retroroom_core::kRoleStack],
		ledFeel.colorB[retroroom_core::kRoleStack]};
	p.colors[static_cast<int>(retroroom_core::LedRole::LEAVING)] = {
		ledFeel.colorR[retroroom_core::kRoleLeaving], ledFeel.colorG[retroroom_core::kRoleLeaving],
		ledFeel.colorB[retroroom_core::kRoleLeaving]};
	p.colors[static_cast<int>(retroroom_core::LedRole::FILL)] = {
		ledFeel.colorR[retroroom_core::kRoleFill], ledFeel.colorG[retroroom_core::kRoleFill], ledFeel.colorB[retroroom_core::kRoleFill]};
	p.colors[static_cast<int>(retroroom_core::LedRole::TRAVEL)] = {
		ledFeel.colorR[retroroom_core::kRoleTravel], ledFeel.colorG[retroroom_core::kRoleTravel],
		ledFeel.colorB[retroroom_core::kRoleTravel]};
	p.colors[static_cast<int>(retroroom_core::LedRole::PROPOSAL)] = {
		ledFeel.colorR[retroroom_core::kRoleProposal], ledFeel.colorG[retroroom_core::kRoleProposal],
		ledFeel.colorB[retroroom_core::kRoleProposal]};
	// The selected role's colour is straight from the config, night
	// mode or not. The palette says what a role *is*; night mode is a
	// statement about what the strip shows right now, so it rides on
	// the frame (StripFrame::nightMode) and the core applies it. Doing
	// it here would have been one line too, and needed hardware to
	// check.
	p.colors[static_cast<int>(retroroom_core::LedRole::SELECTED)] = {
		ledFeel.colorR[retroroom_core::kRoleSelected], ledFeel.colorG[retroroom_core::kRoleSelected],
		ledFeel.colorB[retroroom_core::kRoleSelected]};
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

// When the last rotary detent arrived, which is what the overshoot
// retreat counts its silence from. Written only by
// ledstring_browseProgress(), because that is the only place a detent
// becomes a frame -- paintFilling() runs every frame and must not be
// able to reset it.
uint32_t lastDetentMs = 0;

// Whether the last painted fill still has LEDs left to give back.
// The ring's idle give-up asks, because giving up resets the browse and
// the reset takes the run with it -- so without this the ring times out
// part way through a long retreat and the strip empties in one step
// instead of unwinding.
//
// The mode is re-checked on read so the flag cannot go stale: every
// other mode also ends the retreat, and one of them ending without
// clearing the flag would strand the ring's countdown forever.
static bool fillRetreatInProgress = false;

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
		c.led_position, c.led_width, ledFeel.totalLeds);
}

// Upper bound on how many console windows we will hand to a frame as
// "the ones above". The strip physically cannot hold more than
// above list is bounded by the number of *consoles*, not the number of
// LEDs: there can only ever be one entry per console. Sizing it off
// the strip happened to work because there were never more consoles
// than LEDs, which is a coincidence and not a relationship.
enum { kMaxAboveWindows = 64 };

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

// LEDs a console's commit animation is allowed to touch: the shelf it
// sits on, as a half-open window.
//
// Declared per shelf rather than per console because it is a property of
// the furniture, not of a console: the string runs wider than the
// consoles on it, and that bare space at either end is where the commit
// animation expands into. A config that says nothing about shelf extents
// gets the span of the consoles on the shelf, which never leaves the
// shelf but does not use the bare space either.
retroroom_core::LedRange shelfBoundsFor(int idx) {
	const int n = HowManyConsoles();
	if (idx < 0 || idx >= n) {
		return {0, 0};
	}
	const int shelf = consoles[idx].shelf;
	for (const Shelf& s : shelfBounds) {
		if (s.id == shelf) {
			return {s.fromLed, (s.toLed - s.fromLed) + 1};
		}
	}
	// No declared extent. The whole strip is then the bound, which is the
	// only thing we actually know -- deriving the shelf from the consoles
	// on it would say the shelf is exactly as wide as its contents, and
	// the first and last console on a shelf would then have no room to
	// expand into at all. That is not what a shelf is: there is bare
	// string past the end console, and using it is the whole point of
	// declaring extents.
	return {0, ledFeel.totalLeds};
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
	// Static, and sized at the build capacity: on the stack this is
	// several kilobytes on every single frame, and nothing here is
	// reentrant -- the paint functions are only ever called from loop().
	static retroroom_core::StripPixel px[retroroom_core::kLedStripCapacity];
	retroroom_core::computeStripFrame(frame, px);
	// One global scale, applied here where the levels become pixels --
	// after every role percentage, so the balance the bench work
	// established is preserved and only the overall level moves. See
	// ledBrightnessScale() in src/lighting.cpp for why this is not
	// FastLED's setBrightness().
	//
	// Read live, and read the config directly: this used to go through
	// a runtime override accessor, which made "night mode" a scale on
	// every pixel. Night mode is a palette question now (buildPalette
	// blacks the SELECTED role), so this scale has one input again.
	const uint8_t scale = static_cast<uint8_t>(
		(static_cast<uint32_t>(255) *
		 static_cast<uint32_t>(ledFeel.brightnessPct)) / 100u);
	for (int i = 0; i < ledFeel.totalLeds; ++i) {
		const retroroom_core::LedColor c = retroroom_core::resolvePixel(frame, px[i]);
		selectedLeds[i] = CRGB(static_cast<uint8_t>(c.r * scale / 255),
							  static_cast<uint8_t>(c.g * scale / 255),
							  static_cast<uint8_t>(c.b * scale / 255));
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
// ledFeel.frameIntervalMs in src/configuration.h.
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
	f.totalLeds = ledFeel.totalLeds;
	f.abovePct = ledFeel.abovePct;
	f.selfPct = ledFeel.selfPct;
	f.fromPct = ledFeel.browseFromPct;
	f.toPct = ledFeel.browseToPct;
	f.blobPct = ledFeel.blobPct;
	f.blobWidth = ledFeel.blobWidth;
	f.fillPct = ledFeel.fillPct;
	f.dimPct = ledFeel.dimPct;
	f.travelPct = ledFeel.selfPct;
	f.minFillLeds = ledFeel.fillMinLeds;
	f.travelMs = ledFeel.travelMs;
	f.travelPeakWidth = ledFeel.travelPeakWidth;
	// The console that is actually selected, which stays dim through
	// every browse state. Not the one the browse is on -- that is the
	// whole distinction the role exists for.
	f.activeWindow = windowFor(currentConsoleIndex);
	f.palette = buildPalette();
	f.nightMode = nightMode();
	f.pulseMinPct = ledFeel.pulseMinPct;
	f.pulseMaxPct = ledFeel.pulseMaxPct;
	f.pulsePeriodMs = ledFeel.pulseMs;
	f.select = effectConfig();
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
									 target, ledFeel.travelSparkLeds, f);
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
	// floor was applied by computeTravelGeometry() and is not re-applied
	// here, so the run is the real gap at every angle of the knob.
	f.fillLead = retroroom_core::scaleFillLead(f.fillAnchor, f.fillLead,
											   f.fillForward, stepPermille);
	// The overshoot retreat. The operator turned past a console and
	// stopped, and the run is still pointing at a console they did not
	// ask for, so give it back -- one LED at a time from the leading
	// edge, which is what rolling the knob back would have looked like.
	//
	// Measured from the last detent rather than from the start of the
	// fill, so a step still in progress is never withdrawn underneath
	// the operator. Turning again resets it: a detent is an intent to go
	// somewhere, and the retreat must never argue with that.
	//
	// Computed in permille so the LED being given back *dims* rather
	// than being switched off -- see StripFrame::fillRetreatPermille.
	if (ledFeel.fillRetreatDelayMs > 0 &&
		ledFeel.fillRetreatStepMs > 0) {
		const uint32_t quietMs = (uint32_t)(millis() - lastDetentMs);
		const int runLeds = (f.fillLead > f.fillAnchor)
								? f.fillLead - f.fillAnchor
								: f.fillAnchor - f.fillLead;
		if (quietMs > (uint32_t)ledFeel.fillRetreatDelayMs) {
			const uint32_t into = quietMs - (uint32_t)ledFeel.fillRetreatDelayMs;
			int retreat = static_cast<int>((static_cast<long long>(into) * 1000) /
										  (uint32_t)ledFeel.fillRetreatStepMs);
			// Never withdraw more than the run holds. The core clamps as
			// well, but clamping here keeps the frame honest for anything
			// else that reads the retreat.
			if (retreat > runLeds * 1000) {
				retreat = runLeds * 1000;
			}
			f.fillRetreatPermille = retreat;
		}
		// Started but not finished. Checked outside the quiet test so it
		// also covers the run being exactly one LED long, where the
		// retreat can finish inside a single frame.
		fillRetreatInProgress = (quietMs > (uint32_t)ledFeel.fillRetreatDelayMs) &&
								(f.fillRetreatPermille < runLeds * 1000);
	}
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
	// The shelf bound on the explosion. This is resolved once, when the
	// effect starts, for the same reason the above-list is: a config
	// reload mid-animation must not re-shape a frame that is already
	// under way.
	f.selectBounds = shelfBoundsFor(selectIdx);
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
		if (i >= 0 && i < ledFeel.totalLeds) {
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
	fill_solid(selectedLeds, ledFeel.totalLeds,
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
		selectedLeds, ledFeel.totalLeds);
	// Push black on boot. The strip powers up dark; ledstring_setConsole()
	// paints the active console's window after consoleDefinitions() lands
	// its first console.
	fill_solid(selectedLeds, ledFeel.totalLeds,
			  CRGB::Black);
	FastLED.show();

	// What the animations will actually run at, now that
	// consoleDefinitions() has parsed the config. Every value here is
	// settable in `led` and none of them are otherwise visible at boot,
	// which is why a setting that "does nothing" has been impossible to
	// tell from a setting that was never applied -- the whole reason the
	// kEffect bug above survived.
	//
	// The commit pair is read back through effectConfig() rather than
	// straight off ledFeel, so this line reports what the strip runs on.
	// If it ever disagrees with the file, that disagreement is the bug.
	//
	// The ring timings are printed from here too, even though
	// lighting.cpp owns them, because lighting_init() runs *before*
	// consoleDefinitions() and so cannot print the parsed values at all.
	// One boot report for the one `led` block beats two half-reports from
	// the two places that happen to run at a usable time.
	const retroroom_core::SelectionEffectConfig eff = effectConfig();
	Serial.print("feel: commit ");
	Serial.print(eff.explodeMs);
	Serial.print("+");
	Serial.print(eff.igniteMs);
	Serial.print("ms, travel ");
	Serial.print(ledFeel.travelMs);
	Serial.print("ms, ring ");
	Serial.print(ledFeel.ringIdleMs);
	Serial.print("ms idle / ");
	Serial.print(ledFeel.ringFlashMs);
	Serial.println("ms flash");
}

// Paint the active console's [ledPosition, ledPosition+ledWidth) window
// and the stack above it; see ledstring.h for the full contract and
// ledstring_browseProgress() / ledstring_selectEffect() for the animated
// states that replaced the single detent-per-console paint.

// Repaint the resting picture now.
//
// ledstring_loop() is a no-op in RESTING, on purpose: an idle strip has
// nothing to animate and re-sending the same frame forever is time taken
// from everything else. That is also why a setting changed from the
// config menu would otherwise appear to do nothing -- the value is
// applied, but nothing asks the strip to redraw it.
void ledstring_repaint() {
	if (mode == StripMode::RESTING) {
		paintResting(currentConsoleIndex);
	}
}

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
	if ((uint32_t)(now - lastFrameMs) < ledFeel.frameIntervalMs) {
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
		if (elapsed >= ledFeel.travelMs) {
			// Hand over to the pulsing preview. The last travel frame is
			// already exactly the target window, so the handover is
			// invisible.
			mode = StripMode::PREVIEW;
			// Start the pulse at its *peak*, not its trough. The travel
			// arrives at full brightness on that same window, so a pulse
			// beginning at pulseMinPct steps the console down by most of
			// its range at exactly the moment the movement resolves into
			// an answer. That is a step in luminance, which no amount of
			// colour matching fixes -- it took matching the hue to see it
			// was left.
			//
			// The pulse is a parabola peaking mid-period, so the origin
			// moves half a period back rather than the pulse being
			// inverted. That also leaves the phase continuous from here
			// on, so the candidate's pulse carries on through the
			// following fill instead of restarting. The subtraction is
			// deliberately allowed to wrap: elapsedMs is computed the
			// same way, so the two agree either side of millis()' rollover.
			animStartMs = now - (uint32_t)(ledFeel.pulseMs / 2);
		}
		break;
	}
	case StripMode::PREVIEW:
		paintPreview((uint32_t)(now - animStartMs));
		break;
	case StripMode::SELECTING: {
		const uint32_t elapsed = (uint32_t)(now - animStartMs);
		paintSelecting(elapsed);
		if (elapsed >= effectConfig().totalMs) {
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
	// A detent is a new intent, so it restarts the quiet clock the
	// overshoot retreat is waiting on. Nothing else may write this:
	// paintFilling() is called every frame now, and it reads it.
	lastDetentMs = millis();
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
	// The run is gone with the browse, so there is nothing left to give
	// back. The read side re-checks the mode anyway; clearing it here
	// keeps the flag meaning what its name says.
	fillRetreatInProgress = false;
	paintResting(currentConsoleIndex);
}

bool ledstring_fillRetreatInProgress() {
	// The mode is checked as well as the flag, so a mode change that
	// does not go through ledstring_browseClear() -- a commit, a snap --
	// cannot leave the ring's countdown held forever on a stale flag.
	return fillRetreatInProgress && mode == StripMode::FILLING;
}

bool ledstring_browseRetreatPending() {
	// The decision itself is in lib/LedStringPaint as retreatStillOwed(),
	// with the reasoning about why the delay counts as owed. It is a
	// pure function precisely because this predicate got it wrong three
	// times in a row while it lived here where no test could reach it.
	const bool enabled =
		ledFeel.fillRetreatDelayMs > 0 && ledFeel.fillRetreatStepMs > 0;
	const uint32_t quietMs = (uint32_t)(millis() - lastDetentMs);
	return retroroom_core::retreatStillOwed(
		mode == StripMode::FILLING, enabled, quietMs,
		ledFeel.fillRetreatDelayMs, fillRetreatInProgress);
}

const char* ledstring_modeName() {
	switch (mode) {
	case StripMode::RESTING: return "RESTING";
	case StripMode::FILLING: return "FILLING";
	case StripMode::TRAVEL: return "TRAVEL";
	case StripMode::PREVIEW: return "PREVIEW";
	case StripMode::SELECTING: return "SELECTING";
	}
	return "?";
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
