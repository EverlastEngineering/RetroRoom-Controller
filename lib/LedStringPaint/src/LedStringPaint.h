#pragma once

// Functional core for the second-strip LED paint math.
//
// Pure data + math: no Arduino headers, no FastLED dependency. Compiles
// and unit-tests on the host via PlatformIO + Unity. The shell-side
// wrapper in src/ledstring.{h,cpp} copies the computed window into the
// live CRGB selectedLeds[] buffer and pushes FastLED.show(); this core
// has no knowledge of either.
//
// Why split: the host environment ([env:test_native]) excludes src/ and
// does not link FastLED. Lifting the paint decision into a pure lib
// makes the invariant ("only pixels in [ledPos, ledPos+ledWidth) are
// non-zero") testable on the host without emulating FastLED.

#include <cstddef>
#include <cstdint>

namespace retroroom_core {

// Inclusive-exclusive pixel range computed from a console's LED metadata.
// width == 0 means "nothing to paint" -- caller should clear the strip
// to its off-color instead of touching individual pixels.
struct LedRange {
	int start;   // >= 0, <= totalLeds
	int width;   // >= 0; clamped so start + width <= totalLeds
};

// Compute the inclusive-exclusive window [start, start+width) of pixels
// that should be lit for the given console metadata.
//
// Returns {0, 0} when:
//   - ledWidth <= 0 (a console with no strip mapping)
//   - ledPosition < 0 (malformed config)
//   - ledPosition >= totalLeds (window starts past the end of the strip)
//   - ledWidth would push past totalLeds (defensive clamp; a console
//     whose [ledPosition, ledPosition+ledWidth) range would overflow
//     the strip is left unpainted rather than wrapped -- if you see a
//     console unexpectedly dark, check ledPosition + ledWidth <= totalLeds)
//
// Inputs:
//   ledPosition: pixel index of the first LED in the window (>= 0
//                 expected; <0 returns a zero-width range to defend
//                 against corrupted JSON).
//   ledWidth:     number of pixels in the window (>= 1 expected for
//                 non-zero widths; <= 0 returns a zero-width range).
//   totalLeds:    the size of the physical strip (NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
//                 used to clamp the window.
LedRange computeConsoleWindow(int ledPosition, int ledWidth, int totalLeds);

// ---------------------------------------------------------------------------
// Rotary detent gating
// ---------------------------------------------------------------------------

// The browse cursor no longer moves one console per detent. Instead a
// console step takes `detentsPerStep` physical detents, and the strip
// shows a "blob" creeping toward the next console while they accumulate.
// The final detent snaps the cursor and starts the preview pulse.
//
// If the operator keeps spinning, the requirement drops to
// `fastDetentsPerStep` so a long run through the list stays quick.
struct DetentGateConfig {
	DetentGateConfig()
		: detentsPerStep(5), fastDetentsPerStep(2), fastSpinWindowMs(1000) {}
	DetentGateConfig(int per, int fast, std::uint32_t window)
		: detentsPerStep(per), fastDetentsPerStep(fast), fastSpinWindowMs(window) {}

	// Detents required to move one console when the operator is turning
	// deliberately. Clamped to >= 1 on use.
	int detentsPerStep;
	// Detents required once the operator is spinning fast. Clamped to
	// >= 1 on use, and to <= detentsPerStep (a "fast" threshold slower
	// than the deliberate one would be nonsense).
	int fastDetentsPerStep;
	// A detent that arrives within this many ms of the previous one
	// marks the spin as fast. 0 disables escalation entirely.
	std::uint32_t fastSpinWindowMs;
};

// What one physical detent did to the gate. Returned by value so the
// caller can act on the whole decision without reaching back into the
// gate for the intermediate state.
struct DetentEvent {
	// How far the operator is from the anchor console, in permille of
	// one console step. Positive = past the anchor toward the forward
	// neighbour, negative = past it toward the backward one. 0 means
	// "sitting exactly on the anchor".
	//
	// This is deliberately a *continuous* position rather than a detent
	// counter: the shell lerps the blob between the anchor and the
	// target console by this fraction, so turning the knob back
	// mid-transit walks the blob back the way it came instead of
	// teleporting it to the far side.
	int fractionPermille;

