// Host-side Unity tests for the browse/selection animation math added to
// lib/LedStringPaint.
//
// The shell that draws these frames is src/ledstring.cpp, which needs
// FastLED + Arduino and so cannot run on the host. Everything these
// tests cover is the *decision* half -- how many detents a console step
// takes, where the blob sits, how bright the pulse is, and which pixels
// survive the selection collapse. The shell's only remaining job is to
// copy those numbers into CRGB pixels.
//
// The behavioural contract being pinned down here:
//
//   DetentGate  -- N detents per console, escalating to M when the
//                  operator spins, reversible mid-transit, and exact
//                  for thresholds that don't divide 1000.
//   Blob        -- starts on the anchor, lands centred on the target,
//                  and never leaves the strip.
//   Pulse       -- stays inside its configured min/max.
//   Selection   -- always finished inside totalMs, and always lands on
//                  the same settled picture regardless of when the
//                  last frame was sampled.

#include <cstdlib>
#include <LedStringPaint.h>
#include <unity.h>
#include "configuration.h"

using retroroom_core::computeBlobWindow;
using retroroom_core::computeKeepEnd;
using retroroom_core::computePulseScale;
using retroroom_core::computeSelectScale;
using retroroom_core::DetentGate;
using retroroom_core::DetentGateConfig;
using retroroom_core::easeInOutPermille;
using retroroom_core::LedRange;
using retroroom_core::SelectionEffectConfig;
using retroroom_core::twinkleLevel;
using retroroom_core::twinkleSample;
using retroroom_core::resolvePixel;
using retroroom_core::LedColor;
using retroroom_core::StripPixel;
using retroroom_core::travelEdges;
using retroroom_core::collectAboveWindows;
using retroroom_core::computeTravelPath;
using retroroom_core::computeFillGeometry;
using retroroom_core::scaleFillLead;
using retroroom_core::travelEntryFor;
using retroroom_core::coveragePercent;
using retroroom_core::travelEdges;
using retroroom_core::TravelEdges;
using retroroom_core::computeConsoleWindow;
using retroroom_core::computeStripFrame;
using retroroom_core::StripEffect;
using retroroom_core::StripFrame;

void setUp(void) {}
void tearDown(void) {}

// Role per pixel, for the tests that care what a pixel is *for* rather
// than how bright. Defined further down with the colour tests, so
// declared here for the tests that need a role and a level together.
static void frameRoles(const StripFrame& f, retroroom_core::LedRole* out);

// Brightness per pixel. The core resolves a frame into a role and a
// level per pixel (see computeStripFrame); most of these tests only
// care about how bright, so this drops the role. Tests that care about
// the role render it directly.
static void frameLevels(const StripFrame& f, int* out, int count = 64) {
	// Zero-initialised: a frame with a smaller totalLeds than the
	// buffer leaves the tail untouched, and reading it would be
	// undefined rather than a useful zero.
	retroroom_core::StripPixel px[64] = {};
	computeStripFrame(f, px);
	// `count` is explicit because some of these tests deliberately hand
	// in a short buffer to prove the core does not scribble past the
	// strip it was told about.
	for (int i = 0; i < count; ++i) {
		out[i] = px[i].level;
	}
}

// ---------------------------------------------------------------------------
// DetentGate
// ---------------------------------------------------------------------------

// The list the gate is browsing in the tests that do not care about
// the ends of it: long enough that a handful of detents in either
// direction never reaches an end, so the freeze path is exercised on
// its own terms further down rather than by accident here.
static const int kListSize = 64;
static const int kAnchor = 32;

// Two seconds between detents is well outside any fast-spin window, so
// these tests run in "deliberate" territory until they say otherwise.
// Slower detents are spelled out per test.
static const std::uint32_t kSlowGapMs = 2000;

static void test_gate_needs_five_detents_for_a_deliberate_step(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));

	for (int i = 1; i <= 4; ++i) {
		retroroom_core::DetentEvent ev = gate.onDetent(1, i * kSlowGapMs, kListSize, kAnchor);
		TEST_ASSERT_FALSE_MESSAGE(ev.advanced, "advanced before the 5th detent");
		TEST_ASSERT_EQUAL(i, ev.detents);
		TEST_ASSERT_FALSE(ev.fastMode);
		TEST_ASSERT_EQUAL(5, ev.detentsPerStep);
	}
	retroroom_core::DetentEvent ev = gate.onDetent(1, 5 * kSlowGapMs, kListSize, kAnchor);
	TEST_ASSERT_TRUE_MESSAGE(ev.advanced, "5th detent must complete the step");
	TEST_ASSERT_EQUAL(1, ev.targetDirection);
	TEST_ASSERT_EQUAL(0, ev.fractionPermille);
}


static void test_gate_snaps_back_to_the_anchor_after_advancing(void) {
	// After a step completes the position is back at the anchor, and
	// the next step starts from there rather than from where the last
	// one finished.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 5; ++i) {
		t += kSlowGapMs;
		gate.onDetent(1, t, kListSize, kAnchor);
	}
	TEST_ASSERT_EQUAL(0, gate.fractionPermille());
	t += kSlowGapMs;
	gate.onDetent(1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(200, gate.fractionPermille());
}

static void test_gate_escalates_to_two_detents_when_spinning(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));

	uint32_t t = 0;
	// Deliberate first, so the first detent can't self-trigger fast mode.
	t += kSlowGapMs;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t, kListSize, kAnchor);
	TEST_ASSERT_FALSE(ev.fastMode);

	// Now spin: 20 ms apart, well inside the window.
	for (int i = 0; i < 4; ++i) {
		t += 20;
		ev = gate.onDetent(1, t, kListSize, kAnchor);
	}
	TEST_ASSERT_TRUE_MESSAGE(ev.fastMode, "fast mode must engage on a fast detent");
	TEST_ASSERT_EQUAL(2, ev.detentsPerStep);
}

static void test_fast_spin_completes_a_step_in_two_detents(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));

	// Deliberate detent to prime fast mode, then a fast run. The slow
	// detent put us at 1/5; the first fast detent adds 1/2 which carries
	// the position over the line, so this step lands on the second fast
	// detent at the latest.
	uint32_t t = kSlowGapMs;
	gate.onDetent(1, t, kListSize, kAnchor);
	t += 20;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t, kListSize, kAnchor);
	if (!ev.advanced) {
		t += 20;
		ev = gate.onDetent(1, t, kListSize, kAnchor);
	}
	TEST_ASSERT_TRUE_MESSAGE(ev.advanced, "fast spin must not need 5 detents");
}

static void test_slowing_back_down_returns_to_the_deliberate_cadence(void) {
	// Fast mode used to latch: one fast detent put the whole browse
	// into the fast threshold until something called reset(). On the
	// bench that made the slow path effectively unreachable, because the
	// first detent after any pause is never "fast" and the second
	// almost always was. The escalation now reflects the gap before
	// *this* detent, so slowing down buys the slow cadence back
	// immediately.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));

	uint32_t t = 0;
	t += 20;
	gate.onDetent(1, t, kListSize, kAnchor);
	t += 20;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t, kListSize, kAnchor);
	TEST_ASSERT_TRUE_MESSAGE(ev.fastMode, "a tight gap must read as fast");

	// Now turn deliberately.
	t += 2000;
	ev = gate.onDetent(1, t, kListSize, kAnchor);
	TEST_ASSERT_FALSE_MESSAGE(ev.fastMode,
		"slowing back down must return to the deliberate threshold");
	TEST_ASSERT_EQUAL(5, ev.detentsPerStep);
}

static void test_zero_window_never_escalates(void) {
	// LEDSTRING_FAST_SPIN_WINDOW_MS = 0 is the documented "off" switch
	// and is the current setting. Whatever the cadence, the gate must
	// stay on the deliberate threshold -- that is the whole point of
	// being able to test the slow path.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 0));
	uint32_t t = 0;
	for (int i = 0; i < 20; ++i) {
		t += 5;  // absurdly fast
		const retroroom_core::DetentEvent ev = gate.onDetent(1, t, kListSize, kAnchor);
		TEST_ASSERT_FALSE(ev.fastMode);
		TEST_ASSERT_EQUAL(5, ev.detentsPerStep);
	}
	// And the fast threshold must actually be unreachable: four detents
	// must not have completed a step.
	DetentGate slow;
	slow.configure(DetentGateConfig(5, 2, 0));
	t = 0;
	for (int i = 0; i < 4; ++i) {
		t += 5;
		TEST_ASSERT_FALSE(slow.onDetent(1, t, kListSize, kAnchor).advanced);
	}
	t += 5;
	TEST_ASSERT_TRUE(slow.onDetent(1, t, kListSize, kAnchor).advanced);
}

static void test_reversing_walks_the_position_back(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 3; ++i) {
		t += kSlowGapMs;
		gate.onDetent(1, t, kListSize, kAnchor);
	}
	const int forward = gate.fractionPermille();
	TEST_ASSERT_TRUE(forward > 0);

	// Turn the knob back. The blob must retreat along the same path
	// toward the same side, not jump to the other console.
	t += kSlowGapMs;
	retroroom_core::DetentEvent ev = gate.onDetent(-1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(forward - 200, ev.fractionPermille);
	TEST_ASSERT_FALSE(ev.advanced);
	TEST_ASSERT_EQUAL_MESSAGE(1, ev.targetDirection,
		"still out on the forward side, so that is still the target");
}

