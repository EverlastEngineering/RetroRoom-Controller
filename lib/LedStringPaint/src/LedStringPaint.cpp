// Functional core for the second-strip LED paint math. See
// lib/LedStringPaint/src/LedStringPaint.h for the design rationale and
// the invariants each function upholds. This translation unit is pure
// C++ -- no Arduino, no FastLED, no globals -- so it compiles and
// unit-tests under [env:test_native] on the host.

#include "LedStringPaint.h"

namespace retroroom_core {

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
	ev.detents = detentProgressPermille(ev.detentsPerStep);
	return ev;
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
	//
	// Which end of the target the run stops short of depends on which
	// way the operator is turning, and getting that wrong is what left
	// a backwards browse with a zero-length run.
	const int leaveEnd = leave.start + leave.width;
	int fillToPixel;
	if (entryPixel > leaveEnd) {
		// Shelf crossing: the block comes in from beyond the target, so
		// the fill spans the whole path it will sweep.
		fillToPixel = entryPixel;
	} else if (target.start >= leaveEnd) {
		fillToPixel = target.start;                    // forwards
	} else {
		fillToPixel = target.start + target.width;     // backwards
	}
	frame.fillFrom = leave;
	frame.fillTo = {fillToPixel, 0};
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

int computeFillEnd(const LedRange& from, const LedRange& to, int minLengthLeds,
                   int totalLeds) {
	if (totalLeds <= 0) {
		return 0;
	}
	const int start = from.start + from.width;
	if (to.start >= start) {
		// Forwards: the run starts at the console being left and stops
		// at the console being reached.
		int end = to.start;
		if (end - start < minLengthLeds) {
			end = start + minLengthLeds;
		}
		if (end > totalLeds) {
			end = totalLeds;
		}
		if (end < start) {
			end = start;
		}
		return end;
	}
	// Backwards: the operator is turning the other way, so the run runs
	// from the target back toward the console being left and the floor
	// extends it *past* the target, not along the gap.
	//
	// This case matters: the browse goes both ways, and treating every
	// step as forwards left a backwards step with a zero-length run, so
	// the progression indicator simply did not appear.
	int end = to.start;
	if (start - end < minLengthLeds) {
		end = start - minLengthLeds;
	}
	if (end < 0) {
		end = 0;
	}
	return end;
}

int computeFillScale(int pixel, const StripFrame& frame) {
	const int start = frame.fillFrom.start + frame.fillFrom.width;
	const int end = computeFillEnd(frame.fillFrom, frame.fillTo,
								  frame.minFillLeds, frame.totalLeds);
	// The run is [lo, hi) whichever way it runs, so the two cases below
	// only differ in which end is the *leading* one.
	const bool forward = (end >= start);
	const int lo = forward ? start : end;
	const int hi = forward ? end : start;
	// The console being left stays lit, dim, throughout.
	if (pixel < lo) {
		return frame.dimPct;
	}
	if (pixel >= hi) {
		return 0;
	}
	// Antialias the leading edge only -- it is the edge the operator is
	// watching move. The trailing edge is a fixed boundary the fill grew
	// out of, and softening it would make the whole run shimmer.
	const long long lead = (forward ? hi : lo) * 1000;
	const long long here = static_cast<long long>(pixel) * 1000;
	if (forward) {
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
	// Then the console left behind, dim for the duration of the travel.
	if (pixel >= frame.travelFrom.start &&
		pixel < frame.travelFrom.start + frame.travelFrom.width) {
		return frame.dimPct;
	}
	// Then the knob-turn fill, which the block consumes as it passes.
	// A pixel the block has already swept over goes dark; one it has not
	// reached yet stays lit.
	//
	// Which side is "not yet" depends on the direction of travel, and a
	// shelf crossing runs *backwards* -- the knob goes one way and the
	// light goes the other. Testing the block's leading edge alone
	// therefore lights the fill on the wrong side for half the cases.
	const int fillStart = frame.travelFrom.start + frame.travelFrom.width;
	const int fillTo = computeFillEnd(frame.travelFrom, frame.fillTo,
									  frame.minFillLeds, frame.totalLeds);
	if (pixel >= fillStart && pixel < fillTo) {
		const bool forward =
			frame.travelToLeftPermille >= frame.travelFromLeftPermille;
		const long long here = static_cast<long long>(pixel) * 1000;
		const bool consumed = forward ? (here < rightPermille)
									  : (here >= leftPermille);
		return consumed ? 0 : frame.fillPct;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Whole-strip frames
// ---------------------------------------------------------------------------

namespace {

void fillWindow(int* out, const LedRange& w, int percent) {
	if (w.width <= 0) {
		return;
	}
	const int end = w.start + w.width;
	for (int i = w.start; i < end; ++i) {
		if (i >= 0) {
			out[i] = percent;
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
void paintRestingInto(const StripFrame& f, int* out) {
	for (int i = 0; i < f.totalLeds; ++i) {
		out[i] = 0;
	}
	// The count is trusted only alongside the pointer. A frame with a
	// count but a null list is a bug in the caller, and reading through
	// it would be an out-of-bounds read rather than an obviously wrong
	// frame -- so degrade to "nothing above" instead.
	if (f.aboveWindows != 0 && f.aboveCount > 0 && f.abovePct > 0) {
		for (int i = 0; i < f.aboveCount; ++i) {
			fillWindow(out, f.aboveWindows[i], f.abovePct);
		}
	}
	// The selection goes on top, and on last: it is the brightest thing
	// on the strip and nothing may paint over it.
	fillWindow(out, f.from, f.selfPct);
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

void computeStripFrame(const StripFrame& frame, int* out) {
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
		out[i] = 0;
	}

	switch (frame.effect) {
	case StripEffect::RESTING:
		paintRestingInto(frame, out);
		break;
	case StripEffect::FILLING: {
		for (int i = 0; i < total; ++i) {
			out[i] = computeFillScale(i, frame);
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
			out[i] = computeTravelScale(i, frame, edges.leftPermille,
										edges.rightPermille);
		}
		break;
	}
	case StripEffect::PREVIEW: {
		const int pulse = computePulseScale(frame.elapsedMs, frame.pulsePeriodMs,
										   frame.pulseMinPct, frame.pulseMaxPct);
		fillWindow(out, frame.to, pulse);
		break;
	}
	case StripEffect::SELECTING: {
		// Build the resting picture and animate toward it. Resolving the
		// target through the same helper RESTING uses is what guarantees
		// the last frame of the effect equals the resting paint; when
		// this was two independent band-splitting expressions they
		// agreed only by hand, and a mismatch showed up as the whole
		// strip dimming at the handoff.
		int target[64];
		paintRestingInto(frame, target);
		for (int i = 0; i < total; ++i) {
			out[i] = computeSelectScale(i, target, total, frame.elapsedMs,
										frame.select);
		}
		break;
	}
	}
}

}  // namespace retroroom_core