	// The threshold in force for this detent (clamped to >= 1), and
	// whether the fast one was used. Surfaced for the serial log so the
	// escalation is visible when tuning the feel.
	int detentsPerStep;
	bool fastMode;

	// Which neighbour of the anchor the operator is currently heading
	// toward: +1 forward, -1 backward. The shell computes the target
	// console with this. It is the sign of the *position*, not of the
	// detent that just arrived -- turning back while part-way to the
	// forward console keeps this +1, because the blob is still out on
	// that side and must retreat along the same path.
	int targetDirection;

	// True on the single detent that completed a step. Only then does
	// the shell move the browse anchor. When true, `targetDirection`
	// is the direction the cursor moved.
	bool advanced;

	// Whole detents consumed toward the current step. For logging.
	int detents;

	// -1 when the encoder reported a direction that isn't a real detent
	// (rotaryEncoderTick() drops those before calling us, but the gate
	// defends rather than trusting).
	int direction;
};

// Accumulates detents into a continuous position between consoles.
class DetentGate {
  public:
	DetentGate();

	void configure(const DetentGateConfig& cfg);
	const DetentGateConfig& config() const { return cfg_; }

	// Drop all accumulated progress and the fast-spin memory. Called
	// when the browse is abandoned (ring faded out) and after a commit.
	void reset();

	// Feed one detent. `direction` is +1 or -1; anything else is
	// reported as a no-op so the caller can still repaint. `nowMs` is
	// a millis() reading -- the gate stores the previous one to decide
	// whether the spin has picked up.
	DetentEvent onDetent(int direction, std::uint32_t nowMs);

	int fractionPermille() const { return fraction_; }
	bool fastMode() const { return fastMode_; }
	// Detents required for the next step under the current mode.
	int detentsPerStep() const;

  private:
	// Whole detents consumed toward the current step, derived from
	// fraction_ and the threshold in force. For logging only.
	int detentProgressPermille(int detentsPerStep) const;

	DetentGateConfig cfg_;
	// Position relative to the anchor, in permille of one console step.
	int fraction_;
	// Carries the division remainder of `1000 / detentsPerStep` so a
	// threshold that doesn't divide 1000 evenly (3, 6, 7...) still
	// completes in exactly `detentsPerStep` detents rather than
	// rounding down and never arriving.
	int detentRemainder_;
	bool fastMode_;
	bool hasLastDetent_;
	std::uint32_t lastDetentMs_;
};

// ---------------------------------------------------------------------------
// Blob travel
// ---------------------------------------------------------------------------

// Smoothstep-eased fraction in permille: 0 -> 0, 1000 -> 1000, with the
// midpoint at 500. Used for the blob's travel so it eases out of the
// anchor and decelerates into the snap instead of sliding linearly --
// the deceleration is what makes the final detent read as an arrival.
int easeInOutPermille(int fractionPermille);

// The blob's window during a browse. Interpolates a band `blobWidth`
// pixels wide between the centre of `from` and the centre of `to`.
// `fractionPermille` is the DetentGate position: 0 sits the blob on
// `from`, 1000 lands it dead centre on `to`.
//
// A zero-width window (a console with no LED mapping) degrades to
// using its start pixel as the centre, so the blob still has somewhere
// to travel from. The result is clamped into the strip; a blob wider
// than the strip is truncated rather than wrapped.
LedRange computeBlobWindow(int fromStart, int fromWidth, int toStart,
                           int toWidth, int fractionPermille, int blobWidth,
                           int totalLeds);

// ---------------------------------------------------------------------------
// Preview pulse
// ---------------------------------------------------------------------------

// Brightness percentage (0..100) for the console currently under
// consideration, as a function of how long it has been pulsing.
// A parabola over `periodMs` rather than a triangle or a sine: it peaks
// in the middle of each cycle and falls away at both ends, which reads
// as a soft breathing glow and needs no libm.
int computePulseScale(std::uint32_t elapsedMs, std::uint32_t periodMs,
                      int minPercent, int maxPercent);

// ---------------------------------------------------------------------------
// Selection effect
// ---------------------------------------------------------------------------

// Shape of the twinkle-then-settle played when a browse is committed.
// Every field is surfaced so the "feel" can be tuned from
// src/configuration.h without touching this core.
struct SelectionEffectConfig {
	SelectionEffectConfig()
		: totalMs(900), twinkleMs(350), staggerMs(6), twinkleMin(12),
		  twinkleMax(100), abovePct(22), selfPct(100) {}
	SelectionEffectConfig(std::uint32_t total, std::uint32_t twinkle,
	                      std::uint32_t stagger, int tmin, int tmax,
	                      int above, int self)
		: totalMs(total), twinkleMs(twinkle), staggerMs(stagger),
		  twinkleMin(tmin), twinkleMax(tmax), abovePct(above),
		  selfPct(self) {}