static void test_crossing_the_anchor_flips_the_target_side(void) {
	// Written out detent by detent rather than with a loop, because the
	// whole point is the exact position at each step.
	//   detent 1 (+1) ->  200 permille, heading forward
	//   detent 2 (+1) ->  400 permille, heading forward
	//   detent 3 (-1) ->  200 permille, STILL the forward target
	//   detent 4 (-1) ->    0 permille, on the anchor, now heading back
	//   detent 5 (-1) -> -200 permille, heading backward
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	retroroom_core::DetentEvent ev;

	t += kSlowGapMs;
	ev = gate.onDetent(1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(200, ev.fractionPermille);
	TEST_ASSERT_EQUAL(1, ev.targetDirection);

	t += kSlowGapMs;
	ev = gate.onDetent(1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(400, ev.fractionPermille);
	TEST_ASSERT_EQUAL(1, ev.targetDirection);

	t += kSlowGapMs;
	ev = gate.onDetent(-1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(200, ev.fractionPermille);
	TEST_ASSERT_EQUAL_MESSAGE(1, ev.targetDirection, "still short of the anchor");

	t += kSlowGapMs;
	ev = gate.onDetent(-1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(0, ev.fractionPermille);
	TEST_ASSERT_EQUAL_MESSAGE(-1, ev.targetDirection,
		"back on the anchor and turning back, so the target flips sides");

	t += kSlowGapMs;
	ev = gate.onDetent(-1, t, kListSize, kAnchor);
	TEST_ASSERT_EQUAL(-200, ev.fractionPermille);
	TEST_ASSERT_EQUAL(-1, ev.targetDirection);
}

static void test_backward_step_completes_and_reports_direction(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	retroroom_core::DetentEvent ev;
	for (int i = 0; i < 5; ++i) {
		t += kSlowGapMs;
		ev = gate.onDetent(-1, t, kListSize, kAnchor);
	}
	TEST_ASSERT_TRUE(ev.advanced);
	TEST_ASSERT_EQUAL(-1, ev.targetDirection);
	TEST_ASSERT_EQUAL(-1, ev.direction);
}

static void test_thresholds_that_do_not_divide_1000_still_arrive(void) {
	// 3 and 7 are the interesting cases: 1000/3 truncates to 333 and
	// 1000/7 to 142, so a naive accumulator never reaches the step.
	for (int step = 2; step <= 7; ++step) {
		DetentGate gate;
		gate.configure(DetentGateConfig(step, 1, 0));  // window 0 -> never fast
		uint32_t t = 0;
		bool advanced = false;
		for (int i = 0; i < step; ++i) {
			t += kSlowGapMs;
			advanced = gate.onDetent(1, t, kListSize, kAnchor).advanced;
		}
		TEST_ASSERT_TRUE_MESSAGE(advanced, "step must complete in exactly N detents");
	}
}

static void test_config_is_clamped_to_something_usable(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(0, -3, 0));
	TEST_ASSERT_EQUAL(1, gate.config().detentsPerStep);
	TEST_ASSERT_EQUAL(1, gate.config().fastDetentsPerStep);

	// "Fast" slower than "deliberate" is nonsense; clamp it down.
	DetentGate other;
	other.configure(DetentGateConfig(2, 9, 1000));
	TEST_ASSERT_EQUAL(2, other.config().fastDetentsPerStep);
}

static void test_non_detent_direction_is_a_no_op(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	gate.onDetent(1, 1000, kListSize, kAnchor);
	const int before = gate.fractionPermille();
	retroroom_core::DetentEvent ev = gate.onDetent(0, 1200, kListSize, kAnchor);
	TEST_ASSERT_FALSE(ev.advanced);
	TEST_ASSERT_EQUAL(before, ev.fractionPermille);
}

// ---------------------------------------------------------------------------
// Blob placement
// ---------------------------------------------------------------------------

static void test_ease_endpoints_and_midpoint(void) {
	TEST_ASSERT_EQUAL(0, easeInOutPermille(0));
	TEST_ASSERT_EQUAL(1000, easeInOutPermille(1000));
	// Clamped outside the domain rather than extrapolated.
	TEST_ASSERT_EQUAL(0, easeInOutPermille(-500));
	TEST_ASSERT_EQUAL(1000, easeInOutPermille(5000));
	// Smoothstep is symmetric about the midpoint.
	TEST_ASSERT_EQUAL(easeInOutPermille(250), 1000 - easeInOutPermille(750));
}

static void test_blob_sits_on_the_anchor_at_progress_zero(void) {
	// From NES at [1,2) (centre 1), to SMS at [7,12) (centre 9).
	LedRange b = computeBlobWindow(1, 1, 7, 5, 0, 3, 64);
	// start = 1 - 3/2 = -1, clamped to 0.
	TEST_ASSERT_EQUAL(0, b.start);
	TEST_ASSERT_EQUAL(3, b.width);
}

static void test_blob_lands_centred_on_the_target(void) {
	// Centre of [7,12) is 9, so a 3-wide band is [8,11).
	LedRange b = computeBlobWindow(1, 1, 7, 5, 1000, 3, 64);
	TEST_ASSERT_EQUAL(8, b.start);
	TEST_ASSERT_EQUAL(3, b.width);
}

static void test_blob_moves_forward_monotonically(void) {
	int previous = -1;
	for (int f = 0; f <= 1000; f += 100) {
		LedRange b = computeBlobWindow(1, 1, 7, 5, f, 3, 64);
		TEST_ASSERT_TRUE_MESSAGE(b.start >= previous, "blob must not travel backwards");
		previous = b.start;
	}
}

static void test_blob_never_leaves_the_strip(void) {
	// A target hard against the far end of a short strip, and a blob
	// wider than the target window.
	for (int f = -1000; f <= 1000; f += 250) {
		LedRange b = computeBlobWindow(0, 1, 8, 5, f, 7, 12);
		TEST_ASSERT_TRUE(b.start >= 0);
		TEST_ASSERT_TRUE(b.start + b.width <= 12);
	}
}

static void test_blob_wider_than_the_strip_is_truncated(void) {
	LedRange b = computeBlobWindow(0, 1, 0, 1, 500, 99, 8);
	TEST_ASSERT_EQUAL(0, b.start);
	TEST_ASSERT_EQUAL(8, b.width);
}

static void test_blob_with_an_unmapped_console_still_travels(void) {
	// A console with ledWidth 0 has no centre; the blob uses its start.
	LedRange b = computeBlobWindow(1, 0, 7, 0, 1000, 3, 64);
	TEST_ASSERT_EQUAL(6, b.start);
	TEST_ASSERT_EQUAL(3, b.width);
}

static void test_zero_total_leds_paints_nothing(void) {
	LedRange b = computeBlobWindow(0, 1, 5, 1, 500, 3, 0);
	TEST_ASSERT_EQUAL(0, b.start);
	TEST_ASSERT_EQUAL(0, b.width);
}

// ---------------------------------------------------------------------------
// Preview pulse
// ---------------------------------------------------------------------------

static void test_pulse_stays_inside_its_bounds(void) {
	for (std::uint32_t t = 0; t < 3000; t += 37) {
		const int s = computePulseScale(t, 900, 30, 100);
		TEST_ASSERT_TRUE(s >= 30);
		TEST_ASSERT_TRUE(s <= 100);
	}
}

static void test_pulse_actually_varies(void) {
	int lo = 1000, hi = -1000;
	for (std::uint32_t t = 0; t < 900; t += 10) {
		const int s = computePulseScale(t, 900, 20, 100);
		if (s < lo) lo = s;
		if (s > hi) hi = s;
	}
	TEST_ASSERT_TRUE_MESSAGE(hi - lo > 20, "pulse must visibly breathe");
}

static void test_pulse_is_periodic(void) {
	TEST_ASSERT_EQUAL(computePulseScale(0, 900, 20, 100),
					  computePulseScale(900, 900, 20, 100));
	TEST_ASSERT_EQUAL(computePulseScale(120, 900, 20, 100),
					  computePulseScale(1920, 900, 20, 100));
}

static void test_pulse_with_zero_period_is_fully_on(void) {
	TEST_ASSERT_EQUAL(100, computePulseScale(0, 0, 20, 100));
	TEST_ASSERT_EQUAL(100, computePulseScale(5000, 0, 20, 100));
}

// ---------------------------------------------------------------------------
// Selection effect
// ---------------------------------------------------------------------------

static void test_keep_end_inclusive_covers_the_selected_console(void) {
	// MAME in example2.json: ledPosition 27, ledWidth 15 -> 42.
	TEST_ASSERT_EQUAL(42, computeKeepEnd(27, 15, 64, true));
}

static void test_keep_end_exclusive_stops_above(void) {
	TEST_ASSERT_EQUAL(27, computeKeepEnd(27, 15, 64, false));
}

static void test_keep_end_clamps_to_the_strip(void) {
	TEST_ASSERT_EQUAL(64, computeKeepEnd(60, 15, 64, true));
	TEST_ASSERT_EQUAL(0, computeKeepEnd(0, 15, 64, false));
	TEST_ASSERT_EQUAL(0, computeKeepEnd(0, 0, 64, true));
	TEST_ASSERT_EQUAL(0, computeKeepEnd(-4, 8, 64, true));
	TEST_ASSERT_EQUAL(0, computeKeepEnd(10, 5, 0, true));
}

// The resting picture the tests settle onto: NES's window at 22, the
// selection at 100, the gaps between them dark. Same shape the
// simulator renders for example2.json's first two consoles.
static const int kSettleTarget[8] = {0, 22, 0, 0, 0, 0, 0, 0};
static const int kSettleTargetWithSelection[8] = {0, 22, 0, 0, 0, 100, 100, 0};

static void test_select_effect_is_finished_inside_its_budget(void) {
	// The user-visible contract: under a second, every pixel settled.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22, 100);
	for (int p = 0; p < 8; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(kSettleTargetWithSelection[p],
			computeSelectScale(p, kSettleTargetWithSelection, 8, cfg.totalMs, cfg),
			"pixel must be at its final value by totalMs");
	}
}

static void test_select_lands_exactly_on_its_target(void) {
	// The handoff between the selection effect and the resting paint
	// has to be invisible, and it is now invisible by construction:
	// the target is an input, so "ends on the target" cannot be a
	// separate expression that disagrees.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22, 100);
	for (int p = 0; p < 8; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(kSettleTargetWithSelection[p],
			computeSelectScale(p, kSettleTargetWithSelection, 8, cfg.totalMs, cfg),
			"effect must end on the target it was given");
	}
	// And well past the end, in case the shell samples a late frame.
	for (std::uint32_t t = 900; t < 3000; t += 50) {
		for (int p = 0; p < 8; ++p) {
			TEST_ASSERT_EQUAL(kSettleTargetWithSelection[p],
				computeSelectScale(p, kSettleTargetWithSelection, 8, t, cfg));
		}
	}
}

static void test_select_effect_is_over_a_second_is_unreachable(void) {
	// Guards the headline requirement rather than the config: whatever
	// the shell passes, nothing is still ramping past a second.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22, 100);
	for (std::uint32_t t = 1000; t < 2000; t += 50) {
		for (int p = 0; p < 8; ++p) {
			TEST_ASSERT_EQUAL(kSettleTargetWithSelection[p],
				computeSelectScale(p, kSettleTargetWithSelection, 8, t, cfg));
		}
	}
}

static void test_select_starts_by_twinkling_the_whole_strip(void) {
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22, 100);
	// A pixel that is destined to be dark is still lit during the
	// twinkle -- that is the "entire LED string twinkles" moment.
	// kSettleTarget has lit pixels only at index 1, so every other pixel
	// is one of those.
	int litDark = 0;
	for (int p = 0; p < 8; ++p) {
		if (kSettleTarget[p] == 0 &&
			computeSelectScale(p, kSettleTarget, 8, 0, cfg) > 0) {
			litDark++;
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(litDark >= 6, "whole strip twinkles, not just the prefix");
}

static void test_select_surviving_pixels_never_go_dark(void) {
	// A lit pixel must not flicker to black on the way to settling or
	// the collapse reads as a dropout.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22, 100);
	for (int p = 0; p < 8; ++p) {
		if (kSettleTargetWithSelection[p] == 0) {
			continue;
		}
		for (std::uint32_t t = 0; t < 900; t += 5) {
			TEST_ASSERT_TRUE_MESSAGE(
				computeSelectScale(p, kSettleTargetWithSelection, 8, t, cfg) > 0,
				"a surviving pixel must stay lit for the whole effect");
		}
	}
}

static void test_select_expires_early_for_the_top_of_the_strip(void) {
	// The stagger is per pixel index, so pixel 0 starts settling first
	// and is done before the bottom of the strip. That ripple is the
	// whole point of the stagger.
	//
	// Uses a 64-pixel strip because the ramp length depends on it: the
	// stagger span is (totalLeds - 1) * staggerMs and it is subtracted
	// from the settle window, so a short strip gets a much longer ramp
	// and the ripple never completes inside the effect.
	//
	// 77 is deliberately outside the twinkle range (12..60) so "is this
	// pixel settled yet?" is unambiguous -- a twinkle sample can never
	// be mistaken for a final value of 77 or 0.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 60, 77, 77);
	int target[64];
	for (int p = 0; p < 64; ++p) {
		target[p] = (p < 42) ? 77 : 0;
	}
	int firstSettled = -1;
	int lastUnsettled = -1;
	for (int p = 0; p < 64; ++p) {
		if (computeSelectScale(p, target, 64, cfg.twinkleMs + 200, cfg) ==
			target[p]) {
			if (firstSettled < 0) firstSettled = p;
		} else if (lastUnsettled < 0 || p > lastUnsettled) {
			lastUnsettled = p;
		}
	}
	TEST_ASSERT_EQUAL_MESSAGE(0, firstSettled,
		"the top of the strip must be settled well before the effect ends");
	TEST_ASSERT_EQUAL_MESSAGE(63, lastUnsettled,
		"the bottom of the strip must still be settling at the same moment");
}

static void test_zero_length_effect_settles_immediately(void) {
	const SelectionEffectConfig cfg(0, 0, 6, 12, 100, 22, 100);
	for (int p = 0; p < 8; ++p) {
		TEST_ASSERT_EQUAL(kSettleTargetWithSelection[p],
			computeSelectScale(p, kSettleTargetWithSelection, 8, 0, cfg));
	}
	// A null target means "nothing survives", rather than a crash.
	TEST_ASSERT_EQUAL(0, computeSelectScale(3, 0, 8, 0, cfg));
}

static void test_twinkle_is_deterministic_and_varies_per_pixel(void) {
	TEST_ASSERT_EQUAL(twinkleSample(7, 3), twinkleSample(7, 3));
	TEST_ASSERT_NOT_EQUAL(twinkleSample(7, 3), twinkleSample(8, 3));
	TEST_ASSERT_NOT_EQUAL(twinkleSample(7, 3), twinkleSample(7, 4));
	for (int i = 0; i < 64; ++i) {
		const int s = twinkleSample(i, 0);
		TEST_ASSERT_TRUE(s >= 0 && s <= 1023);
	}
}

// ---------------------------------------------------------------------------
// Whole-strip frames
// ---------------------------------------------------------------------------

// A frame configured the way src/configuration.h configures the
// firmware today. Anything that renders a frame in the tests goes
// through here, so a change to the real tunables shows up here.
static StripFrame configuredFrame(StripEffect effect) {
	StripFrame f;
	f.effect = effect;
	f.totalLeds = 64;
	f.abovePct = 22;
	f.selfPct = 100;
	f.fromPct = 25;
	f.toPct = 45;
	f.blobPct = 100;
	f.blobWidth = 3;
	f.pulseMinPct = 30;
	f.pulseMaxPct = 100;
	f.pulsePeriodMs = 1100;
	f.select = SelectionEffectConfig(900, 350, 6, 12, 100, 22, 100);
	return f;
}

static void test_frame_clears_every_pixel_first(void) {
	// A frame must not inherit anything from the previous one -- the
	// caller hands the same buffer back each tick, so a pixel the
	// current effect does not touch has to be zeroed or the last frame
	// ghosts through.
	int out[8];
	for (int i = 0; i < 8; ++i) {
		out[i] = 77;
	}
	StripFrame f = configuredFrame(StripEffect::PREVIEW);
	f.totalLeds = 8;
	f.to = {0, 0};  // nothing to light
	frameLevels(f, out, 8);
	for (int i = 0; i < 8; ++i) {
		TEST_ASSERT_EQUAL_MESSAGE(0, out[i], "untouched pixels must be cleared");
	}
}

static void test_resting_frame_splits_prefix_from_selection(void) {
	int out[64];
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.from = computeConsoleWindow(27, 15, 64);  // MAME
	f.aboveWindows = 0;
	f.aboveCount = 0;
		frameLevels(f, out);

	TEST_ASSERT_EQUAL(0, out[0]);
	TEST_ASSERT_EQUAL(0, out[26]);
	TEST_ASSERT_EQUAL(100, out[27]);
	TEST_ASSERT_EQUAL(100, out[41]);
	TEST_ASSERT_EQUAL(0, out[42]);
	TEST_ASSERT_EQUAL(0, out[63]);
}




static void test_preview_frame_pulses_only_the_target(void) {
	int out[64];
	StripFrame f = configuredFrame(StripEffect::PREVIEW);
	f.to = computeConsoleWindow(7, 5, 64);
	f.from = computeConsoleWindow(1, 1, 64);  // must be ignored
	f.elapsedMs = 0;
	frameLevels(f, out);
	// Only the target window is lit at all, and at the dim end of the
	// glow on the first frame.
	TEST_ASSERT_EQUAL(0, out[1]);
	TEST_ASSERT_EQUAL_MESSAGE(computePulseScale(0, 1100, 30, 100), out[9],
		"preview starts at the dim end of the pulse");
	TEST_ASSERT_EQUAL(0, out[0]);
	TEST_ASSERT_EQUAL(0, out[63]);
}

static void test_selecting_frame_ends_on_the_resting_paint(void) {
	// The handoff has to be invisible. This is now true by
	// construction -- SELECTING resolves its target through the same
	// helper RESTING renders with -- but assert it anyway: it is the one
	// invariant whose violation is a visible flash and nothing else.
	int selecting[64];
	int resting[64];

	LedRange above[1];
	above[0] = computeConsoleWindow(1, 1, 64);  // NES

	StripFrame s = configuredFrame(StripEffect::SELECTING);
	s.from = computeConsoleWindow(27, 15, 64);
	s.aboveWindows = above;
	s.aboveCount = 1;
	s.elapsedMs = s.select.totalMs;
	frameLevels(s, selecting);

	StripFrame r = configuredFrame(StripEffect::RESTING);
	r.from = s.from;
	r.aboveWindows = above;
	r.aboveCount = 1;
	frameLevels(r, resting);

	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(resting[p], selecting[p],
			"selection effect must end on the resting paint");
	}
}


static void test_frame_rejects_a_bad_strip_size(void) {
	// A frame that does not know how long the strip is must not write
	// anywhere. Asserted against computeStripFrame() directly, because
	// the level helper reads the whole buffer and would paper over it.
	retroroom_core::StripPixel out[4];
	for (int i = 0; i < 4; ++i) {
		out[i].role = retroroom_core::LedRole::TRAVEL;
		out[i].level = 77;
	}
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.totalLeds = 0;
	computeStripFrame(f, out);
	for (int i = 0; i < 4; ++i) {
		TEST_ASSERT_EQUAL_MESSAGE(77, out[i].level,
			"a zero-sized strip must not write into the caller's buffer");
		TEST_ASSERT_TRUE(out[i].role == retroroom_core::LedRole::TRAVEL);
	}
	// And a null destination is a no-op, not a crash.
	computeStripFrame(f, 0);
}

static void test_frame_clamps_windows_that_overrun_the_strip(void) {
	// A config whose ledPosition + ledWidth exceeds the strip must not
	// write past the end of the caller's buffer.
	int out[16];
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.totalLeds = 16;
	f.from = computeConsoleWindow(12, 20, 16);  // truncated to width 4
	f.aboveWindows = 0;
	f.aboveCount = 0;
	frameLevels(f, out, 16);
	TEST_ASSERT_EQUAL(100, out[15]);
}

// The example2.json windows, and the gaps between them.
static const LedRange kExample2[] = {
	{1, 1},   // NES   led 1
	{7, 5},   // SMS   leds 7..11
	{17, 5},  // XBOX  leds 17..21
	{27, 15}, // MAME  leds 27..41
};

static void test_resting_frame_lights_each_window_above(void) {
	// The consoles do not tile the strip. With SMS selected, NES's window
	// is lit dimly and SMS's brightly, and the pixels between them -- the
	// physical gap between shelves -- stay dark. Filling those gaps is
	// what made the strip read as one bar from the top of the cabinet
	// rather than a stack of separate consoles.
	int out[64];
	LedRange above[1];
	above[0] = computeConsoleWindow(1, 1, 64);  // NES at [1,2)

	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.from = computeConsoleWindow(7, 5, 64);  // SMS at [7,12)
	f.aboveWindows = above;
	f.aboveCount = 1;
	frameLevels(f, out);

	TEST_ASSERT_EQUAL_MESSAGE(0, out[0], "led 0 belongs to no console");
	TEST_ASSERT_EQUAL(22, out[1]);
	TEST_ASSERT_EQUAL_MESSAGE(0, out[2], "the gap above SMS must stay dark");
	TEST_ASSERT_EQUAL(0, out[6]);
	TEST_ASSERT_EQUAL(100, out[7]);
	TEST_ASSERT_EQUAL(100, out[11]);
	TEST_ASSERT_EQUAL(0, out[12]);
	TEST_ASSERT_EQUAL(0, out[63]);
}

static void test_resting_frame_with_zero_above_lights_only_the_selection(void) {
	// abovePct 0 is the third reading of the same phrase: the selected
	// console and nothing else.
	int out[64];
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.from = computeConsoleWindow(7, 5, 64);
	f.abovePct = 0;
	frameLevels(f, out);
	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE((p >= 7 && p < 12) ? 100 : 0, out[p],
			"only the selection may be lit");
	}
}

static void test_resting_frame_tolerates_a_null_above_list(void) {
	// A non-zero count with a null pointer must not be dereferenced.
	int out[64];
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.from = computeConsoleWindow(7, 5, 64);
	f.aboveWindows = 0;
	f.aboveCount = 5;
	frameLevels(f, out);
	TEST_ASSERT_EQUAL(0, out[1]);
	TEST_ASSERT_EQUAL(100, out[7]);
}

static void test_collect_above_picks_only_earlier_windows(void) {
	LedRange out[4];
	TEST_ASSERT_EQUAL(0, collectAboveWindows(kExample2, 4, 1, out, 4));
	TEST_ASSERT_EQUAL(1, collectAboveWindows(kExample2, 4, 7, out, 4));
	TEST_ASSERT_EQUAL(2, collectAboveWindows(kExample2, 4, 17, out, 4));
	TEST_ASSERT_EQUAL(3, collectAboveWindows(kExample2, 4, 27, out, 4));
	TEST_ASSERT_EQUAL(4, collectAboveWindows(kExample2, 4, 64, out, 4));
}

static void test_collect_above_preserves_the_windows_it_keeps(void) {
	// XBOX starts at 17, so NES ([1,2)) and SMS ([7,12)) are wholly
	// above it and XBOX's own window is not. Input order is preserved --
	// the lit set is the set, and sorting it would be gratuitous.
	LedRange out[4];
	const int n = collectAboveWindows(kExample2, 4, 17, out, 4);
	TEST_ASSERT_EQUAL(2, n);
	TEST_ASSERT_EQUAL(1, out[0].start);
	TEST_ASSERT_EQUAL(1, out[0].width);
	TEST_ASSERT_EQUAL(7, out[1].start);
	TEST_ASSERT_EQUAL(5, out[1].width);
}

static void test_collect_above_skips_a_straddling_window(void) {
	// A hand-edited config with an overlapping window: the middle entry
	// starts before the selection and ends after it, so it is neither
	// wholly above nor the selection. It is left out rather than clipped
	// -- one console dark beats painting its pixels twice at the wrong
	// brightness. The entry wholly above still comes through, so one bad
	// record does not blank the whole stack.
	LedRange mixed[3] = {{2, 3}, {5, 20}, {30, 4}};
	LedRange out[3];
	TEST_ASSERT_EQUAL(1, collectAboveWindows(mixed, 3, 12, out, 3));
	TEST_ASSERT_EQUAL(2, out[0].start);
	TEST_ASSERT_EQUAL(3, out[0].width);
	// A zero-width window is not a console.
	LedRange empty[1] = {{5, 0}};
	TEST_ASSERT_EQUAL(0, collectAboveWindows(empty, 1, 30, out, 3));
}

static void test_collect_above_is_defensive_about_bad_arguments(void) {
	LedRange out[2];
	TEST_ASSERT_EQUAL(0, collectAboveWindows(0, 4, 30, out, 2));
	TEST_ASSERT_EQUAL(0, collectAboveWindows(kExample2, 4, 30, 0, 2));
	TEST_ASSERT_EQUAL(0, collectAboveWindows(kExample2, 4, 30, out, 0));
	TEST_ASSERT_EQUAL(0, collectAboveWindows(kExample2, 0, 30, out, 2));
	TEST_ASSERT_EQUAL_MESSAGE(1, collectAboveWindows(kExample2, 4, 64, out, 1),
		"a short buffer must truncate, not overrun");
}

static void test_gate_freezes_at_the_end_of_the_list(void) {
	// The cabinet has physical ends. Turning off the end of it reaches
	// nothing, so the position must not move at all -- if it kept
	// filling, the operator would watch a full four-detent progression
	// animate and then nothing happen, which reads as a broken strip
	// rather than as an end.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 12; ++i) {
		t += kSlowGapMs;
		const retroroom_core::DetentEvent ev =
			gate.onDetent(1, t, /*listSize=*/3, /*anchorIndex=*/2);
		TEST_ASSERT_TRUE_MESSAGE(ev.frozen, "every detent off the end must freeze");
		TEST_ASSERT_FALSE(ev.advanced);
		TEST_ASSERT_EQUAL(0, ev.fractionPermille);
		TEST_ASSERT_EQUAL_MESSAGE(0, gate.fractionPermille(),
			"the position must not move while frozen");
	}
}

static void test_gate_freezes_at_the_start_of_the_list(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 12; ++i) {
		t += kSlowGapMs;
		const retroroom_core::DetentEvent ev =
			gate.onDetent(-1, t, /*listSize=*/3, /*anchorIndex=*/0);
		TEST_ASSERT_TRUE(ev.frozen);
		TEST_ASSERT_EQUAL(0, gate.fractionPermille());
	}
}

