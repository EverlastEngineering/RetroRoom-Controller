// Functional core for the second-strip LED paint math. See
// lib/LedStringPaint/src/LedStringPaint.h for the design rationale and
// the invariants each function upholds. This translation unit is pure
// C++ -- no Arduino, no FastLED, no globals -- so it compiles and
// unit-tests under [env:test_native] on the host.

#include "LedStringPaint.h"

namespace retroroom_core {

const LedColor kLedBlack = {0, 0, 0};

LedRange computeConsoleWindow(int ledPosition, int ledWidth, int totalLeds) {
	// Defensive: a malformed or stale JSON record could leave
	// ledPosition outside [0, totalLeds) or ledWidth <= 0. Returning a
	// zero-width range here is the safest call -- the shell-side
	// paint code paints a "fully off" strip in that case (no lit
	// pixels) rather than lighting an undefined window.
	if (ledWidth <= 0) {
		return {0, 0};
	}
	if (ledPosition < 0) {
		return {0, 0};
	}
	if (ledPosition >= totalLeds) {
		return {0, 0};
	}
	if (totalLeds <= 0) {
		return {0, 0};
	}

	// Clamp width so we don't overflow the strip. A console whose
	// declared window would extend past totalLeds gets its window
	// truncated to the strip boundary rather than wrapped or dropped;
	// this matches the convention in the rest of the codebase
	// (e.g. lib/ConsoleConfig parses led_width as-is but callers
	// must guard their own bounds).
	int maxWidth = totalLeds - ledPosition;
	int width = ledWidth < maxWidth ? ledWidth : maxWidth;

	return {ledPosition, width};
}

// ---------------------------------------------------------------------------
// Rotary detent gating
// ---------------------------------------------------------------------------

DetentGate::DetentGate()
	: fraction_(0), detentRemainder_(0), fastMode_(false),
	  hasLastDetent_(false), lastDetentMs_(0) {}

void DetentGate::configure(const DetentGateConfig& cfg) {
	cfg_ = cfg;
	// Clamp at the edge rather than at every use site: a config with a
	// zero or negative threshold would otherwise make onDetent()'s
	// `1000 / detentsPerStep` divide by zero.
	if (cfg_.detentsPerStep < 1) {
		cfg_.detentsPerStep = 1;
	}
	if (cfg_.fastDetentsPerStep < 1) {
		cfg_.fastDetentsPerStep = 1;
	}
	// "Fast" has to mean fewer detents, not more.
	if (cfg_.fastDetentsPerStep > cfg_.detentsPerStep) {
		cfg_.fastDetentsPerStep = cfg_.detentsPerStep;
	}
	// A config change invalidates any progress measured against the old
	// threshold, so start over rather than let the fraction describe a
	// step the new threshold would express differently.
	reset();
}

void DetentGate::reset() {
	fraction_ = 0;
	detentRemainder_ = 0;
	fastMode_ = false;
	hasLastDetent_ = false;
	lastDetentMs_ = 0;
}

int DetentGate::detentsPerStep() const {
	return fastMode_ ? cfg_.fastDetentsPerStep : cfg_.detentsPerStep;
}

DetentEvent DetentGate::onDetent(int direction, std::uint32_t nowMs,
                                 int listSize, int anchorIndex) {
	DetentEvent ev;
	ev.fractionPermille = fraction_;
	ev.detentsPerStep = detentsPerStep();
	ev.fastMode = fastMode_;
	ev.targetDirection = (fraction_ > 0) ? 1 : ((fraction_ < 0) ? -1 : 0);
	ev.advanced = false;
	ev.frozen = false;
	ev.direction = direction;
	ev.detents = 0;

	if (direction != 1 && direction != -1) {
		// Not a detent. Report the unchanged state so the caller can
		// still repaint the current frame without the gate pretending
		// the operator moved.
		ev.stepPermille = stepProgressPermille(ev.detentsPerStep);
		ev.detents = detentProgressPermille(ev.detentsPerStep);
		return ev;
	}

	// Fast mode is decided per detent from the gap since the previous
	// one, so a spin that accelerates picks it up on the very next
	// detent -- and, just as importantly, *un-picks* it the moment the
	// operator slows back down.
	//
	// This used to latch: once a detent tripped the window, fastMode_
	// stayed true for the rest of the browse and was only cleared by
	// reset(). That made the slow path effectively unreachable, because
	// a single early fast detent -- or a window simply set below the
	// operator's deliberate cadence -- locked in the fast threshold for
	// everything that followed.
	//
	// The signed subtraction is the millis()-wraparound-safe form.
	fastMode_ = cfg_.fastSpinWindowMs > 0 && hasLastDetent_ &&
				(std::uint32_t)(nowMs - lastDetentMs_) < cfg_.fastSpinWindowMs;
	lastDetentMs_ = nowMs;
	hasLastDetent_ = true;

	ev.detentsPerStep = detentsPerStep();
	ev.fastMode = fastMode_;

	// Recompute the target from the *post-update* position... (see the
	// ordering comment further down; the targetDirection that matters
	// here is the one this detent would aim at, before the position
	// moves.)
	const int aimed =
		(fraction_ != 0) ? ((fraction_ > 0) ? 1 : -1) : direction;

	// Is there anywhere to go? The cabinet has physical ends, and
	// turning off one of them reaches nothing. Freeze *before* touching
	// the position, so the progression indicator stops dead rather than
	// filling toward a console that is not there.
	//
	// The detent is still recorded above, so the gap since the last one
	// stays honest: a frozen knob is not a pause in the spin.
	const int target = anchorIndex + aimed;
	if (listSize <= 0 || target < 0 || target >= listSize) {
		ev.frozen = true;
		ev.stepPermille = stepProgressPermille(ev.detentsPerStep);
		ev.detents = detentProgressPermille(ev.detentsPerStep);
		return ev;
	}

	// Advance the position by exactly 1/detentsPerStep of a console
	// step, carrying the division remainder. `fraction_` is continuous
	// in permille, so a spin that speeds up mid-step is already further
	// along than the new (smaller) threshold and completes on the very
	// next detent -- which is the catch-up you want when someone
	// decides to run rather than creep.
	detentRemainder_ += 1000;
	const int delta = detentRemainder_ / ev.detentsPerStep;
	detentRemainder_ %= ev.detentsPerStep;
	fraction_ += direction * delta;

	// Recompute the target from the *post-update* position, not from
	// where we were. Walking back to exactly 0 has to flip the target
	// to the side the operator is now turning, otherwise the blob
	// would keep heading for the console it just came from.
	if (fraction_ >= 1000) {
		fraction_ = 0;
		ev.advanced = true;
		ev.targetDirection = 1;
	} else if (fraction_ <= -1000) {
		fraction_ = 0;
		ev.advanced = true;
		ev.targetDirection = -1;
	} else if (fraction_ > 0) {
		ev.targetDirection = 1;
	} else if (fraction_ < 0) {
		ev.targetDirection = -1;
	} else {
		// Sitting exactly on the anchor. Aim at the side the operator
		// is currently turning, so the first detent of a step travels
		// the right way.
		ev.targetDirection = direction;
	}

	ev.fractionPermille = fraction_;
	ev.stepPermille = stepProgressPermille(ev.detentsPerStep);
	ev.detents = detentProgressPermille(ev.detentsPerStep);
	return ev;
}

int DetentGate::stepProgressPermille(int detentsPerStep) const {
	// The step completes on its last detent, and the indicator that
	// leads up to it is only drawn on the ones before. So the last
	// drawn detent is 100% of the step, not (N-1)/N of it.
	if (detentsPerStep <= 1) {
		return 1000;
	}
	int f = fraction_;
	if (f < 0) {
		f = -f;
	}
	// fractionPermille is already in permille of a *whole* step, so the
	// rescale is a plain ratio: N / (N-1). At five detents per step that
	// turns 800 (the last drawn detent) into 1000 and 200 into 250.
	int p = (f * detentsPerStep) / (detentsPerStep - 1);
	return (p > 1000) ? 1000 : p;
}

int DetentGate::detentProgressPermille(int detentsPerStep) const {
	int f = fraction_;
	if (f < 0) {
		f = -f;
	}
	// Round to nearest so a 5-detent step reports 1..5 rather than
	// 0..4 as it approaches the far end.
	return (f * detentsPerStep + 500) / 1000;
}

// ---------------------------------------------------------------------------
// Blob travel
// ---------------------------------------------------------------------------

int easeInOutPermille(int fractionPermille) {
	if (fractionPermille <= 0) {
		return 0;
	}
	if (fractionPermille >= 1000) {
		return 1000;
	}
	// Smoothstep: 3t^2 - 2t^3 with t = x/1000, rescaled back to
	// permille, i.e. 3x^2/1000 - 2x^3/1e6. Computed in 64-bit because
	// x^3 at x=1000 overflows a 32-bit int once doubled.
	const long x = fractionPermille;
	const long eased = (3 * x * x) / 1000 - (2 * x * x * x) / 1000000;
	return static_cast<int>(eased);
}

LedRange computeBlobWindow(int fromStart, int fromWidth, int toStart,
                           int toWidth, int fractionPermille, int blobWidth,
                           int totalLeds) {
	if (totalLeds <= 0 || blobWidth < 1) {
		return {0, 0};
	}
	// Centre of a window, treating an empty window as sitting on its
	// start pixel so a console with no LED mapping still gives the blob
	// an origin.
	const int fromCentre =
		fromStart + ((fromWidth > 0) ? (fromWidth - 1) / 2 : 0);
	const int toCentre = toStart + ((toWidth > 0) ? (toWidth - 1) / 2 : 0);

	const int eased = easeInOutPermille(fractionPermille);
	const int centre = fromCentre + (toCentre - fromCentre) * eased / 1000;

	// Clamp the band into the strip. A blob wider than the strip is
	// truncated at the boundaries rather than wrapped -- a wrapped blob
	// would read as two blobs.
	int width = blobWidth;
	if (width > totalLeds) {
		width = totalLeds;
	}
	int start = centre - width / 2;
	if (start < 0) {
		start = 0;
	}
	if (start + width > totalLeds) {
		start = totalLeds - width;
	}
	if (start < 0) {
		start = 0;
	}
	return {start, width};
}

// ---------------------------------------------------------------------------
// Preview pulse
// ---------------------------------------------------------------------------

int computePulseScale(std::uint32_t elapsedMs, std::uint32_t periodMs,
                      int minPercent, int maxPercent) {
	if (periodMs == 0) {
		return maxPercent;
	}
	if (maxPercent < minPercent) {
		const int t = minPercent;
		minPercent = maxPercent;
		maxPercent = t;
	}
	// Parabola over one period: peaks at the midpoint, zero at both
	// ends. (2t-1000)^2 / 1000 in permille keeps it integer-only.
	const std::uint32_t phase = elapsedMs % periodMs;
	// Scale the phase to permille of the period. periodMs is a config
	// value in the hundreds-to-thousands, so 64-bit here is just to
	// keep phase*1000 from overflowing for an unusually long period.
	const int phasePermille = static_cast<int>((static_cast<long long>(phase) * 1000ll) / periodMs);
	const int centred = phasePermille * 2 - 1000;
	const int dip = centred * centred / 1000;  // 1000 at the edges, 0 at the peak
	const int level = 1000 - dip;              // 0 at the edges, 1000 at the peak
	return minPercent + (maxPercent - minPercent) * level / 1000;
}

// ---------------------------------------------------------------------------
// Selection effect
// ---------------------------------------------------------------------------

int computeKeepEnd(int ledPosition, int ledWidth, int totalLeds,
                   bool includeSelf) {
	if (totalLeds <= 0 || ledPosition < 0) {
		return 0;
	}
	int end = ledPosition;
	if (includeSelf && ledWidth > 0) {
		end += ledWidth;
	}
	if (end > totalLeds) {
		end = totalLeds;
	}
	if (end < 0) {
		end = 0;
	}
	return end;
}

namespace {

// Avalanche-style 32-bit mix. Cheap, no state, and the same input always
// gives the same output -- which is what lets the host tests assert an
// exact twinkle frame.
std::uint32_t mix32(std::uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

int lerpPercent(int from, int to, int progressPermille) {
	return from + (to - from) * progressPermille / 1000;
}


// Is this pixel inside a console window? Used for the dim/context roles,
// where "which console" matters and a raw range test would be a second
// copy of the same condition to keep in step.
bool inWindow(int pixel, const LedRange& w) {
	return w.width > 0 && pixel >= w.start && pixel < w.start + w.width;
}

}  // namespace

int twinkleSample(int pixel, std::uint32_t tick) {
	const std::uint32_t a = mix32(static_cast<std::uint32_t>(pixel + 1) * 0x9e3779b1u);
	const std::uint32_t b = mix32(tick + 0x51u);
	return static_cast<int>((a ^ b) & 0x3ffu);  // 0..1023
}

int computeSelectScale(int pixel, const int* finalPct, int totalLeds,
                       std::uint32_t elapsedMs,
                       const SelectionEffectConfig& cfg) {
	const int finalScale = (finalPct != 0) ? finalPct[pixel] : 0;
	if (cfg.totalMs == 0 || elapsedMs >= cfg.totalMs) {
		return finalScale;
	}

	// The twinkle is sampled on a fixed tick rather than per millisecond
	// so it reads as a sparkle instead of a smooth shimmer.
	const std::uint32_t twinkleTickMs =
		(cfg.twinkleMs / 8) > 0 ? (cfg.twinkleMs / 8) : 1;

	if (elapsedMs < cfg.twinkleMs) {
		const int sample = twinkleSample(pixel, elapsedMs / twinkleTickMs);
		return lerpPercent(cfg.twinkleMin, cfg.twinkleMax, sample * 1000 / 1023);
	}

	// Settle. The whole stagger span is subtracted from the ramp so the
	// effect still finishes inside totalMs no matter how large
	// staggerMs is -- raising the stagger trades ripple length for
	// ramp length rather than stretching past the budget.
	const std::uint32_t settleSpan = cfg.totalMs - cfg.twinkleMs;
	const std::uint32_t staggerSpan =
		(totalLeds > 0)
			? static_cast<std::uint32_t>(totalLeds - 1) * cfg.staggerMs
			: 0;
	const std::uint32_t rampMs = (settleSpan > staggerSpan)
		? (settleSpan - staggerSpan)
		: 1;
	const std::uint32_t delay =
		(totalLeds > 0) ? static_cast<std::uint32_t>(pixel) * cfg.staggerMs : 0;
	const std::uint32_t t = elapsedMs - cfg.twinkleMs;

	if (t <= delay) {
		// Still holding the twinkle's last look for this pixel. Sample
		// the twinkle at the moment the ramp would have started so the
		// hand-off is continuous.
		const int sample = twinkleSample(pixel, cfg.twinkleMs / twinkleTickMs);
		return lerpPercent(cfg.twinkleMin, cfg.twinkleMax, sample * 1000 / 1023);
	}
	if (t >= delay + rampMs) {
		return finalScale;
	}
	const std::uint32_t into = t - delay;
	const int sample = twinkleSample(pixel, cfg.twinkleMs / twinkleTickMs);
	const int start = lerpPercent(cfg.twinkleMin, cfg.twinkleMax, sample * 1000 / 1023);
	return lerpPercent(start, finalScale, static_cast<int>(into) * 1000 / static_cast<int>(rampMs));
}

// ---------------------------------------------------------------------------
// Browse: the knob-turn fill and the scripted travel
// ---------------------------------------------------------------------------

namespace {

// Ease-out cubic, permille in, permille out. The leading edge: fast off
// the mark, decelerating into the target so the arrival reads as an
// arrival rather than a stop.
int easeOutPermille(int t) {
	if (t <= 0) {
		return 0;
	}
	if (t >= 1000) {
		return 1000;
	}
	const long x = t;
	// 1000 * (1 - (1-t)^3) = 1000 - 1000*(1-t)^3
	const long inv = 1000 - x;
	return static_cast<int>(1000 - (inv * inv * inv) / 1000000);
}

// Smoothstep, permille in, permille out. The trailing edge: eases out
// of the start so the block does not snap away from the console, then
// settles so it does not snap onto the target.
int easeInOutPermillePermille(int t) {
	if (t <= 0) {
		return 0;
	}
	if (t >= 1000) {
		return 1000;
	}
	const long x = t;
	return static_cast<int>((3 * x * x) / 1000 - (2 * x * x * x) / 1000000);
}

int lerpPermille(int from, int to, int eased) {
	return from + ((to - from) * eased) / 1000;
}

}  // namespace

TravelEdges travelEdges(int fromLeftPermille, int fromRightPermille,
                        int toLeftPermille, int toRightPermille,
                        int progressPermille, int peakWidthPermille) {
	TravelEdges e;
	if (progressPermille < 0) {
		progressPermille = 0;
	}
	if (progressPermille > 1000) {
		progressPermille = 1000;
	}
	// The *leading* edge -- the one moving into new territory -- runs
	// ease-out, so the block decelerates into its target and the arrival
	// reads as an arrival. The trailing edge runs smoothstep, easing out
	// of the start and settling so it never snaps. The two differ, so the
	// gap between them opens and then closes: that opening is the
	// stretch.
	//
	// Which of the two is leading depends on the direction of travel. A
	// shelf crossing runs backwards -- the operator's knob goes one way
	// and the light goes the other -- and using the right-hand edge's
	// easing unconditionally inverted the block for exactly those moves.
	const bool forward = toLeftPermille >= fromLeftPermille;
	int right = forward
					? lerpPermille(fromRightPermille, toRightPermille,
								   easeOutPermille(progressPermille))
					: lerpPermille(fromRightPermille, toRightPermille,
								   easeInOutPermillePermille(progressPermille));
	int left = forward
				   ? lerpPermille(fromLeftPermille, toLeftPermille,
								  easeInOutPermillePermille(progressPermille))
				   : lerpPermille(fromLeftPermille, toLeftPermille,
								  easeOutPermille(progressPermille));

	// Bound the stretch. Without this the two edges can cross on a long
	// travel and the block inverts, which reads as a glitch. The peak is
	// taken as a minimum width rather than as a cap on the position, so
	// the block still ends exactly on the target window.
	if (peakWidthPermille > 0 && (right - left) > peakWidthPermille) {
		left = right - peakWidthPermille;
	}
	if (left > right) {
		left = right;
	}
	e.leftPermille = left;
	e.rightPermille = right;
	e.widthPermille = right - left;
	return e;
}

int travelEntryFor(const LedRange& leave, const LedRange& target) {
	// Whichever edge of the console being left the block travels away
	// from. Going forward that is the trailing edge; coming back it is
	// the leading one.
	//
	// The block's spark is centred on this pixel, so it straddles the
	// edge either way: one LED of the console being left and one LED of
	// the gap it is heading into. That is what makes it read as peeling
	// off rather than appearing from nowhere -- and only if the edge is
	// the one the block is actually leaving by.
	if (target.start < leave.start) {
		return leave.start;
	}
	return leave.start + leave.width;
}

void computeTravelPath(const LedRange& leave, int entryPixel,
                       const LedRange& target, int sparkLeds, StripFrame& frame) {
	frame.travelFrom = leave;
	// The block starts as a `sparkLeds`-wide spark sitting on the pixel
	// it enters at, so it reads as peeling off rather than appearing
	// from nowhere. Two is enough to read as an object; one reads as a
	// stray pixel.
	const int spark = (sparkLeds < 1) ? 1 : sparkLeds;
	const int fromLeft = entryPixel - spark / 2;
	frame.travelFromLeftPermille = fromLeft * 1000;
	frame.travelFromRightPermille = (fromLeft + spark) * 1000;
	// End: exactly the target window, so the last frame of the travel IS
	// the preview and the block never has to "correct" on arrival.
	frame.travelToLeftPermille = target.start * 1000;
	frame.travelToRightPermille = (target.start + target.width) * 1000;
	// The fill covers the path the block will sweep, and never the
	// target's own window -- that is where the block lands, and lighting
	// it dim in the meantime would make the arrival mean nothing.
	// computeFillGeometry() works out where the run starts and ends from
	// the same two windows and the same entry pixel, so that decision
	// lives in exactly one place. An earlier version of this function
	// also derived a `fillToPixel` here, and it was dead: the geometry
	// call below recomputed the same thing. It was tempting to fix the
	// backwards crossing in both places, and the two would have drifted.
	//
	// The strip size and the floor come from the frame the caller
	// already assembled, so the fill and the block are resolved
	// against the same numbers. Passing zeroes here clamped every
	// run to nothing.
	computeFillGeometry(leave, entryPixel, target, frame.minFillLeds,
	                   frame.totalLeds, frame);
}

int coveragePercent(int leftPermille, int rightPermille, int pixel) {
	// Exact overlap of [left, right) with the LED's one-unit pitch
	// [pixel, pixel+1), as a percentage. All in permille, so the working
	// is in permille of a LED and the result scales to 0..100 at the
	// end.
	const long long l = leftPermille;
	const long long r = rightPermille;
	const long long p0 = static_cast<long long>(pixel) * 1000;
	const long long p1 = p0 + 1000;
	const long long lo = l > p0 ? l : p0;
	const long long hi = r < p1 ? r : p1;
	if (hi <= lo) {
		return 0;
	}
	// (overlap in permille) / 1000 gives the fraction of a pitch, so the
	// percentage is overlap * 100 / 1000 = overlap / 10.
	return static_cast<int>((hi - lo) / 10);
}

void computeFillGeometry(const LedRange& leave, int entryPixel,
                         const LedRange& target, int minLengthLeds,
                         int totalLeds, StripFrame& frame) {
	// The run is the GAP between the two console windows, with its
	// anchor on the side of the console being left and its leading edge
	// starting at the far side of the gap.
	//
	// Both of those details were wrong before, and in the same way: the
	// run was measured from the source's *trailing* edge in both
	// directions, so a backwards step's run lay inside the console
	// being left instead of in the gap. The fill then appeared on the
	// wrong side of the console and grew the wrong way -- the operator
	// turned left and the indicator crept right, towards the console
	// they were turning away from.
	// The run is the path the *block* sweeps, minus the target's own
	// window, and the anchor is the end the block starts from. Tying it
	// to the block rather than to the gap between the two consoles is
	// what makes a shelf crossing fill its whole sweep: there the block
	// starts at the far end of the destination shelf, so the run does
	// too, and a run sized to the two-pixel gap would have left the
	// block sliding over a backdrop.
	const int leaveEnd = leave.start + leave.width;
	const int targetEnd = target.start + target.width;
	// The stretch of pixels the two consoles occupy together. A step
	// *within* a shelf always enters inside it -- at the source's
	// trailing edge, which is between the two windows however the
	// operator is turning.
	//
	// An entry outside that stretch is a step that crossed the bridge
	// between shelves. There the entry is over on the far side of the
	// target, so the run is not the gap between the two windows: it is
	// the whole destination shelf, from the end the block entered at to
	// the target.
	//
	// Going back over the bridge is the case that was missing, and it
	// is the one the operator meets on the way home. The entry lands
	// *before* the target, which is not "between the two consoles" by
	// any reading, so it fell through to the same-shelf backwards
	// branch -- where the run is the gap between the windows. Across a
	// bridge that gap is two pixels, so a step that had just sent the
	// light the width of a shelf in the other direction came back as a
	// one-pixel nudge.
	const int spanLo = (leave.start < target.start) ? leave.start : target.start;
	const int spanHi = (leaveEnd > targetEnd) ? leaveEnd : targetEnd;
	const bool fromOutside = (entryPixel < spanLo || entryPixel > spanHi);
	int anchor;
	int lead;
	if (fromOutside) {
		anchor = entryPixel;                  // the far end of the shelf
		lead = target.start;                  // where it lands
	} else if (target.start >= leaveEnd) {
		anchor = leaveEnd;                    // just past the source
		lead = target.start;                  // just short of the target
	} else {
		anchor = leave.start;                 // just short of the source
		lead = target.start + target.width;   // just past the target
	}
	const bool forward = (lead >= anchor);
	// The floor, extending the run *away* from the anchor. Without it a
	// step between shelves -- a couple of pixels in index space and a
	// long way round the cabinet -- leaves the progression indicator
	// with nothing to show.
	if (forward) {
		if (lead - anchor < minLengthLeds) {
			lead = anchor + minLengthLeds;
		}
		if (lead > totalLeds) {
			lead = totalLeds;
		}
		if (lead < anchor) {
			lead = anchor;
		}
	} else {
		if (anchor - lead < minLengthLeds) {
			lead = anchor - minLengthLeds;
		}
		if (lead < 0) {
			lead = 0;
		}
		if (lead > anchor) {
			lead = anchor;
		}
	}
	frame.fillAnchor = anchor;
	frame.fillLead = lead;
	frame.fillForward = forward;
	frame.fillFrom = leave;
	frame.fillTo = target;
}

int scaleFillLead(int anchor, int lead, bool forward, int progressPermille) {
	// The shell calls this to walk the leading edge across the gap as
	// the operator turns. Kept in the core so the shell, the simulator
	// and the tests all do it the same way.
	// Rounded, not truncated. A three-LED gap over four detents is most
	// of a LED on the first click, and truncating that to nothing left
	// the operator turning a detent with no visible response at all.
	if (forward) {
		return anchor + ((lead - anchor) * progressPermille + 500) / 1000;
	}
	return anchor - ((anchor - lead) * progressPermille + 500) / 1000;
}

int computeFillScale(int pixel, const StripFrame& frame) {
	// Only the selected console is dimmed, and only where nothing else
	// has claimed the pixel. Dimming "everything below the run" instead
	// lit up bare gap pixels at the start of the strip, which read as
	// the fill starting from LED 0 rather than from the console the
	// operator is turning away from.
	if (inWindow(pixel, frame.activeWindow) &&
		!inWindow(pixel, frame.candidateWindow)) {
		return frame.dimPct;
	}
	const int lo = (frame.fillLead < frame.fillAnchor) ? frame.fillLead
													  : frame.fillAnchor;
	const int hi = (frame.fillLead < frame.fillAnchor) ? frame.fillAnchor
													  : frame.fillLead;
	if (pixel < lo || pixel >= hi) {
		return 0;
	}
	// Antialias the leading edge only -- it is the edge the operator is
	// watching move. The anchor end is fixed, and softening it would
	// make the whole run shimmer.
	const long long lead = static_cast<long long>(frame.fillLead) * 1000;
	const long long here = static_cast<long long>(pixel) * 1000;
	if (frame.fillForward) {
		if (here + 1000 <= lead) {
			return frame.fillPct;
		}
		if (here >= lead) {
			return 0;
		}
		return (frame.fillPct * static_cast<int>(lead - here)) / 1000;
	}
	if (here >= lead) {
		return frame.fillPct;
	}
	if (here + 1000 <= lead) {
		return 0;
	}
	return (frame.fillPct * static_cast<int>(1000 - (lead - here))) / 1000;
}

int computeTravelScale(int pixel, const StripFrame& frame, int leftPermille,
                       int rightPermille) {
	// The block first, over everything. It is the thing the operator is
	// watching, and it is meant to look like it is leaving the console
	// behind it -- so it must be able to overlap the console it started
	// on without being painted out by it.
	const int covered = coveragePercent(leftPermille, rightPermille, pixel);
	if (covered > 0) {
		return (frame.travelPct * covered) / 100;
	}
	// Then the selected console, dim for the duration of the travel.
	if (inWindow(pixel, frame.activeWindow) &&
		!inWindow(pixel, frame.candidateWindow)) {
		return frame.dimPct;
	}
	(void)leftPermille;
	// Then the knob-turn fill, which the block consumes as it passes.
	// A pixel the block has already swept over goes dark; one it has not
	// reached yet stays lit.
	//
	// Which side is "not yet" depends on the direction of travel, and a
	// shelf crossing runs *backwards* -- the knob goes one way and the
	// light goes the other. Testing the block's leading edge alone
	// therefore lights the fill on the wrong side for half the cases.
	const int fillLo = (frame.fillLead < frame.fillAnchor) ? frame.fillLead
														   : frame.fillAnchor;
	const int fillHi = (frame.fillLead < frame.fillAnchor) ? frame.fillAnchor
														   : frame.fillLead;
	if (pixel >= fillLo && pixel < fillHi) {
		const bool forward =
			frame.travelToLeftPermille >= frame.travelFromLeftPermille;
		const long long here = static_cast<long long>(pixel) * 1000;
		const bool consumed = forward ? (here < rightPermille)
									  : (here >= leftPermille);
		return consumed ? 0 : frame.fillPct;
	}
	return 0;
}

StripPixel computeSelectPixel(int pixel, const StripPixel* finalPixels,
                              int totalLeds, std::uint32_t elapsedMs,
                              const SelectionEffectConfig& cfg) {
	// computeSelectScale() wants the settle target as bare levels, so
	// flatten a copy rather than change its signature -- it is also
	// called directly by the tests, and keeping it level-only keeps
	// that honest.
	int levels[64];
	for (int i = 0; i < totalLeds && i < 64; ++i) {
		levels[i] = (finalPixels != 0) ? finalPixels[i].level : 0;
	}
	StripPixel p;
	p.role = (finalPixels != 0) ? finalPixels[pixel].role : LedRole::OFF;
	p.level = computeSelectScale(pixel, levels, totalLeds, elapsedMs, cfg);
	if (p.level <= 0) {
		p.role = LedRole::OFF;
	}
	return p;
}

// ---------------------------------------------------------------------------
// Whole-strip frames
// ---------------------------------------------------------------------------

namespace {

void fillWindow(StripPixel* out, const LedRange& w, LedRole role, int level) {
	if (w.width <= 0) {
		return;
	}
	const int end = w.start + w.width;
	for (int i = w.start; i < end; ++i) {
		if (i >= 0) {
			out[i].role = role;
			out[i].level = level;
		}
	}
}

// The resting picture: the consoles above, dim, then the console itself
// bright. This is the single definition of what the strip looks like
// when nothing is happening -- RESTING renders it directly, and
// SELECTING animates toward it. One function, so the two cannot drift.
//
// Clears `out` itself rather than assuming it is already clear,
// because SELECTING calls it on a scratch buffer.
void paintRestingInto(const StripFrame& f, StripPixel* out) {
	for (int i = 0; i < f.totalLeds; ++i) {
		out[i].role = LedRole::OFF;
		out[i].level = 0;
	}
	// The count is trusted only alongside the pointer. A frame with a
	// count but a null list is a bug in the caller, and reading through
	// it would be an out-of-bounds read rather than an obviously wrong
	// frame -- so degrade to "nothing above" instead.
	if (f.aboveWindows != 0 && f.aboveCount > 0 && f.abovePct > 0) {
		for (int i = 0; i < f.aboveCount; ++i) {
			fillWindow(out, f.aboveWindows[i], LedRole::STACK, f.abovePct);
		}
	}
	// The selection goes on top, and on last: it is the brightest thing
	// on the strip and nothing may paint over it.
	fillWindow(out, f.from, LedRole::SELECTED, f.selfPct);
}

}  // namespace

int collectAboveWindows(const LedRange* windows, int count,
                        int selectedStart, LedRange* out,
                        int outCapacity) {
	if (windows == 0 || out == 0 || count <= 0 || outCapacity <= 0) {
		return 0;
	}
	int written = 0;
	for (int i = 0; i < count && written < outCapacity; ++i) {
		// Wholly above, or not at all. A window that straddles the
		// selection's start is left out rather than clipped, so a
		// hand-edited config with overlapping windows shows one console
		// dark instead of painting pixels twice.
		if (windows[i].width > 0 &&
			windows[i].start + windows[i].width <= selectedStart) {
			out[written++] = windows[i];
		}
	}
	return written;
}

LedColor resolvePixel(const StripFrame& frame, const StripPixel& pixel) {
	if (pixel.role == LedRole::OFF || pixel.level <= 0) {
		return kLedBlack;
	}
	const LedColor base = frame.palette.colors[static_cast<int>(pixel.role)];
	// Clamped rather than allowed to wrap: an over-100 level would
	// otherwise roll a channel over into a different primary and a
	// brightness tweak would silently become a colour change.
	int pct = pixel.level;
	if (pct > 100) {
		pct = 100;
	}
	LedColor c;
	c.r = (base.r * pct) / 100;
	c.g = (base.g * pct) / 100;
	c.b = (base.b * pct) / 100;
	return c;
}

void computeStripFrame(const StripFrame& frame, StripPixel* out) {
	if (out == 0) {
		return;
	}
	const int total = frame.totalLeds;
	if (total <= 0) {
		return;
	}
	// Clear first, every effect. The caller hands back the same buffer
	// each tick, so any pixel the current effect does not touch has to
	// be zeroed or the previous frame ghosts through. TRANSIT and
	// PREVIEW only ever write their own windows and rely on this.
	for (int i = 0; i < total; ++i) {
		out[i].role = LedRole::OFF;
		out[i].level = 0;
	}

	switch (frame.effect) {
	case StripEffect::RESTING:
		paintRestingInto(frame, out);
		break;
	case StripEffect::FILLING: {
		// Three things at once: the console being played (dim), the
		// console a press would select right now (pulsing, and still
		// pulsing even though the operator has started turning toward
		// the next one), and the fill running on past it.
		fillWindow(out, frame.activeWindow, LedRole::LEAVING, frame.dimPct);
		const int pulse = computePulseScale(frame.elapsedMs, frame.pulsePeriodMs,
										   frame.pulseMinPct, frame.pulseMaxPct);
		fillWindow(out, frame.candidateWindow, LedRole::PROPOSAL, pulse);
		for (int i = 0; i < total; ++i) {
			const int level = computeFillScale(i, frame);
			// The fill goes on last: it is the thing that is moving, and
			// nothing may paint over it. Pixels already claimed by the
			// two context roles keep their role, so a fill that runs
			// across the candidate does not repaint it.
			if (out[i].role == LedRole::OFF) {
				out[i].role = (level <= 0) ? LedRole::OFF : LedRole::FILL;
				out[i].level = level;
			}
		}
		break;
	}
	case StripEffect::TRAVEL: {
		int progress = 1000;
		if (frame.travelMs > 0) {
			progress = static_cast<int>((static_cast<long long>(frame.elapsedMs) *
										 1000) /
										frame.travelMs);
			if (progress > 1000) {
				progress = 1000;
			}
		}
		const TravelEdges edges = travelEdges(
			frame.travelFromLeftPermille, frame.travelFromRightPermille,
			frame.travelToLeftPermille, frame.travelToRightPermille, progress,
			frame.travelPeakWidth * 1000);
		for (int i = 0; i < total; ++i) {
			const int level = computeTravelScale(i, frame, edges.leftPermille,
												edges.rightPermille);
			// The block, the console left behind and the fill it is
			// consuming are three different things, and at the same
			// level they are three identical pixels. The roles are what
			// let them be told apart.
			const bool onBlock =
				coveragePercent(edges.leftPermille, edges.rightPermille, i) > 0;
			const bool leaving = inWindow(i, frame.activeWindow) &&
								 !inWindow(i, frame.candidateWindow);
			out[i].role = onBlock ? LedRole::TRAVEL
								  : (leaving ? LedRole::LEAVING
											 : ((level <= 0) ? LedRole::OFF
																: LedRole::FILL));
			out[i].level = level;
		}
		break;
	}
	case StripEffect::PREVIEW: {
		// Only the selected console stays lit behind the proposal. It is
		// what the proposal is a proposal *for*, and it is the one thing
		// the operator is comparing against.
		fillWindow(out, frame.activeWindow, LedRole::LEAVING, frame.dimPct);
		const int pulse = computePulseScale(frame.elapsedMs, frame.pulsePeriodMs,
										   frame.pulseMinPct, frame.pulseMaxPct);
		fillWindow(out, frame.to, LedRole::PROPOSAL, pulse);
		break;
	}
	case StripEffect::SELECTING: {
		// Build the resting picture and animate toward it. Resolving the
		// target through the same helper RESTING uses is what guarantees
		// the last frame of the effect equals the resting paint; when
		// this was two independent band-splitting expressions they
		// agreed only by hand, and a mismatch showed up as the whole
		// strip dimming at the handoff.
		StripPixel target[64];
		paintRestingInto(frame, target);
		for (int i = 0; i < total; ++i) {
			out[i] = computeSelectPixel(i, target, total, frame.elapsedMs,
										 frame.select);
		}
		break;
	}
	}
}

}  // namespace retroroom_core
