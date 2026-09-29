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
using retroroom_core::collectAboveWindows;
using retroroom_core::computeConsoleWindow;
using retroroom_core::computeStripFrame;
using retroroom_core::StripEffect;
using retroroom_core::StripFrame;

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
	gate.onDetent(1, t);
	t += 20;
	retroroom_core::DetentEvent ev = gate.onDetent(1, t);
	TEST_ASSERT_TRUE_MESSAGE(ev.fastMode, "a tight gap must read as fast");

	// Now turn deliberately.
	t += 2000;
	ev = gate.onDetent(1, t);
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
		const retroroom_core::DetentEvent ev = gate.onDetent(1, t);
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
		TEST_ASSERT_FALSE(slow.onDetent(1, t).advanced);
	}
	t += 5;
	TEST_ASSERT_TRUE(slow.onDetent(1, t).advanced);
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
	computeStripFrame(f, out);
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
		computeStripFrame(f, out);

	TEST_ASSERT_EQUAL(0, out[0]);
	TEST_ASSERT_EQUAL(0, out[26]);
	TEST_ASSERT_EQUAL(100, out[27]);
	TEST_ASSERT_EQUAL(100, out[41]);
	TEST_ASSERT_EQUAL(0, out[42]);
	TEST_ASSERT_EQUAL(0, out[63]);
}

static void test_transit_frame_draws_both_windows_and_the_blob(void) {
	int out[64];
	StripFrame f = configuredFrame(StripEffect::TRANSIT);
	f.from = computeConsoleWindow(1, 1, 64);   // NES at [1,2)
	f.to = computeConsoleWindow(7, 5, 64);     // SMS at [7,12)
	f.fractionPermille = 1000;                 // blob arrived
	computeStripFrame(f, out);

	TEST_ASSERT_EQUAL(100, out[9]);  // blob centre of [7,12) is 9
	TEST_ASSERT_EQUAL(100, out[8]);
	TEST_ASSERT_EQUAL(100, out[10]);
	TEST_ASSERT_EQUAL(0, out[20]);
	// Both windows are drawn dim; the NES window is not under the blob
	// at full progress, so it is still readable as a dim mark.
	TEST_ASSERT_EQUAL(25, out[1]);
}

static void test_transit_frame_travels_backwards_too(void) {
	// Regression. The gate's position is signed to say which side of
	// the anchor the operator is on, and the frame is handed the target
	// window that side. The travel itself is the *magnitude*. Clamping
	// the sign instead of taking the magnitude eased every negative
	// position to 0, which froze the blob on the anchor for the whole
	// backward half of a step -- it only showed up in the simulator's
	// reverse scenario, not in any of the blob unit tests, because those
	// all pass a non-negative fraction.
	int forward[64];
	int backward[64];

	StripFrame fwd = configuredFrame(StripEffect::TRANSIT);
	fwd.from = computeConsoleWindow(1, 1, 64);
	fwd.to = computeConsoleWindow(7, 5, 64);
	fwd.fractionPermille = 400;
	computeStripFrame(fwd, forward);

	// Same step seen from the other side of the anchor: the caller has
	// already chosen the other neighbour as `to`.
	StripFrame bwd = configuredFrame(StripEffect::TRANSIT);
	bwd.from = computeConsoleWindow(1, 1, 64);
	bwd.to = computeConsoleWindow(27, 15, 64);
	bwd.fractionPermille = -400;
	computeStripFrame(bwd, backward);

	int blobStart = -1;
	for (int p = 0; p < 64; ++p) {
		if (backward[p] == 100) {
			blobStart = p;
			break;
		}
	}
	TEST_ASSERT_TRUE_MESSAGE(blobStart >= 0, "a blob must be drawn");
	TEST_ASSERT_TRUE_MESSAGE(blobStart > 3,
		"a backward position must move the blob away from the anchor");
	// The two frames share a departure window but different targets, so
	// the blob cannot be in the same place in both.
	TEST_ASSERT_NOT_EQUAL(blobStart, forward[1] == 100 ? 1 : -2);
}

static void test_blob_wins_over_the_windows_it_crosses(void) {
	// The layering is departure, then destination, then blob. If the
	// blob were drawn first it would be painted over by a wide
	// destination window and the travelling object would disappear
	// part-way across.
	int out[64];
	StripFrame f = configuredFrame(StripEffect::TRANSIT);
	f.from = computeConsoleWindow(1, 1, 64);
	f.to = computeConsoleWindow(7, 5, 64);
	f.fractionPermille = 0;  // blob still sitting on the departure
	computeStripFrame(f, out);
	for (int p = 0; p < 3; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(100, out[p], "blob must be on top at progress 0");
	}
}

static void test_preview_frame_pulses_only_the_target(void) {
	int out[64];
	StripFrame f = configuredFrame(StripEffect::PREVIEW);
	f.to = computeConsoleWindow(7, 5, 64);
	f.from = computeConsoleWindow(1, 1, 64);  // must be ignored
	f.elapsedMs = 0;
	computeStripFrame(f, out);
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
	computeStripFrame(s, selecting);

	StripFrame r = configuredFrame(StripEffect::RESTING);
	r.from = s.from;
	r.aboveWindows = above;
	r.aboveCount = 1;
	computeStripFrame(r, resting);

	for (int p = 0; p < 64; ++p) {
		TEST_ASSERT_EQUAL_MESSAGE(resting[p], selecting[p],
			"selection effect must end on the resting paint");
	}
}


static void test_frame_rejects_a_bad_strip_size(void) {
	int out[4] = {9, 9, 9, 9};
	StripFrame f = configuredFrame(StripEffect::RESTING);
	f.totalLeds = 0;
	computeStripFrame(f, out);
	for (int i = 0; i < 4; ++i) {
		TEST_ASSERT_EQUAL_MESSAGE(9, out[i],
			"a zero-sized strip must not write into the caller's buffer");
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
	computeStripFrame(f, out);
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
	computeStripFrame(f, out);

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
	computeStripFrame(f, out);
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
	computeStripFrame(f, out);
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
	RUN_TEST(test_transit_frame_draws_both_windows_and_the_blob);
	RUN_TEST(test_blob_wins_over_the_windows_it_crosses);
	RUN_TEST(test_transit_frame_travels_backwards_too);
	RUN_TEST(test_preview_frame_pulses_only_the_target);
	RUN_TEST(test_selecting_frame_ends_on_the_resting_paint);
	RUN_TEST(test_frame_rejects_a_bad_strip_size);
	RUN_TEST(test_frame_clamps_windows_that_overrun_the_strip);

	return UNITY_END();
}