static void test_gate_unfreezes_once_turned_back(void) {
	// Freezing is per detent, not a latched state: turn back and the
	// browse has to resume, or the knob would be dead at the end of the
	// list until something else reset it.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 6; ++i) {
		t += kSlowGapMs;
		gate.onDetent(1, t, 3, 2);
	}
	t += kSlowGapMs;
	retroroom_core::DetentEvent ev = gate.onDetent(-1, t, 3, 2);
	TEST_ASSERT_FALSE_MESSAGE(ev.frozen, "turning back must resume the browse");
	TEST_ASSERT_FALSE(ev.advanced);
	TEST_ASSERT_TRUE(gate.fractionPermille() < 0);
	TEST_ASSERT_EQUAL(-1, ev.targetDirection);
}

static void test_gate_freeze_does_not_wreck_the_fast_mode_clock(void) {
	// A frozen knob is not a pause in the spin. The detent is still
	// recorded, so the gap since the previous one stays honest and a
	// frozen knob followed by a fast spin still escalates.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 6; ++i) {
		t += kSlowGapMs;
		gate.onDetent(1, t, 3, 2);
	}
	t += 20;
	const retroroom_core::DetentEvent ev = gate.onDetent(1, t, 3, 2);
	TEST_ASSERT_TRUE_MESSAGE(ev.fastMode,
		"a tight gap after a frozen detent must still read as fast");
}