	// Whole effect. The user-visible contract is that it finishes in
	// under a second.
	std::uint32_t totalMs;
	// Portion of the effect spent twinkling the entire strip before it
	// starts settling. Clamped to totalMs.
	std::uint32_t twinkleMs;
	// Per-pixel delay added to the settle ramp, so the collapse ripples
	// down the strip instead of snapping in one frame. The whole
	// stagger is subtracted from the ramp length, so raising this too
	// far shortens the ramp rather than stretching the effect.
	std::uint32_t staggerMs;
	// Brightness percentage of the darkest and brightest twinkle samples.
	int twinkleMin;
	int twinkleMax;
	// Brightness percentages the strip settles to. The split is not
	// cosmetic: the effect has to land on *exactly* the resting paint,
	// which is the stack above the console dim and the console's own
	// window bright. A single flat level would leave the whole prefix at
	// console brightness and then visibly dim the moment the effect
	// handed back to the resting paint.
	int abovePct;
	int selfPct;
};

// The exclusive pixel index the strip settles to: the lit prefix is
// [0, result). This is what "reduces down to only the ones above the
// selected console" means concretely.
//
// `includeSelf` selects between the two readings of that phrase:
//   true  -- light everything from the top of the strip down to and
//            including the selected console's own window. The stack
//            reads as lit down to the selection.
//   false -- stop just short, so only the pixels physically above the
//            selected console's window stay lit and the selection
//            itself goes dark. Reads oddly (the thing you just selected
//            turns off) but is the literal reading of the wording, so
//            it stays available as a tuning knob.
int computeKeepEnd(int ledPosition, int ledWidth, int totalLeds,
                   bool includeSelf);

// Deterministic per-pixel pseudo-random brightness sample in [0, 1023].
// Integer-only so host tests can assert exact frames. `tick` is a frame
// counter, not milliseconds -- the caller divides by whatever cadence
// the twinkle should visibly run at.
int twinkleSample(int pixel, std::uint32_t tick);

// Brightness percentage (0..100) for one pixel at `elapsedMs` into the
// selection effect. `pixel` must be < totalLeds.
//
// The pixel settles on one of three values, matching the resting paint
// exactly:
//
//   pixel <  consoleStart -> cfg.abovePct   (the stack above the selection)
//   pixel <  keepEnd      -> cfg.selfPct    (the selection itself)
//   otherwise             -> 0             (below the selection, goes dark)
//
// The split is not cosmetic. A single flat settle level would leave the
// whole prefix at console brightness and then visibly dim the moment the
// effect handed back to the resting paint -- a flash at exactly the
// moment the operator is looking at the result.
//
// Twinkles with the rest of the strip for the first `twinkleMs`, then
// ramps to its final value over a ramp that starts `pixel *
// staggerMs` late. Returns the settled value once the effect is over, so
// the caller can use the last frame as the resting paint.
int computeSelectScale(int pixel, int keepEnd, int consoleStart, int totalLeds,
                       std::uint32_t elapsedMs,
                       const SelectionEffectConfig& cfg);

// ---------------------------------------------------------------------------
// Whole-strip frames
// ---------------------------------------------------------------------------

// What the strip is showing right now. One of the four states the shell
// animates between.
enum class StripEffect {
	RESTING,    // the stack, lit down to the selection
	TRANSIT,    // a browse in progress: a blob between two windows
	PREVIEW,    // a browse snapped: the target console pulsing
	SELECTING,  // a commit in progress: twinkle, then settle
};

// One frame of the strip, fully resolved: the caller has already turned
// console indices into LED windows, and filled in the brightness
// percentages it wants. computeStripFrame() then produces the whole
// frame as one percentage per pixel.
//
// This exists so the *only* thing left in src/ledstring.cpp is
// percentage -> CRGB and FastLED.show(). Everything that decides which
// pixels are lit lives here, which means it is host-testable and can be
// replayed offline by agent-script/ledstring-sim.sh -- see the
// simulator's header for why that matters.
struct StripFrame {
	StripEffect effect = StripEffect::RESTING;

