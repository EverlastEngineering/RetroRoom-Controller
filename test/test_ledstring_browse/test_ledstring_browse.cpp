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

#include <LedStringPaint.h>
#include <unity.h>

using retroroom_core::computeBlobWindow;
using retroroom_core::computeKeepEnd;
using retroroom_core::computePulseScale;
using retroroom_core::computeSelectScale;
using retroroom_core::DetentGate;
using retroroom_core::DetentGateConfig;
using retroroom_core::easeInOutPermille;
using retroroom_core::LedRange;
using retroroom_core::SelectionEffectConfig;
using retroroom_core::twinkleSample;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// DetentGate
// ---------------------------------------------------------------------------

// One millisecond between detents is well inside the 1000 ms fast-spin
// window, so these tests run in "deliberate" territory until they say
// otherwise. Slower detents are spelled out per test.
static const std::uint32_t kSlowGapMs = 2000;

static void test_gate_needs_five_detents_for_a_deliberate_step(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));

	for (int i = 1; i <= 4; ++i) {
		retroroom_core::DetentEvent ev = gate.onDetent(1, i * kSlowGapMs);
		TEST_ASSERT_FALSE_MESSAGE(ev.advanced, "advanced before the 5th detent");
		TEST_ASSERT_EQUAL(i, ev.detents);
		TEST_ASSERT_FALSE(ev.fastMode);
		TEST_ASSERT_EQUAL(5, ev.detentsPerStep);
	}
	retroroom_core::DetentEvent ev = gate.onDetent(1, 5 * kSlowGapMs);
	TEST_ASSERT_TRUE_MESSAGE(ev.advanced, "5th detent must complete the step");
	TEST_ASSERT_EQUAL(1, ev.targetDirection);
	TEST_ASSERT_EQUAL(0, ev.fractionPermille);
}

static void test_gate_snaps_back_to_the_anchor_after_advancing(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 5; ++i) {
		t += kSlowGapMs;
		gate.onDetent(1, t);
	}
	TEST_ASSERT_EQUAL(0, gate.fractionPermille());
	// The next step starts from scratch, not from where the last one
	// finished.
	t += kSlowGapMs;
	gate.onDetent(1, t);
	TEST_ASSERT_EQUAL(200, gate.fractionPermille());
}

static void test_gate_escalates_to_two_detents_when_spinning(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));

	uint32_t t = 0;
	// Deliberate first, so the first detent can't self-trigger fast mode.
	t += kSlowGapMs;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t);
	TEST_ASSERT_FALSE(ev.fastMode);

	// Now spin: 20 ms apart, well inside the window.
	for (int i = 0; i < 4; ++i) {
		t += 20;
		ev = gate.onDetent(1, t);
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
	gate.onDetent(1, t);
	t += 20;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t);
	if (!ev.advanced) {
		t += 20;
		ev = gate.onDetent(1, t);
	}
	TEST_ASSERT_TRUE_MESSAGE(ev.advanced, "fast spin must not need 5 detents");
}

static void test_slow_spin_after_a_fast_one_stays_deliberate(void) {
	// Fast mode is sticky: once the operator has shown they are in a
	// hurry, a slightly longer gap does not put the slow cadence back.
	// It is cleared by reset(), which is what the abandoned-browse and
	// commit paths do.
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	t += 20;
	gate.onDetent(1, t);
	t += 500;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t);
	TEST_ASSERT_TRUE(ev.fastMode);

	gate.reset();
	t += 5000;
	ev = gate.onDetent(1, t);
	TEST_ASSERT_FALSE(ev.fastMode);
	TEST_ASSERT_EQUAL(5, ev.detentsPerStep);
}

static void test_reversing_walks_the_position_back(void) {
	DetentGate gate;
	gate.configure(DetentGateConfig(5, 2, 1000));
	uint32_t t = 0;
	for (int i = 0; i < 3; ++i) {
		t += kSlowGapMs;
		gate.onDetent(1, t);
	}
	const int forward = gate.fractionPermille();
	TEST_ASSERT_TRUE(forward > 0);

	// Turn the knob back. The blob must retreat along the same path
	// toward the same side, not jump to the other console.
	t += kSlowGapMs;
	retroroom_core::DetentEvent ev = gate.onDetent(-1, t);
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
	ev = gate.onDetent(1, t);
	TEST_ASSERT_EQUAL(200, ev.fractionPermille);
	TEST_ASSERT_EQUAL(1, ev.targetDirection);

	t += kSlowGapMs;
	ev = gate.onDetent(1, t);
	TEST_ASSERT_EQUAL(400, ev.fractionPermille);
	TEST_ASSERT_EQUAL(1, ev.targetDirection);

	t += kSlowGapMs;
	ev = gate.onDetent(-1, t);
	TEST_ASSERT_EQUAL(200, ev.fractionPermille);
	TEST_ASSERT_EQUAL_MESSAGE(1, ev.targetDirection, "still short of the anchor");

	t += kSlowGapMs;
	ev = gate.onDetent(-1, t);
	TEST_ASSERT_EQUAL(0, ev.fractionPermille);
	TEST_ASSERT_EQUAL_MESSAGE(-1, ev.targetDirection,
		"back on the anchor and turning back, so the target flips sides");

	t += kSlowGapMs;
	ev = gate.onDetent(-1, t);
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
		ev = gate.onDetent(-1, t);
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
			advanced = gate.onDetent(1, t).advanced;
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
	gate.onDetent(1, 1000);
	const int before = gate.fractionPermille();
	retroroom_core::DetentEvent ev = gate.onDetent(0, 1200);
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

static void test_select_effect_is_finished_inside_its_budget(void) {
	// The user-visible contract: under a second, every pixel settled.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22);
	const int keepEnd = computeKeepEnd(27, 15, 64, true);
	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(p < keepEnd ? 22 : 0,
			computeSelectScale(p, keepEnd, 64, cfg.totalMs, cfg),
			"pixel must be at its final value by totalMs");
	}
}