static void test_gate_on_an_empty_list_always_freezes(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	const retroroom_core::DetentEvent ev = gate.onDetent(1, 100, 0, 0);
	TEST_ASSERT_TRUE(ev.frozen);
	TEST_ASSERT_FALSE(ev.advanced);
}

// A travel frame from two windows and a path, matching what the shell
// assembles. `spark` is the block's width where it leaves the console.
static StripFrame travelFrame(const LedRange& leave, int entryPixel,
                              const LedRange& target, std::uint32_t elapsedMs,
                              int spark = 2) {
	StripFrame f = configuredFrame(StripEffect::TRAVEL);
	f.dimPct = 22;
	f.fillPct = 45;
	f.travelPct = 100;
	f.minFillLeds = 3;
	f.travelMs = 1000;
	f.aboveWindows = 0;
	f.aboveCount = 0;
	// On a first-step browse the active console *is* the one being left.
	f.activeWindow = leave;
	computeTravelPath(leave, entryPixel, target, spark, f);
	f.elapsedMs = elapsedMs;
	return f;
}

// The ordinary case: the block enters at the console being left's
// trailing edge and lands on the target.
static StripFrame stepTravel(const LedRange& leave, const LedRange& target,
                             std::uint32_t elapsedMs) {
	return travelFrame(leave, leave.start + leave.width, target, elapsedMs);
}

// example2.json's first two windows: NES [1,2) and SMS [7,12).
static const LedRange kNes = {1, 1};
static const LedRange kSms = {7, 5};

static void test_travel_ends_exactly_on_the_target_window(void) {
	// The last frame of the travel IS the preview. If it does not land
	// exactly on the target the block has to "correct" on arrival, which
	// reads as a mistake.
	int out[64];
	frameLevels(stepTravel(kNes, kSms, 1000), out);
	for (int p = 7; p < 12; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(100, out[p], "target window must be fully lit");
	}
	TEST_ASSERT_EQUAL(0, out[6]);
	TEST_ASSERT_EQUAL(0, out[12]);
}

static void test_travel_starts_on_the_console_being_leaving(void) {
	// The spark sits on NES's trailing edge and the block is drawn *over*
	// the console it is leaving, not under it -- otherwise the block
	// appears to start in the gap rather than peel off the console.
	int out[64];
	frameLevels(stepTravel(kNes, kSms, 0), out);
	TEST_ASSERT_EQUAL(100, out[1]);
	TEST_ASSERT_EQUAL(100, out[2]);
	TEST_ASSERT_EQUAL(0, out[0]);
	// The fill is lit ahead of the block from the first frame.
	TEST_ASSERT_EQUAL(45, out[3]);
		TEST_ASSERT_EQUAL(45, out[6]);
		// The run stops short of SMS -- that is where the block lands.
		TEST_ASSERT_EQUAL(0, out[7]);
}

static void test_travel_consumes_the_fill_behind_it(void) {
	// The knob-turn fill goes dark as the block passes it. That is what
	// makes the block look like it is eating the path rather than
	// sliding over a backdrop.
	int early[64];
	int late[64];
	frameLevels(stepTravel(kNes, kSms, 0), early);
	frameLevels(stepTravel(kNes, kSms, 900), late);
	// Pixels 3..6 are fill at the start and dark by the end, having
	// been swept over on the way.
	for (int p = 3; p <= 5; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(45, early[p], "fill must be lit at the start");
		TEST_ASSERT_EQUAL_MESSAGE(0, late[p],
			"a pixel the block has passed must be dark");
	}
}

static void test_travel_keeps_the_fill_ahead_of_it(void) {
	// The other half of the same rule: the fill the block has not
	// reached yet stays lit, or the block is sliding over a backdrop
	// rather than consuming a path.
	int out[64];
	// Early in the travel the block has barely left, so the run ahead of
	// it is still substantial.
    frameLevels(stepTravel(kNes, kSms, 0), out);
	int ahead = 0;
    for (int p = 3; p < 7; ++p) {
		if (out[p] == 45) ahead++;
	}
	TEST_ASSERT_TRUE_MESSAGE(ahead >= 2,
		"the fill ahead of the block must still be lit");
}

static void test_travel_stretches_then_settles(void) {
	// The two edges move at different rates, so the block is widest
	// part-way and narrows onto the target. A fixed-width slide cannot
	// do this, and it is the shape the whole effect is for.
	int widest = 0;
	for (int prog = 0; prog <= 1000; prog += 25) {
		const TravelEdges e = travelEdges(1000, 3000, 7000, 12000, prog, 6000);
		if (e.widthPermille > widest) widest = e.widthPermille;
		TEST_ASSERT_TRUE_MESSAGE(e.widthPermille <= 6000,
			"the block must never exceed its peak width");
		TEST_ASSERT_TRUE(e.leftPermille <= e.rightPermille);
	}
	TEST_ASSERT_TRUE_MESSAGE(widest > kSms.width * 1000,
		"the block must be wider than the target at some point");
	// Ends exactly on the target, so the last frame is the preview.
	const TravelEdges last = travelEdges(1000, 3000, 7000, 12000, 1000, 6000);
	TEST_ASSERT_EQUAL(7000, last.leftPermille);
	TEST_ASSERT_EQUAL(12000, last.rightPermille);
}

static void test_travel_sweeps_a_shelf_for_a_shelf_crossing(void) {
	// A step between shelves enters at the far end of the destination
	// shelf and sweeps back to the target: the knob goes right, the
	// light goes left. Compared with the ordinary step it covers far
	// more of the strip, which is the entire point of doing it.
	//
	// SMS is left at [7,12); the block enters at pixel 52 (the far end
	// of that shelf) and lands on NES at [1,2).
	// Measured as how far the block's leading edge travels, which is the
	// contract: the point of entering at the far end is that the block
	// crosses the whole shelf rather than a two-pixel gap.
	StripFrame ordinary;
	computeTravelPath(kSms, kSms.start + kSms.width, kNes, 2, ordinary);
	StripFrame crossing;
	computeTravelPath(kSms, 52, kNes, 2, crossing);

	const int ordinarySpan = std::abs(ordinary.travelToLeftPermille -
									  ordinary.travelFromLeftPermille);
	const int crossingSpan = std::abs(crossing.travelToLeftPermille -
									  crossing.travelFromLeftPermille);
	// SMS ends at 12 and NES starts at 1, so the gap is 10 LEDs.
	TEST_ASSERT_EQUAL_MESSAGE(10000, ordinarySpan,
		"an ordinary step crosses exactly the gap between the two windows");
	TEST_ASSERT_TRUE_MESSAGE(crossingSpan > ordinarySpan * 3,
		"a shelf crossing must sweep the width of the shelf, not a gap");
	// It starts out on the far side and arrives at the target.
	TEST_ASSERT_EQUAL(51000, crossing.travelFromLeftPermille);
	TEST_ASSERT_EQUAL(1000, crossing.travelToLeftPermille);
	// And the block must actually be visible doing it -- a travel that
	// spends its middle inverted looks like nothing happened.
	int visible = 0;
	for (std::uint32_t t = 0; t <= 1000; t += 50) {
		int b[64];
		frameLevels(travelFrame(kSms, 52, kNes, t), b);
		for (int p = 0; p < 64; ++p) {
			if (b[p] == 100) {
				visible++;
				break;
			}
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(visible >= 20,
		"the block must be visible on essentially every frame of the sweep");
}

static void test_travel_backwards_does_not_invert_the_block(void) {
	// Regression. The block's *leading* edge is whichever one is moving
	// into new territory, and on a backwards travel that is the LEFT
	// one. Easing the right edge as leading unconditionally made the two
	// cross and the block vanished for the whole middle of every shelf
	// crossing.
	for (int prog = 0; prog <= 1000; prog += 20) {
		const TravelEdges e = travelEdges(51000, 53000, 1000, 2000, prog, 6000);
		TEST_ASSERT_TRUE_MESSAGE(e.leftPermille <= e.rightPermille,
			"a backwards travel must not invert the block");
		TEST_ASSERT_TRUE_MESSAGE(e.widthPermille > 0,
			"the block must be visible throughout a backwards travel");
	}
	// And it really does move leftwards.
	TEST_ASSERT_TRUE(travelEdges(51000, 53000, 1000, 2000, 0, 6000).leftPermille >
					 travelEdges(51000, 53000, 1000, 2000, 1000, 6000).leftPermille);
}

static void test_coverage_is_exact_at_the_edges(void) {
	// The antialiasing: a block covering half a pixel lights it half as
	// brightly, and the arithmetic is exact rather than sampled.
	TEST_ASSERT_EQUAL(100, coveragePercent(0, 1000, 0));     // pixel 0 fully covered
	TEST_ASSERT_EQUAL(50, coveragePercent(0, 500, 0));       // its left half
	TEST_ASSERT_EQUAL(50, coveragePercent(500, 1000, 0));    // its right half
	TEST_ASSERT_EQUAL(0, coveragePercent(0, 1000, 1));       // beyond the block
	TEST_ASSERT_EQUAL(100, coveragePercent(1000, 2000, 1));
	TEST_ASSERT_EQUAL(0, coveragePercent(2000, 3000, 0));    // entirely past
	// A sub-pixel block lights exactly one pixel, partially.
	TEST_ASSERT_TRUE(coveragePercent(1400, 1500, 1) > 0);
	TEST_ASSERT_TRUE(coveragePercent(1400, 1500, 1) < 100);
}

static void test_fill_run_is_the_gap_between_the_windows(void) {
	// The run is the gap, with its anchor on the side of the console
	// being left. It used to be measured from the source's trailing edge
	// in both directions, so a backwards step's run lay *inside* the
	// console being left and the indicator crept the wrong way.
	StripFrame fwd;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, fwd);
	TEST_ASSERT_EQUAL(2, fwd.fillAnchor);   // just past NES [1,2)
	TEST_ASSERT_EQUAL(7, fwd.fillLead);     // just short of SMS [7,12)
	TEST_ASSERT_TRUE(fwd.fillForward);

	StripFrame back;
	computeFillGeometry(kSms, kSms.start + kSms.width, kNes, 0, 64, back);
	TEST_ASSERT_EQUAL_MESSAGE(7, back.fillAnchor, "just short of SMS [7,12)");
	TEST_ASSERT_EQUAL_MESSAGE(2, back.fillLead, "just past NES [1,2)");
	TEST_ASSERT_FALSE(back.fillForward);
}

static void test_fill_run_has_a_floor(void) {
	// A step between shelves is a couple of pixels in index space. The
	// floor keeps the progression indicator moving on exactly the steps
	// where it is hardest to see what is happening.
	// NES [1,2) to SMS [7,12) has a five-LED gap. A floor of 3 is
	// already shorter than that, so the natural gap is what runs.
	StripFrame f;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 3, 64, f);
	TEST_ASSERT_EQUAL(7, f.fillLead);
	// A floor longer than the gap is what extends it.
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 8, 64, f);
	TEST_ASSERT_EQUAL(10, f.fillLead);
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	TEST_ASSERT_EQUAL(7, f.fillLead);

	// And the run never leaves the strip, whichever way it runs.
	computeFillGeometry({50, 4}, 54, {60, 1}, 10, 64, f);
	TEST_ASSERT_EQUAL_MESSAGE(64, f.fillLead, "floor must clamp to the strip");
	// A console hard against the end of the strip, stepping back off
	// it: the floor runs backwards from the console's own near edge.
	computeFillGeometry({60, 4}, 64, {63, 1}, 10, 64, f);
	// Measured, not derived: a console at the very end of the strip
	// stepping back off it anchors at its own near edge and the floor
	// runs back from there, clamped to the strip.
	TEST_ASSERT_EQUAL_MESSAGE(60, f.fillAnchor, "anchored on the console being left");
	TEST_ASSERT_EQUAL_MESSAGE(64, f.fillLead, "clamped to the end of the strip");
}

