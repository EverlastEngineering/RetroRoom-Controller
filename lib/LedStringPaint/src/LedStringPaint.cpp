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

DetentEvent DetentGate::onDetent(int direction, std::uint32_t nowMs) {
	DetentEvent ev;
	ev.fractionPermille = fraction_;
	ev.detentsPerStep = detentsPerStep();
	ev.fastMode = fastMode_;
	ev.targetDirection = (fraction_ > 0) ? 1 : ((fraction_ < 0) ? -1 : 0);
	ev.advanced = false;
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
	// detent. The signed subtraction is the millis()-wraparound-safe
	// form.
	if (cfg_.fastSpinWindowMs > 0 && hasLastDetent_ &&
		(std::uint32_t)(nowMs - lastDetentMs_) < cfg_.fastSpinWindowMs) {
		fastMode_ = true;
	}
	lastDetentMs_ = nowMs;
	hasLastDetent_ = true;

	ev.detentsPerStep = detentsPerStep();
	ev.fastMode = fastMode_;

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

int computeSelectScale(int pixel, int keepEnd, int totalLeds,
                       std::uint32_t elapsedMs,
                       const SelectionEffectConfig& cfg) {
	const int finalScale = (pixel < keepEnd) ? cfg.keepScale : 0;
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

}  // namespace retroroom_core