static void test_select_effect_is_over_a_second_is_unreachable(void) {
	// Guards the headline requirement rather than the config: whatever
	// the shell passes, nothing is still ramping past a second.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22);
	const int keepEnd = computeKeepEnd(27, 15, 64, true);
	for (std::uint32_t t = 1000; t < 2000; t += 50) {
		for (int p = 0; p < 64; ++p) {
			TEST_ASSERT_EQUAL(p < keepEnd ? 22 : 0,
							  computeSelectScale(p, keepEnd, 64, t, cfg));
		}
	}
}

static void test_select_starts_by_twinkling_the_whole_strip(void) {
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22);
	// A pixel that is destined to be dark is still lit during the
	// twinkle -- that is the "entire LED string twinkles" moment.
	int litBelow = 0, litAbove = 0;
	for (int p = 0; p < 64; ++p) {
		if (p > 42) {
			// Destined to be dark.
			if (computeSelectScale(p, 42, 64, 0, cfg) > 0) litBelow++;
		} else if (computeSelectScale(p, 42, 64, 0, cfg) > 0) {
			litAbove++;
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(litAbove > 0, "strip must be lit during the twinkle");
	TEST_ASSERT_TRUE_MESSAGE(litBelow > 0, "whole strip twinkles, not just the prefix");
}

static void test_select_surviving_pixels_never_go_dark(void) {
	// The prefix must not flicker to black on the way to settling or
	// the collapse reads as a dropout.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 100, 22);
	for (int p = 0; p < 42; ++p) {
		for (std::uint32_t t = 0; t < 900; t += 5) {
			TEST_ASSERT_TRUE_MESSAGE(computeSelectScale(p, 42, 64, t, cfg) > 0,
				"a surviving pixel must stay lit for the whole effect");
		}
	}
}

static void test_select_expires_early_for_the_top_of_the_strip(void) {
	// The stagger is per pixel index, so pixel 0 starts settling first
	// and is done before the bottom of the strip. That ripple is the
	// whole point of the stagger.
	//
	// keepScale 77 is deliberately outside the twinkle range (12..60)
	// so "is this pixel settled yet?" is unambiguous -- a twinkle
	// sample can never be mistaken for a final value of 77 or 0.
	const SelectionEffectConfig cfg(900, 350, 6, 12, 60, 77);
	const int keepEnd = 42;
	int firstSettled = -1;
	int lastUnsettled = -1;
	for (int p = 0; p < 64; ++p) {
		const int finalScale = (p < keepEnd) ? 77 : 0;
		if (computeSelectScale(p, keepEnd, 64, cfg.totalMs, cfg) != finalScale) {
			continue;  // not settled at the end; can't be settled earlier
		}
		if (computeSelectScale(p, keepEnd, 64, cfg.twinkleMs + 200, cfg) ==
			finalScale) {
			if (firstSettled < 0) firstSettled = p;
		} else if (lastUnsettled < 0 || p > lastUnsettled) {
			// Track the highest index still mid-transition.
			if (computeSelectScale(p, keepEnd, 64, cfg.totalMs, cfg) == finalScale) {
				lastUnsettled = p;
			}
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(firstSettled >= 0,
		"the top of the strip must be settled well before the effect ends");
	TEST_ASSERT_TRUE_MESSAGE(firstSettled <= 20,
		"the ripple must reach the top of the strip early");
	TEST_ASSERT_TRUE_MESSAGE(lastUnsettled > firstSettled,
		"pixels below the ripple front must still be settling");
}

static void test_zero_length_effect_settles_immediately(void) {
	const SelectionEffectConfig cfg(0, 0, 6, 12, 100, 22);
	TEST_ASSERT_EQUAL(22, computeSelectScale(10, 42, 64, 0, cfg));
	TEST_ASSERT_EQUAL(0, computeSelectScale(50, 42, 64, 0, cfg));
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

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();

	// Detent gating.
	RUN_TEST(test_gate_needs_five_detents_for_a_deliberate_step);
	RUN_TEST(test_gate_snaps_back_to_the_anchor_after_advancing);
	RUN_TEST(test_gate_escalates_to_two_detents_when_spinning);
	RUN_TEST(test_fast_spin_completes_a_step_in_two_detents);
	RUN_TEST(test_slow_spin_after_a_fast_one_stays_deliberate);
	RUN_TEST(test_reversing_walks_the_position_back);
	RUN_TEST(test_crossing_the_anchor_flips_the_target_side);
	RUN_TEST(test_backward_step_completes_and_reports_direction);
	RUN_TEST(test_thresholds_that_do_not_divide_1000_still_arrive);
	RUN_TEST(test_config_is_clamped_to_something_usable);
	RUN_TEST(test_non_detent_direction_is_a_no_op);

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
	RUN_TEST(test_select_effect_is_over_a_second_is_unreachable);
	RUN_TEST(test_select_starts_by_twinkling_the_whole_strip);
	RUN_TEST(test_select_surviving_pixels_never_go_dark);
	RUN_TEST(test_select_expires_early_for_the_top_of_the_strip);
	RUN_TEST(test_zero_length_effect_settles_immediately);
	RUN_TEST(test_twinkle_is_deterministic_and_varies_per_pixel);

	return UNITY_END();
}