static void test_fill_lead_moves_towards_the_target(void) {
	// The whole point of the indicator: the edge the operator watches
	// walks across the gap as they turn, and stops at the target.
	StripFrame f;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	int previous = kNes.start + kNes.width;
	for (int p = 250; p <= 1000; p += 250) {
		const int lead =
			scaleFillLead(f.fillAnchor, f.fillLead, f.fillForward, p);
		TEST_ASSERT_TRUE_MESSAGE(lead > previous, "lead must advance");
		previous = lead;
	}
	// ...and the first click must show something. Truncating rather than
	// rounding left the first detent of a short gap with no visible
	// response at all.
	const int firstClick =
		scaleFillLead(f.fillAnchor, f.fillLead, f.fillForward, 250);
	TEST_ASSERT_TRUE_MESSAGE(firstClick > f.fillAnchor,
		"the first detent must already light part of the gap");

	// Backwards, the same walk runs the other way.
	StripFrame b;
	computeFillGeometry(kSms, kSms.start + kSms.width, kNes, 0, 64, b);
	int prevBack = kSms.start;
	for (int p = 250; p <= 1000; p += 250) {
		const int lead =
			scaleFillLead(b.fillAnchor, b.fillLead, b.fillForward, p);
		TEST_ASSERT_TRUE_MESSAGE(lead < prevBack, "a backwards lead must retreat");
		prevBack = lead;
	}
	TEST_ASSERT_EQUAL(2, scaleFillLead(b.fillAnchor, b.fillLead, b.fillForward, 1000));
}

static void test_fill_does_not_light_pixels_below_the_console(void) {
	// Regression. Dimming "everything below the run" lit up bare gap
	// pixels at the start of the strip, so the indicator appeared to
	// begin at LED 0 rather than at the console being turned away from.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 22;
	f.fillPct = 45;
	f.minFillLeds = 0;
	f.from = kNes;
	f.activeWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	f.fillLead = scaleFillLead(f.fillAnchor, f.fillLead, f.fillForward, 1000);
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(0, out[0], "LED 0 belongs to no console");
	TEST_ASSERT_EQUAL(22, out[1]);
	TEST_ASSERT_EQUAL(45, out[2]);
	TEST_ASSERT_EQUAL(45, out[6]);
	TEST_ASSERT_EQUAL_MESSAGE(0, out[7], "the target's window stays dark");
}

static void test_fill_runs_backwards_when_browsing_back(void) {
	// The browse goes both ways. A backwards step used to get a
	// zero-length run, so the progression indicator simply did not
	// appear when browsing back.
	int early[64];
	int late[64];
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 22;
	f.fillPct = 45;
	f.minFillLeds = 0;
	f.from = kSms;
	f.activeWindow = kSms;
	computeFillGeometry(kSms, kSms.start + kSms.width, kNes, 0, 64, f);
	// At the start of the step the run is just short of SMS. Both leads
	// are scaled from the *unprogressed* one -- scaling an already
	// scaled lead compounds the rounding and the run never arrives.
	const int anchor = f.fillAnchor;
	const int lead = f.fillLead;
	f.fillLead = scaleFillLead(anchor, lead, f.fillForward, 250);
	frameLevels(f, early);
	// At the end it reaches NES's trailing edge.
	f.fillLead = scaleFillLead(anchor, lead, f.fillForward, 1000);
	frameLevels(f, late);
	TEST_ASSERT_EQUAL_MESSAGE(22, early[7], "SMS itself is dim, not fill");
	TEST_ASSERT_EQUAL_MESSAGE(0, early[12], "past SMS, outside the run");
	TEST_ASSERT_TRUE_MESSAGE(late[2] == 45,
		"a backwards run must reach NES's trailing edge");
	// The consoles already in the stack stay dim throughout.
	TEST_ASSERT_EQUAL(0, late[1]);
}

static void test_fill_never_lights_the_target_window(void) {
	// The block lands on the target; lighting its window dim in the
	// meantime would make the arrival mean nothing.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 22;
	f.fillPct = 45;
	f.minFillLeds = 0;
	computeTravelPath(kNes, kNes.start + kNes.width, kSms, 2, f);
	f.effect = StripEffect::FILLING;
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL(kSms.start, f.fillLead);
	for (int p = 7; p < 12; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(0, out[p],
			"the target's own window must stay dark until the block lands");
	}
}

static void test_shelf_crossing_sweeps_the_whole_destination_shelf(void) {
	// A step between shelves enters at the far end of the destination
	// shelf and sweeps back to the target, so the light travels the
	// width of the cabinet even though the knob went forward one
	// console -- and the fill spans that whole sweep for the block to
	// consume.
	StripFrame ordinary;
	computeTravelPath(kSms, kSms.start + kSms.width, kNes, 2, ordinary);
	StripFrame crossing;
	computeTravelPath(kSms, 52, kNes, 2, crossing);
	TEST_ASSERT_EQUAL(2, ordinary.fillLead);
	// The block *starts* at the far end and lands on the target, so the
	// far end is the anchor and the target is the leading edge.
	TEST_ASSERT_EQUAL_MESSAGE(52, crossing.fillAnchor,
		"a shelf crossing sweeps in from the far end of the shelf");
	TEST_ASSERT_EQUAL(1, crossing.fillLead);
	TEST_ASSERT_FALSE(crossing.fillForward);
}

static void test_a_return_across_the_bridge_sweeps_the_shelf_too(void) {
	// The same crossing made in reverse, and the one that was broken.
	// Going back over the bridge the block enters at the *low* end of
	// the destination shelf -- still the end the string does not arrive
	// at -- and sweeps the whole shelf the other way.
	//
	// The entry used to always be the high end of the destination
	// shelf. Going up that is the far end and it is right. Coming back
	// it is the console being returned to, so the run was measured as
	// the gap between the two windows -- two pixels across the bridge
	// -- and a step that had just sent the light the width of a shelf
	// came back as a one-pixel nudge.
	//
	// MAME and GEN are the two consoles either side of the bridge, both
	// at the bridge end of their own shelf, which is what makes them the
	// hard case: there is no gap to show in either direction.
	const LedRange mame = {19, 6};
	const LedRange gen = {26, 3};
	StripFrame f = travelFrame(gen, /*entryPixel=*/1, mame, 0);
	TEST_ASSERT_EQUAL_MESSAGE(1, f.fillAnchor,
		"a return crossing sweeps in from the far end of the shelf");
	TEST_ASSERT_EQUAL(19, f.fillLead);
	TEST_ASSERT_TRUE(f.fillForward);
	// And the block itself travels the shelf rather than the gap: it
	// leaves at the low end and lands on MAME's window.
	TEST_ASSERT_EQUAL(0, f.travelFromLeftPermille);
	TEST_ASSERT_EQUAL_MESSAGE(mame.start * 1000, f.travelToLeftPermille,
		"the block lands on the console being returned to");

	StripFrame g = configuredFrame(StripEffect::FILLING);
	g.dimPct = 22;
	g.fillPct = 45;
	g.minFillLeds = 0;
	g.from = gen;
	g.activeWindow = gen;
	computeFillGeometry(gen, 1, mame, 0, 64, g);
	int out[64];
	frameLevels(g, out);
	TEST_ASSERT_EQUAL_MESSAGE(45, out[1],
		"the run starts at the far end of the destination shelf");
	TEST_ASSERT_EQUAL_MESSAGE(45, out[12],
		"and runs the length of it, over the consoles already in the stack");
	TEST_ASSERT_EQUAL_MESSAGE(0, out[19],
		"stopping short of the console the block lands on");
	TEST_ASSERT_EQUAL_MESSAGE(22, out[26],
		"the console being left stays dim, not fill");
}

static void test_the_block_enters_on_the_edge_it_departs_by(void) {
	// The direction rule, which is what the shell used to get wrong.
	//
	// The block's spark is centred on the entry pixel, so the edge has
	// to be the one the block is *leaving by*. The trailing edge is
	// right going forward and wrong coming back: coming back, the block
	// appeared on the right of the console it was leaving and then ran
	// left, which read as a mis-start a LED too far along. It showed up
	// as a bare LED to the right of the console flashing on the first
	// frame of the animation, on every console, only in that direction.
	TEST_ASSERT_EQUAL_MESSAGE(kNes.start + kNes.width,
							  travelEntryFor(kNes, kSms),
							  "a forward step leaves by the trailing edge");
	TEST_ASSERT_EQUAL_MESSAGE(kSms.start, travelEntryFor(kSms, kNes),
							  "a backward step leaves by the leading edge");
}

static void test_the_block_starts_where_its_own_fill_starts(void) {
	// The two halves have to agree, in both directions. The fill anchors
	// on the edge the block departs by, so entering anywhere else leaves
	// the block starting outside its own run -- which is what it did: the
	// run began at the console's leading edge and the block three LEDs to
	// the right of the end of it.
	//
	// This is the invariant that would have caught it, and it is
	// direction-symmetric on purpose. The bug was invisible going
	// forward, so a forward-only test of it always passes.
	StripFrame forward = travelFrame(kNes, travelEntryFor(kNes, kSms), kSms, 0);
	TEST_ASSERT_EQUAL_MESSAGE(forward.fillAnchor,
							  forward.travelFromLeftPermille / 1000 + 1,
							  "going forward the block enters on the fill's anchor");
	StripFrame back = travelFrame(kSms, travelEntryFor(kSms, kNes), kNes, 0);
	TEST_ASSERT_EQUAL_MESSAGE(back.fillAnchor,
							  back.travelFromLeftPermille / 1000 + 1,
							  "coming back the block enters on the fill's anchor");
	// And the run is continuous with the block on the very first frame:
	// the spark's outer LED is inside the run, so nothing is skipped.
	const int runLo = (back.fillLead < back.fillAnchor) ? back.fillLead
														: back.fillAnchor;
	const int runHi = (back.fillLead < back.fillAnchor) ? back.fillAnchor
														: back.fillLead;
	TEST_ASSERT_TRUE_MESSAGE(
		back.travelFromLeftPermille / 1000 >= runLo &&
			back.travelFromLeftPermille / 1000 < runHi,
		"the spark must reach into the run it is about to consume");
}

static void test_the_backward_block_no_longer_starts_right_of_the_console(void) {
	// The exact symptom, as pixels. The fixture's SMS is 7..11; rotating
	// left to NES used to start the block on 11..12 -- the console's last
	// LED plus the bare one to its right -- and then run left, so that
	// bare LED flashed for a frame on the wrong side of the console. It
	// starts on 6..7 now: the gap it is heading into, and the console's
	// first LED.
	StripFrame f = travelFrame(kSms, travelEntryFor(kSms, kNes), kNes, 0);
	TEST_ASSERT_EQUAL_MESSAGE(6, f.travelFromLeftPermille / 1000,
							  "the spark's leading edge is in the gap being entered");
	TEST_ASSERT_EQUAL_MESSAGE(7, f.travelFromRightPermille / 1000 - 1,
							  "its other LED is on the console being left");
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(0, out[12],
		"the bare LED right of the console must stay dark");
	// The run it is about to consume is still there, and the block is
	// sitting on the end of it.
	TEST_ASSERT_EQUAL(45, out[5]);
	TEST_ASSERT_EQUAL_MESSAGE(22, out[8],
							  "the rest of the console being left stays dim");
}

static void test_a_spark_centred_on_the_first_led_is_safe(void) {
	// A crossing into a shelf whose first console sits at LED 0 enters
	// at 0, and the spark is centred on the entry -- so half of it hangs
	// off the front of the strip. Coverage is clamped, so this reads as
	// LED 0 lit and nothing else; it cannot index off the front. Worth
	// pinning, because a negative entry is the obvious next thing to
	// "fix", and it is the fixed path that would break.
	StripFrame f = travelFrame({0, 2}, /*entryPixel=*/0, {6, 2}, 0);
	TEST_ASSERT_EQUAL(-1000, f.travelFromLeftPermille);
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(100, out[0],
							  "the half-spark still lights the first LED");
}

static void test_the_retreat_shortens_the_run_from_the_front(void) {
	// The overshoot retreat. The operator turned past a console and
	// stopped; the run is still pointing at a console they did not ask
	// for. It gets given back from its leading edge, and the anchor end
	// stays exactly where it was, so the run retracts rather than
	// sliding.
	//
	// Forward run, anchor at 2, lead at 7 (pixels 2..6).
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 0;
	f.fillPct = 100;
	f.from = kNes;
	f.activeWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	f.fillLead = 7;
	f.fillRetreatPermille = 2000;   // two LEDs
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(100, out[2], "the anchor end never moves");
	TEST_ASSERT_EQUAL(100, out[3]);
	TEST_ASSERT_EQUAL(100, out[4]);
	TEST_ASSERT_EQUAL_MESSAGE(0, out[5], "the leading end is given back");
	TEST_ASSERT_EQUAL(0, out[6]);
	TEST_ASSERT_EQUAL(0, out[7]);
}