	// Size of the physical strip. Everything is clamped to this.
	int totalLeds = 0;

	// Console windows, resolved by the caller (it owns the console
	// list). Only the ones the current effect needs are read:
	// RESTING reads `from`, TRANSIT reads both, PREVIEW reads `to`.
	LedRange from;
	LedRange to;

	// TRANSIT only. Clamped to [0, 1000].
	int fractionPermille = 0;

	// RESTING and SELECTING. The selected console's own window, as a
	// split point. The three brightness tiers fall out of this plus
	// keepEnd:
	//
	//   [0, consoleStart)          the stack above the selection
	//   [consoleStart, keepEnd)    the selection itself
	//   [keepEnd, totalLeds)       below the selection, dark
	//
	// In the default (inclusive) configuration keepEnd is the console
	// window's end, so the middle band is exactly the console. In the
	// exclusive one keepEnd == consoleStart, the middle band is empty,
	// and the selection goes dark -- which is what
	// LEDSTRING_KEEP_INCLUDES_SELECTED promises.
	int keepEnd = 0;
	int consoleStart = 0;

	// Brightness percentages, all 0..100.
	int abovePct = 0;   // RESTING: the pixels above the selection
	int selfPct = 100;  // RESTING: the selection's own window
	int fromPct = 0;    // TRANSIT: the console being left
	int toPct = 0;      // TRANSIT: the console being approached
	int blobPct = 100;  // TRANSIT: the travelling blob

	// TRANSIT. Clamped to totalLeds.
	int blobWidth = 3;

	// PREVIEW. The glow follows a parabola over the period, so it peaks
	// mid-cycle and falls to pulseMinPct at both ends.
	int pulseMinPct = 0;
	int pulseMaxPct = 100;
	std::uint32_t pulsePeriodMs = 0;

	// PREVIEW and SELECTING: how far into the animation we are.
	std::uint32_t elapsedMs = 0;

	// SELECTING only.
	SelectionEffectConfig select;
};

// Resolve one whole frame into `out[0 .. totalLeds)`, one brightness
// percentage per pixel. `out` is fully overwritten, so callers do not
// need to clear it first.
//
// Zeros the output and returns immediately if `out` is null or
// totalLeds <= 0, so a caller with an uninitialised strip size cannot
// scribble past its buffer.
//
// The layering inside TRANSIT is deliberate and is the order the pixels
// are meant to be read in: departure window, then destination window on
// top of it, then the blob on top of both. A wider destination therefore
// wins over the window it is approaching, and the blob is always the
// brightest thing on the strip while it moves.
void computeStripFrame(const StripFrame& frame, int* out);

}  // namespace retroroom_core