static void test_the_retreat_fades_rather_than_snapping(void) {
	// The reason the retreat is in permille. Withdrawing a whole LED at
	// a time would switch the boundary LED off, and a run that blinks
	// itself out reads as a fault rather than a release. Withdrawing
	// part of a LED leaves it partly covered, so it dims.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 0;
	f.fillPct = 100;
	f.from = kNes;
	f.activeWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	f.fillLead = 7;
	int out[64];
	// Nothing withdrawn: the boundary LED is fully lit, because a whole
	// LED lead leaves no partial anywhere.
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(100, out[6], "no retreat, no partial LED");
	// Half a LED withdrawn: the same LED is half covered.
	f.fillRetreatPermille = 500;
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(50, out[6], "a half-withdrawn LED is half lit");
	// And it is still a fill, not switched off -- the role is what the
	// simulator's view and the colour depend on.
	retroroom_core::LedRole r[64];
	frameRoles(f, r);
	TEST_ASSERT_TRUE(r[6] == retroroom_core::LedRole::FILL);
	// The LEDs behind it are untouched, and the one past the lead is
	// still dark: only the boundary pixel is partial.
	TEST_ASSERT_TRUE(r[5] == retroroom_core::LedRole::FILL);
	TEST_ASSERT_TRUE(r[2] == retroroom_core::LedRole::FILL);
	TEST_ASSERT_TRUE_MESSAGE(r[7] == retroroom_core::LedRole::OFF,
							  "and nothing past the lead is claimed");
}

static void test_a_retreat_longer_than_the_run_empties_it(void) {
	// Overshooting badly must not invert the run or light the wrong
	// span. It empties, which is what leaves the pulsing selection on
	// its own.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 0;
	f.fillPct = 100;
	f.from = kNes;
	f.activeWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	f.fillLead = 7;
	f.fillRetreatPermille = 9999;
	int out[64];
	frameLevels(f, out);
	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(0, out[p], "an over-long retreat empties the run");
	}
}

static void test_the_retreat_applies_at_the_low_end_going_back(void) {
	// Direction. A backwards run is bounded the other way round, so
	// "towards the anchor" is the other direction too. Getting this
	// wrong would make a backwards retreat *lengthen* the run.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 0;
	f.fillPct = 100;
	f.from = kSms;
	f.activeWindow = kSms;
	computeTravelPath(kSms, kSms.start, kNes, 2, f);
	TEST_ASSERT_FALSE(f.fillForward);
	// Anchor 7, lead 2, so the run is pixels 2..6 -- five LEDs. Two are
	// given back, which leaves three, and it is the *low* two that go:
	// the lead is the near end of a backwards run, and the anchor end
	// never moves.
	f.fillRetreatPermille = 2000;
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(0, out[2], "the leading end is given back");
	TEST_ASSERT_EQUAL_MESSAGE(0, out[3], "both of the leading LEDs");
	TEST_ASSERT_EQUAL_MESSAGE(100, out[4], "the rest of the run stays lit");
	TEST_ASSERT_EQUAL(100, out[5]);
	TEST_ASSERT_EQUAL_MESSAGE(100, out[6], "the anchor end never moves");
	// And a retreat the other way would have *lengthened* the run, which
	// is the mistake this direction check exists to catch.
	f.fillRetreatPermille = 0;
	frameLevels(f, out);
	TEST_ASSERT_EQUAL_MESSAGE(100, out[2], "unretracted, the run is whole");
}

static void test_a_console_wider_than_the_peak_still_gets_the_whole_block(void) {
	// The peak width is a cap on how fat the block gets, and a cap must
	// never make it narrower than the console it is landing on.
	//
	// The peak is 6 LEDs. A 39-LED console is wider than that, so the
	// block used to arrive as a 6-LED sliver sitting against the far end
	// of the window rather than covering it -- and *which* end depended
	// on the direction of travel, because the clamp pulls the left edge
	// towards the right one. Wide windows are not hypothetical: they are
	// what a strip laid out for big consoles has.
	const int peak = 6 * 1000;
	const int wide = 39 * 1000;

	// Landing on a window wider than the peak, travelling forwards.
	TravelEdges fwd = travelEdges(0, 2 * 1000, 55 * 1000, 55 * 1000 + wide,
								  1000, peak);
	TEST_ASSERT_EQUAL_MESSAGE(55 * 1000, fwd.leftPermille,
							  "the block must cover the whole window, forwards");
	TEST_ASSERT_EQUAL(55 * 1000 + wide, fwd.rightPermille);
	TEST_ASSERT_EQUAL_MESSAGE(wide, fwd.widthPermille,
							  "and be as wide as the window it lands on");

	// And backwards, where the clamp used to bias the error the other
	// way. Landing is exact either way now, which is the point.
	TravelEdges back = travelEdges(94 * 1000, 94 * 1000 + 2 * 1000,
								   55 * 1000, 55 * 1000 + wide, 1000, peak);
	TEST_ASSERT_EQUAL_MESSAGE(55 * 1000, back.leftPermille,
							  "the block must cover the whole window, backwards");
	TEST_ASSERT_EQUAL(55 * 1000 + wide, back.rightPermille);

	// A window narrower than the peak is untouched by the fix: the cap
	// still applies, and the block still lands exactly on it.
	TravelEdges narrow = travelEdges(0, 2 * 1000, 20 * 1000, 23 * 1000, 1000,
									 peak);
	TEST_ASSERT_EQUAL(20 * 1000, narrow.leftPermille);
	TEST_ASSERT_EQUAL(23 * 1000, narrow.rightPermille);
}

// A select config set up the way the twinkle tests want it: a
// thresholded twinkle, a fifth of the strip lit, re-rolling every 15ms.
//
// Built with the default constructor and named fields rather than the
// positional one -- that has seven arguments and a reader cannot hold
// them, and the new twinkle values are exactly the ones most likely to
// be retuned.
static SelectionEffectConfig twinkleConfig() {
	SelectionEffectConfig cfg;
	cfg.totalMs = 900;
	cfg.twinkleMs = 150;
	cfg.twinkleTickMs = 15;
	cfg.twinkleOnPct = 20;
	cfg.staggerMs = 6;
	cfg.twinkleMin = 0;
	cfg.twinkleMax = 100;
	cfg.abovePct = 22;
	cfg.selfPct = 100;
	return cfg;
}

static void test_the_twinkle_lights_a_few_pixels_rather_than_all_of_them(void) {
	// The difference between a twinkle and a shimmer. Scaling a random
	// brightness lit every pixel *a bit*, which reads as a broken strip;
	// thresholding it lights a fraction fully and leaves the rest
	// genuinely dark, which is what the eye reads as a strike.
	SelectionEffectConfig cfg = twinkleConfig();
	int lit = 0;
	for (int p = 0; p < 64; ++p) {
		const int level = twinkleLevel(p, 0, cfg);
		if (level == cfg.twinkleMax) {
			++lit;
		} else {
			TEST_ASSERT_EQUAL_MESSAGE(cfg.twinkleMin, level,
									 "a dark pixel must be dark, not dim");
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(lit > 0 && lit < 64,
		"a twinkle must light some of the strip and not all of it");
	// Roughly the configured fraction, not merely "some".
	TEST_ASSERT_TRUE_MESSAGE(lit >= 8 && lit <= 20,
		"about 20% of a 64-LED strip should be lit on a tick");
}

static void test_the_twinkle_off_fraction_means_off(void) {
	// 0% and 100% have to be exactly that. The cut is over 1024 rather
	// than 1023 so neither end can fall through to the other branch --
	// a "0% on" twinkle that lights a pixel or two reads as a fault.
	SelectionEffectConfig cfg = twinkleConfig();
	cfg.twinkleOnPct = 0;
	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(cfg.twinkleMin, twinkleLevel(p, 0, cfg),
								  "0% must light nothing at all");
	}
	cfg.twinkleOnPct = 100;
	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(cfg.twinkleMax, twinkleLevel(p, 0, cfg),
								  "100% must light everything");
	}
}

static void test_the_twinkle_re_rolls_on_its_own_cadence(void) {
	// The flicker rate is the effect. Held constant between ticks and
	// changing between them, at the configured interval -- otherwise the
	// tick is just a frame counter and the twinkle is a shimmer again.
	SelectionEffectConfig cfg = twinkleConfig();
	int changed = 0;
	for (int p = 0; p < 64; ++p) {
		const int first = twinkleLevel(p, 0, cfg);
		// Same tick, same answer: deterministic, or the host tests
		// could not assert exact frames.
		TEST_ASSERT_EQUAL(first, twinkleLevel(p, 0, cfg));
		// A tick's worth of time later, no change yet.
		TEST_ASSERT_EQUAL_MESSAGE(first, twinkleLevel(p, 0, cfg),
								  "a pixel must hold its state within a tick");
		// Several ticks on, something must have moved.
		for (int tick = 1; tick < 4; ++tick) {
			if (twinkleLevel(p, tick, cfg) != first) {
				++changed;
				break;
			}
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(changed > 32,
		"most of the strip must re-roll within a few ticks");
}

static void test_the_twinkle_is_visible_where_the_strip_rests_dark(void) {
	// The twinkle was invisible, and the reason is worth stating because
	// it is not where you would look: the brightness was computed
	// correctly and then discarded by the *colour*.
	//
	// The role decides the colour and OFF is black whatever the level
	// says, and the twinkle took its role from the resting picture. With
	// the stack no longer lit at rest, that is most of the strip -- gaps
	// included. So the twinkle had only ever been visible on top of the
	// lit stack, and turning the stack off at rest took the twinkle with
	// it.
	StripFrame f = configuredFrame(StripEffect::SELECTING);
	f.abovePct = LEDSTRING_ABOVE_PCT;   // 0: only the selection is lit
	f.selfPct = LEDSTRING_SELF_PCT;
	f.from = kSms;                       // the console being committed
	f.aboveCount = 0;
	f.select.totalMs = 900;
	f.select.twinkleMs = 150;
	f.select.twinkleTickMs = 15;
	f.select.twinkleOnPct = 20;
	f.select.twinkleMin = 0;
	f.select.twinkleMax = 100;
	f.elapsedMs = 0;

	StripPixel px[64];
	// configuredFrame() leaves the palette zeroed, which resolves every
	// role to black -- fine for level assertions, useless for "is this
	// pixel actually visible". Fill it in here rather than in the shared
	// fixture, so this test can prove visibility without changing what
	// every other test is asserting.
	for (int r = 0; r <= static_cast<int>(retroroom_core::LedRole::SELECTED); ++r) {
		const int v = 10 * (r + 1);
		f.palette.colors[r] = LedColor{v, v, v};
	}
	computeStripFrame(f, px);

	// Nothing outside SMS's window is lit at rest, so a twinkle pixel out
	// there is exactly the case that was being painted black.
	int litOutside = 0;
	for (int p = 0; p < 64; ++p) {
		if (p >= kSms.start && p < kSms.start + kSms.width) {
			continue;
		}
		if (px[p].level <= 0) {
			continue;
		}
		++litOutside;
		TEST_ASSERT_TRUE_MESSAGE(px[p].role != retroroom_core::LedRole::OFF,
			"a twinkle pixel outside every console must still carry a colour");
		const LedColor c = resolvePixel(f, px[p]);
		TEST_ASSERT_TRUE_MESSAGE(c.r + c.g + c.b > 0,
								  "and must resolve to something other than black");
	}
	TEST_ASSERT_TRUE_MESSAGE(litOutside > 8,
		"the twinkle is supposed to be reaching across the whole strip");
}

static void test_the_strike_gives_the_colour_back_as_it_settles(void) {
	// The rule that fixes the twinkle also has to hand the pixels back,
	// or the effect would not land on the resting paint: a gap pixel
	// would sit in the strike colour instead of going out.
	//
	// So the same pixel has to be the strike part way through and dark at
	// the end. That pair is the whole rule, and it is the thing worth
	// pinning -- asserting the *end* alone would pass even if the strike
	// colour simply never came back, because the end is unaffected.
	StripFrame f = configuredFrame(StripEffect::SELECTING);
	f.abovePct = LEDSTRING_ABOVE_PCT;
	f.selfPct = LEDSTRING_SELF_PCT;
	f.from = kSms;
	f.aboveCount = 0;
	f.select.totalMs = 900;
	f.select.twinkleMs = 150;
	f.select.twinkleTickMs = 15;
	f.select.twinkleOnPct = 20;
	f.select.twinkleMin = 0;
	f.select.twinkleMax = 100;

	// Half way through the effect, once the collapse is well under way.
	StripPixel mid[64];
	f.elapsedMs = 400;
	computeStripFrame(f, mid);
	int striking = 0;
	for (int p = 0; p < 64; ++p) {
		if (p >= kSms.start && p < kSms.start + kSms.width) {
			continue;
		}
		if (mid[p].role == retroroom_core::LedRole::SELECTED) {
			++striking;
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(striking > 0,
		"the strike should have reached beyond the console being committed");

	// And the end of the effect is the resting paint, with every one of
	// those pixels back to dark and holding their resting role. Scoped to
	// the pixels outside the committed console, because that one rests
	// lit at selfPct and asserting it is dark would be asserting the
	// resting picture is wrong.
	StripPixel end[64];
	f.elapsedMs = f.select.totalMs;
	computeStripFrame(f, end);
	for (int p = 0; p < 64; ++p) {
		if (p >= kSms.start && p < kSms.start + kSms.width) {
			continue;
		}
		TEST_ASSERT_EQUAL_MESSAGE(0, end[p].level,
								  "the effect must end on the resting paint");
		TEST_ASSERT_TRUE_MESSAGE(
			end[p].role != retroroom_core::LedRole::SELECTED,
			"no gap pixel may still be holding the strike colour");
	}
	// And the committed console is left lit, at its resting brightness.
	TEST_ASSERT_EQUAL_MESSAGE(LEDSTRING_SELF_PCT,
							  end[kSms.start].level,
							  "the committed console rests at the selection level");
}

static void test_a_twinkle_of_zero_is_just_the_collapse(void) {
	// The off switch, and the reason it needs a test of its own rather
	// than just a zero: with the twinkle off the effect must be a plain
	// collapse. It used to threshold a fresh sample as the collapse's
	// start, which lit a random fifth of the strip for one frame -- so
	// twinkleMs = 0 still twinked, exactly once, at the moment it was
	// supposed to be off.
	SelectionEffectConfig cfg = twinkleConfig();
	cfg.twinkleMs = 0;
	const int finalPct[8] = {0, 22, 100, 0, 0, 0, 0, 0};

	// Nothing ever gets brighter. That is the whole difference between a
	// twinkle and its absence, and the same check on a twinkling config
	// fails on the first re-roll.
	for (int p = 0; p < 8; ++p) {
		int prev = computeSelectScale(p, finalPct, 8, 0, cfg);
		for (std::uint32_t t = 1; t <= cfg.totalMs; t += 5) {
			const int now = computeSelectScale(p, finalPct, 8, t, cfg);
			TEST_ASSERT_TRUE_MESSAGE(now <= prev,
									  "with no twinkle a pixel must only ever dim");
			prev = now;
		}
	}
	// It still ends on the resting paint, so turning the twinkle off does
	// not turn the selection off.
	for (int p = 0; p < 8; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(finalPct[p],
								  computeSelectScale(p, finalPct, 8, cfg.totalMs, cfg),
								  "the collapse must still land on the resting paint");
	}
}

static void test_the_travel_is_the_colour_it_hands_over_to(void) {
	// The travel's last frame is the target window, and the frame after
	// it is that window pulsing as a proposal. A difference between the
	// two colours is a visible flash of a different hue at exactly the
	// moment the movement resolves into an answer.
	//
	// The values are compared rather than hard-coded, so retuning the
	// proposal does not silently reintroduce the flip.
	TEST_ASSERT_EQUAL_MESSAGE(LEDSTRING_COLOR_PROPOSAL_R,
							  LEDSTRING_COLOR_TRAVEL_R,
							  "the travel block must not change hue on handover");
	TEST_ASSERT_EQUAL(LEDSTRING_COLOR_PROPOSAL_G, LEDSTRING_COLOR_TRAVEL_G);
	TEST_ASSERT_EQUAL(LEDSTRING_COLOR_PROPOSAL_B, LEDSTRING_COLOR_TRAVEL_B);
	// And both have to be at the same *brightness* as the pulse peak,
	// or the handover is a step in luminance even with the hue fixed.
	StripFrame f = configuredFrame(StripEffect::PREVIEW);
	f.from = kNes;
	f.to = kNes;
	f.travelPct = LEDSTRING_SELF_PCT;
	f.pulseMaxPct = LEDSTRING_PREVIEW_PULSE_MAX_PCT;
	TEST_ASSERT_EQUAL_MESSAGE(f.pulseMaxPct, f.travelPct,
							  "the block must arrive at the pulse's peak brightness");
}

static void test_a_step_within_a_shelf_still_measures_the_gap(void) {
	// The guard on the rule above. Within a shelf the block enters at
	// the source's trailing edge, which lands exactly *on* the far edge
	// of the two consoles together -- so "entered from outside them" has
	// to be a strict test. Made non-strict, every ordinary backwards
	// step reads as a shelf crossing and fills over the console being
	// left on the way out of it.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.minFillLeds = 0;
	computeFillGeometry(kSms, kSms.start + kSms.width, kNes, 0, 64, f);
	TEST_ASSERT_EQUAL_MESSAGE(kSms.start, f.fillAnchor,
		"a backwards step still anchors on the source, not the entry");
	TEST_ASSERT_EQUAL_MESSAGE(kNes.start + kNes.width, f.fillLead,
		"and still runs to the far side of the target");
	TEST_ASSERT_FALSE(f.fillForward);
	// The forward case, for the same reason: its entry is short of the
	// target but still inside the two consoles, and it must stay the
	// gap between them rather than becoming a crossing.
	StripFrame g = configuredFrame(StripEffect::FILLING);
	g.minFillLeds = 0;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, g);
	TEST_ASSERT_EQUAL_MESSAGE(kNes.start + kNes.width, g.fillAnchor,
		"a forwards step still anchors just past the source");
	TEST_ASSERT_EQUAL_MESSAGE(kSms.start, g.fillLead,
		"and still stops just short of the target");
	TEST_ASSERT_TRUE(g.fillForward);
}

static void test_fill_uses_three_distinct_levels(void) {
	// Stack, fill and selection must not collapse into each other, or
	// "where the stack ends" and "how far I have got" become the same
	// fact and there is nothing to read progress from. The resting
	// brightness is 0 (only the selection is lit) and the fill is its
	// own level on top of that.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.dimPct = 22;
	f.fillPct = 45;
	f.minFillLeds = 0;
	f.from = kNes;
	f.activeWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	int out[64];
	frameLevels(f, out);
	TEST_ASSERT_EQUAL(22, out[1]);
	TEST_ASSERT_EQUAL_MESSAGE(45, out[5], "the fill is its own level");
	TEST_ASSERT_TRUE(22 != 45);
	TEST_ASSERT_TRUE(45 != 100);
}

// ---------------------------------------------------------------------------
// Roles and colours
// ---------------------------------------------------------------------------

// Roles per pixel, for the tests that care what a pixel is *for* rather
// than how bright.
static void frameRoles(const StripFrame& f, retroroom_core::LedRole* out) {
	retroroom_core::StripPixel px[64] = {};
	computeStripFrame(f, px);
	for (int i = 0; i < 64; ++i) {
		out[i] = px[i].role;
	}
}

static void test_the_resting_frame_distinguishes_selection_from_stack(void) {
	// The reason colours exist: the stack and the selection are both
	// lit, and at the same brightness they would be two identical blocks
	// of light.
	retroroom_core::LedRole r[64];
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.from = computeConsoleWindow(27, 15, 64);  // MAME
	f.aboveWindows = 0;
	f.aboveCount = 0;
	frameRoles(f, r);
	TEST_ASSERT_TRUE(r[5] == retroroom_core::LedRole::STACK ||
					 r[5] == retroroom_core::LedRole::OFF);
	TEST_ASSERT_TRUE_MESSAGE(r[30] == retroroom_core::LedRole::SELECTED,
		"the selection must be its own role");
	TEST_ASSERT_TRUE(r[50] == retroroom_core::LedRole::OFF);
}

static void test_the_fill_and_the_console_left_are_different_roles(void) {
	// Two different things, both lit, at the same time. The console
	// being played is context; the fill is progress.
	retroroom_core::LedRole r[64];
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.from = kSms;
	f.activeWindow = kSms;
	computeFillGeometry(kSms, kSms.start + kSms.width, kNes, 0, 64, f);
	f.fillLead = scaleFillLead(f.fillAnchor, f.fillLead, f.fillForward, 1000);
	frameRoles(f, r);
	TEST_ASSERT_TRUE_MESSAGE(r[7] == retroroom_core::LedRole::LEAVING,
		"the console being played is context, not progress");
	TEST_ASSERT_TRUE(r[3] == retroroom_core::LedRole::FILL);
	TEST_ASSERT_TRUE(r[12] == retroroom_core::LedRole::OFF);
}

static void test_the_preview_keeps_what_it_is_comparing_against(void) {
	// The proposal pulses with the consoles above it still visible. If
	// they were not, there is nothing on the strip to compare it
	// against and the preview is just a light with no context.
	retroroom_core::LedRole r[64];
	StripFrame f = configuredFrame(StripEffect::PREVIEW);
	f.to = kSms;
	f.aboveWindows = 0;
	f.aboveCount = 0;
	frameRoles(f, r);
	TEST_ASSERT_TRUE(r[8] == retroroom_core::LedRole::PROPOSAL);
	TEST_ASSERT_TRUE(r[50] == retroroom_core::LedRole::OFF);
}

static void test_resolve_pixel_applies_the_role_colour_and_level(void) {
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.palette.colors[static_cast<int>(retroroom_core::LedRole::SELECTED)] =
		{200, 100, 50};
	retroroom_core::StripPixel p;
	p.role = retroroom_core::LedRole::SELECTED;
	p.level = 100;
	const retroroom_core::LedColor full = resolvePixel(f, p);
	TEST_ASSERT_EQUAL(200, full.r);
	TEST_ASSERT_EQUAL(100, full.g);
	TEST_ASSERT_EQUAL(50, full.b);
	p.level = 50;
	const retroroom_core::LedColor half = resolvePixel(f, p);
	TEST_ASSERT_EQUAL(100, half.r);
	TEST_ASSERT_EQUAL(50, half.g);
	TEST_ASSERT_EQUAL(25, half.b);
}

static void test_resolve_pixel_clamps_rather_than_wrapping(void) {
	// An over-100 level must not roll a channel over into another
	// primary -- that would turn a brightness tweak into a colour
	// change, silently.
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.palette.colors[static_cast<int>(retroroom_core::LedRole::SELECTED)] =
		{200, 100, 50};
	retroroom_core::StripPixel p;
	p.role = retroroom_core::LedRole::SELECTED;
	p.level = 250;
	const retroroom_core::LedColor c = resolvePixel(f, p);
	TEST_ASSERT_EQUAL(200, c.r);
	TEST_ASSERT_EQUAL(100, c.g);
	TEST_ASSERT_EQUAL(50, c.b);
}

static void test_resolve_pixel_gives_black_to_an_unlit_pixel(void) {
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.palette.colors[static_cast<int>(retroroom_core::LedRole::OFF)] =
		{255, 255, 255};
	retroroom_core::StripPixel p;
	p.role = retroroom_core::LedRole::OFF;
	p.level = 100;
	const retroroom_core::LedColor off = resolvePixel(f, p);
	TEST_ASSERT_EQUAL(0, off.r);
	TEST_ASSERT_EQUAL(0, off.g);
	TEST_ASSERT_EQUAL(0, off.b);
	// A lit role at zero level is black too.
	p.role = retroroom_core::LedRole::FILL;
	p.level = 0;
	const retroroom_core::LedColor none = resolvePixel(f, p);
	TEST_ASSERT_EQUAL(0, none.r);
	TEST_ASSERT_EQUAL(0, none.g);
	TEST_ASSERT_EQUAL(0, none.b);
}

static void test_only_the_active_console_and_the_candidate_are_lit(void) {
	// The rule from the bench, twice over:
	//
	//   - whatever the operator is being shown, the console they are
	//     actually playing stays lit and dim. It used to be the console
	//     the browse *departed from*, which is the same thing for the
	//     first step and a different thing for every step after it, so
	//     keeping the knob moving handed the dim role to the candidate
	//     and the console being played went dark.
	//
	//   - the candidate keeps pulsing while the operator fills onward
	//     past it, because it is still what a press would select. It
	//     used to become part of the fill, so the pulse vanished one
	//     detent after it appeared.
	//
	// SMS is active; NES is the candidate; the fill runs toward SMS.
	const LedRange active = computeConsoleWindow(5, 3, 64);     // SMS
	const LedRange candidate = computeConsoleWindow(1, 1, 64);  // NES
	const LedRange target = active;
	retroroom_core::LedRole r[64];

	StripFrame fill = configuredFrame(StripEffect::FILLING);
	fill.from = candidate;
	fill.activeWindow = active;
	fill.candidateWindow = candidate;
	computeFillGeometry(candidate, candidate.start + candidate.width, target, 0,
						64, fill);
	fill.fillLead = scaleFillLead(fill.fillAnchor, fill.fillLead, fill.fillForward,
								 1000);
	frameRoles(fill, r);
	TEST_ASSERT_TRUE_MESSAGE(r[5] == retroroom_core::LedRole::LEAVING,
		"the console being played stays lit through the fill");
	TEST_ASSERT_TRUE_MESSAGE(r[1] == retroroom_core::LedRole::PROPOSAL,
		"the candidate keeps pulsing while the operator turns past it");
	// And the fill is the fill, not either of them.
	int fillPixels = 0;
	for (int p = 0; p < 64; ++p) {
		if (r[p] == retroroom_core::LedRole::FILL) fillPixels++;
	}
	TEST_ASSERT_TRUE_MESSAGE(fillPixels > 0, "the fill must be its own thing");
	// Nothing else is lit.
	TEST_ASSERT_TRUE(r[40] == retroroom_core::LedRole::OFF);

	StripFrame preview = configuredFrame(StripEffect::PREVIEW);
	preview.from = candidate;
	preview.to = candidate;
	preview.activeWindow = active;
	frameRoles(preview, r);
	TEST_ASSERT_TRUE(r[1] == retroroom_core::LedRole::PROPOSAL);
	TEST_ASSERT_TRUE_MESSAGE(r[5] == retroroom_core::LedRole::LEAVING,
		"the proposal never replaces the console being played as the lit one");
}

static void test_a_fill_frame_depends_on_the_clock(void) {
	// The premise behind repainting a fill on the frame clock instead of
	// only on a detent.
	//
	// The fill's *length* is the detent gate's position, and that really
	// is static between clicks -- which is why the loop used to treat
	// FILLING as a no-op. But the candidate behind it is pulsing, and
	// its level is a function of elapsedMs. A loop that only repaints on
	// a detent therefore freezes the pulse at whatever level the last
	// click happened to land on, and the console stops breathing the
	// moment the operator touches the knob.
	//
	// So the test is not "does the fill move", it is "does anything in
	// this frame move with time" -- and exactly one thing does.
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.from = kNes;
	f.activeWindow = kSms;
	f.candidateWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	f.elapsedMs = 0;
	int dim[64];
	frameLevels(f, dim);

	// Half a pulse period later: the other end of the breath. The
	// configured pulse is 30..100 over 1100ms, so 0 is the dim end and
	// 550 the bright end.
	f.elapsedMs = 550;
	int bright[64];
	frameLevels(f, bright);
	TEST_ASSERT_TRUE_MESSAGE(dim[1] != bright[1],
		"the candidate must breathe between two frames of the same fill");
	TEST_ASSERT_EQUAL(computePulseScale(0, 1100, 30, 100), dim[1]);
	TEST_ASSERT_EQUAL(computePulseScale(550, 1100, 30, 100), bright[1]);
	// Nothing else in the frame moves with time, which is what keeps the
	// detent-driven length working.
	for (int p = 0; p < 64; ++p) {
		if (p == 1) {
			continue;
		}
		TEST_ASSERT_TRUE_MESSAGE(dim[p] == bright[p],
			"only the candidate may change with the clock");
	}
}

static void test_a_fill_with_no_candidate_does_not_pulse(void) {
	// Before the first snap the anchor is the console already selected,
	// and that one is shown as selected, not offered as a proposal. A
	// frame with no candidate window must draw nothing pulsing.
	retroroom_core::LedRole r[64];
	StripFrame f = configuredFrame(StripEffect::FILLING);
	f.from = kNes;
	f.activeWindow = kNes;
	computeFillGeometry(kNes, kNes.start + kNes.width, kSms, 0, 64, f);
	frameRoles(f, r);
	TEST_ASSERT_TRUE(r[1] == retroroom_core::LedRole::LEAVING);
	TEST_ASSERT_TRUE_MESSAGE(r[7] != retroroom_core::LedRole::PROPOSAL,
		"the selected console is not also a proposal");
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();

	// Detent gating.
	RUN_TEST(test_gate_needs_five_detents_for_a_deliberate_step);
	RUN_TEST(test_gate_snaps_back_to_the_anchor_after_advancing);
	RUN_TEST(test_gate_escalates_to_two_detents_when_spinning);
	RUN_TEST(test_fast_spin_completes_a_step_in_two_detents);
	RUN_TEST(test_slowing_back_down_returns_to_the_deliberate_cadence);
	RUN_TEST(test_zero_window_never_escalates);
	RUN_TEST(test_reversing_walks_the_position_back);
	RUN_TEST(test_crossing_the_anchor_flips_the_target_side);
	RUN_TEST(test_backward_step_completes_and_reports_direction);
	RUN_TEST(test_thresholds_that_do_not_divide_1000_still_arrive);
	RUN_TEST(test_config_is_clamped_to_something_usable);
	RUN_TEST(test_non_detent_direction_is_a_no_op);
	RUN_TEST(test_gate_freezes_at_the_end_of_the_list);
	RUN_TEST(test_gate_freezes_at_the_start_of_the_list);
	RUN_TEST(test_gate_unfreezes_once_turned_back);
	RUN_TEST(test_gate_freeze_does_not_wreck_the_fast_mode_clock);
	RUN_TEST(test_gate_on_an_empty_list_always_freezes);

	// Blob travel.
	RUN_TEST(test_ease_endpoints_and_midpoint);
	RUN_TEST(test_blob_sits_on_the_anchor_at_progress_zero);
	RUN_TEST(test_blob_lands_centred_on_the_target);
	RUN_TEST(test_blob_moves_forward_monotonically);
	RUN_TEST(test_blob_never_leaves_the_strip);
	RUN_TEST(test_blob_wider_than_the_strip_is_truncated);
	RUN_TEST(test_blob_with_an_unmapped_console_still_travels);
	RUN_TEST(test_zero_total_leds_paints_nothing);

	// Preview pulse.
	RUN_TEST(test_pulse_stays_inside_its_bounds);
	RUN_TEST(test_pulse_actually_varies);
	RUN_TEST(test_pulse_is_periodic);
	RUN_TEST(test_pulse_with_zero_period_is_fully_on);

	// Selection effect.
	RUN_TEST(test_keep_end_inclusive_covers_the_selected_console);
	RUN_TEST(test_keep_end_exclusive_stops_above);
	RUN_TEST(test_keep_end_clamps_to_the_strip);
	RUN_TEST(test_select_effect_is_finished_inside_its_budget);
	RUN_TEST(test_select_lands_exactly_on_its_target);
	RUN_TEST(test_select_effect_is_over_a_second_is_unreachable);
	RUN_TEST(test_select_starts_by_twinkling_the_whole_strip);
	RUN_TEST(test_select_surviving_pixels_never_go_dark);
	RUN_TEST(test_select_expires_early_for_the_top_of_the_strip);
	RUN_TEST(test_zero_length_effect_settles_immediately);
	RUN_TEST(test_twinkle_is_deterministic_and_varies_per_pixel);

	// Whole-strip frames.
	RUN_TEST(test_frame_clears_every_pixel_first);
	RUN_TEST(test_travel_ends_exactly_on_the_target_window);
	RUN_TEST(test_travel_starts_on_the_console_being_leaving);
	RUN_TEST(test_travel_keeps_the_fill_ahead_of_it);
	RUN_TEST(test_travel_backwards_does_not_invert_the_block);
	RUN_TEST(test_travel_consumes_the_fill_behind_it);
	RUN_TEST(test_travel_stretches_then_settles);
	RUN_TEST(test_travel_sweeps_a_shelf_for_a_shelf_crossing);
	RUN_TEST(test_coverage_is_exact_at_the_edges);
	RUN_TEST(test_fill_run_is_the_gap_between_the_windows);
	RUN_TEST(test_fill_run_has_a_floor);
	RUN_TEST(test_fill_lead_moves_towards_the_target);
	RUN_TEST(test_fill_does_not_light_pixels_below_the_console);
	RUN_TEST(test_fill_uses_three_distinct_levels);
	RUN_TEST(test_the_resting_frame_distinguishes_selection_from_stack);
	RUN_TEST(test_the_fill_and_the_console_left_are_different_roles);
	RUN_TEST(test_the_preview_keeps_what_it_is_comparing_against);
	RUN_TEST(test_resolve_pixel_applies_the_role_colour_and_level);
	RUN_TEST(test_resolve_pixel_clamps_rather_than_wrapping);
	RUN_TEST(test_resolve_pixel_gives_black_to_an_unlit_pixel);
	RUN_TEST(test_only_the_active_console_and_the_candidate_are_lit);
	RUN_TEST(test_a_fill_frame_depends_on_the_clock);
	RUN_TEST(test_a_fill_with_no_candidate_does_not_pulse);
	RUN_TEST(test_fill_runs_backwards_when_browsing_back);
	RUN_TEST(test_fill_never_lights_the_target_window);
	RUN_TEST(test_shelf_crossing_sweeps_the_whole_destination_shelf);
	RUN_TEST(test_a_return_across_the_bridge_sweeps_the_shelf_too);
	RUN_TEST(test_the_block_enters_on_the_edge_it_departs_by);
	RUN_TEST(test_the_block_starts_where_its_own_fill_starts);
	RUN_TEST(test_the_backward_block_no_longer_starts_right_of_the_console);
	RUN_TEST(test_a_spark_centred_on_the_first_led_is_safe);
	RUN_TEST(test_the_retreat_shortens_the_run_from_the_front);
	RUN_TEST(test_the_retreat_fades_rather_than_snapping);
	RUN_TEST(test_a_retreat_longer_than_the_run_empties_it);
	RUN_TEST(test_the_retreat_applies_at_the_low_end_going_back);
	RUN_TEST(test_the_twinkle_lights_a_few_pixels_rather_than_all_of_them);
	RUN_TEST(test_the_twinkle_off_fraction_means_off);
	RUN_TEST(test_the_twinkle_re_rolls_on_its_own_cadence);
	RUN_TEST(test_the_twinkle_is_visible_where_the_strip_rests_dark);
	RUN_TEST(test_the_strike_gives_the_colour_back_as_it_settles);
	RUN_TEST(test_a_twinkle_of_zero_is_just_the_collapse);
	RUN_TEST(test_the_travel_is_the_colour_it_hands_over_to);
	RUN_TEST(test_a_console_wider_than_the_peak_still_gets_the_whole_block);
	RUN_TEST(test_a_step_within_a_shelf_still_measures_the_gap);
	RUN_TEST(test_fill_uses_three_distinct_levels);
	RUN_TEST(test_resting_frame_lights_each_window_above);
	RUN_TEST(test_resting_frame_with_zero_above_lights_only_the_selection);
	RUN_TEST(test_resting_frame_tolerates_a_null_above_list);
	RUN_TEST(test_resting_frame_lights_each_window_above);
	RUN_TEST(test_resting_frame_with_zero_above_lights_only_the_selection);
	RUN_TEST(test_resting_frame_tolerates_a_null_above_list);
	RUN_TEST(test_resting_frame_splits_prefix_from_selection);
	RUN_TEST(test_collect_above_picks_only_earlier_windows);
	RUN_TEST(test_collect_above_preserves_the_windows_it_keeps);
	RUN_TEST(test_collect_above_skips_a_straddling_window);
	RUN_TEST(test_collect_above_is_defensive_about_bad_arguments);
	RUN_TEST(test_collect_above_picks_only_earlier_windows);
	RUN_TEST(test_collect_above_preserves_the_windows_it_keeps);
	RUN_TEST(test_collect_above_skips_a_straddling_window);
	RUN_TEST(test_collect_above_is_defensive_about_bad_arguments);
	RUN_TEST(test_preview_frame_pulses_only_the_target);
	RUN_TEST(test_selecting_frame_ends_on_the_resting_paint);
	RUN_TEST(test_frame_rejects_a_bad_strip_size);
	RUN_TEST(test_frame_clamps_windows_that_overrun_the_strip);

	return UNITY_END();
}
