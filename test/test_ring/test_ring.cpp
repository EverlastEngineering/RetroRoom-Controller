// Host-side Unity tests for the selector ring state machine in
// lib/RingPaint.
//
// The shell that drives this is src/lighting.cpp, which needs FastLED +
// Arduino and so cannot run on the host. Everything here is the
// *decision* half -- which mode the ring is in, how bright it is, and
// when it changes. The shell's only remaining job is to copy levels into
// CRGB pixels and call show().
//
// These are the properties the old src/lighting.cpp could not express,
// because it kept the ring's appearance as accumulated decrements in the
// pixel buffer rather than deriving it. In rough order of how much they
// are worth:
//
//   Completion  -- a fade reaches black. Always. Including after being
//                  interrupted, which is the bug that shipped: a
//                  re-light set ringFading = false and stranded the
//                  ring part-dimmed with nothing able to finish it.
//   Purity      -- brightness is a function of elapsed time, not of how
//                  many times the loop has run. The old fade was
//                  fadeToBlackBy(1) per loop(), so it took 255 loop
//                  iterations rather than a length of time.
//   Ownership   -- a commit ends the interaction, including when a hand
//                  is still resting on the pad. The old code cleared
//                  the ring's hold but not the pad's last reading, so
//                  the two disagreed and the next edge resurrected it.
//   Structure   -- the strike is a flash over everything, lasts exactly
//                  flashMs, and is not extended by a turn underneath it.

#include <cstdlib>
#include <RingPaint.h>
#include <unity.h>

using retroroom_core::RingConfig;
using retroroom_core::RingMode;
using retroroom_core::RingPaint;
using retroroom_core::RingState;
using retroroom_core::RingUpdate;
using retroroom_core::ringCommit;
using retroroom_core::ringDetent;
using retroroom_core::ringProximity;
using retroroom_core::ringTick;

void setUp(void) {}
void tearDown(void) {}

// Small, explicit timings so the arithmetic in each test is readable.
static RingConfig cfg() {
	RingConfig c;
	c.idleMs = 1000;
	c.flashMs = 120;
	c.fadeMs = 300;
	c.pixelCount = 8;
	return c;
}

// Tick until the ring reports a completed fade. Returns the elapsed
// milliseconds it took, or -1 if it never did. The cap exists so a
// regression fails the test rather than hanging it.
static int runUntilFadeDone(RingState& s, uint32_t fromMs, const RingConfig& c,
							uint32_t stepMs = 10,
							uint32_t capMs = 20000) {
	uint32_t now = fromMs;
	for (uint32_t waited = 0; waited <= capMs; waited += stepMs) {
		now = fromMs + waited;
		if (ringTick(s, now, c, false).fadeCompleted) {
			return static_cast<int>(waited);
		}
	}
	return -1;
}

// How many whole pixels the paint highlights, clamped to the ring.
static int lit(const RingPaint& p, int pixelCount) {
	return p.highlightCount > pixelCount ? pixelCount : p.highlightCount;
}

// ---------------------------------------------------------------------------
// Completion: the ring always gets to black. These are the tests the
// shipped bug would fail.
// ---------------------------------------------------------------------------

// The headline property. A fade that gets interrupted must still be able
// to finish.
//
// This is the case that shipped broken. lightSingle() set ringFading =
// false on every re-light, which cancelled the fade with no way to
// resume it, and because the brightness lived in the pixel buffer as
// accumulated decrements the partial progress was stranded with it. The
// ring sat at whatever level it had reached and no path could move it
// again -- visible as a ring that "goes dim but never goes out".
static void test_an_interrupted_fade_still_reaches_black(void) {
	RingConfig c = cfg();
	c.idleMs = 100;  // keep the test's timeline short
	RingState s;

	ringCommit(s, 1000, c);
	const int toFirstFade = runUntilFadeDone(s, 1000, c);
	TEST_ASSERT_TRUE_MESSAGE(toFirstFade >= 0,
							 "the post-strike fade must complete at all");

	// Interrupt it. Both kinds, because both used to strand: a detent
	// (lightSingle, from the encoder) and a hand returning to the pad.
	ringCommit(s, 1000, c);
	for (uint32_t t = 0; t < 200; t += 20) {
		ringTick(s, 1000 + t, c, false);
	}
	ringDetent(s, 1200, c, 1);
	ringTick(s, 1210, c, false);

	// And again, mid-way through the fade the detent caused.
	for (uint32_t t = 0; t < 60; t += 20) {
		ringTick(s, 1220 + t, c, false);
	}
	ringProximity(s, 1300, true);
	ringTick(s, 1310, c, false);
	ringProximity(s, 1400, false);

	TEST_ASSERT_TRUE_MESSAGE(runUntilFadeDone(s, 1400, c) >= 0,
							 "a fade interrupted twice must still reach black");
	TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::DARK),
						  static_cast<int>(s.mode));
}

// A completed fade is reported on exactly one tick. It is a
// once-per-interaction event, and the shell uses it to revert an
// abandoned browse -- so reporting it twice would revert the cursor
// twice, and reporting it never would strand the browse.
static void test_a_fade_is_reported_complete_exactly_once(void) {
	const RingConfig c = cfg();
	RingState s;
	ringCommit(s, 0, c);

	int reports = 0;
	for (uint32_t t = 0; t < 2000; t += 10) {
		if (ringTick(s, t, c, false).fadeCompleted) {
			++reports;
		}
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(1, reports,
								  "a fade must report completion on one tick only");
	TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::DARK),
						  static_cast<int>(s.mode));
}

// The same, for a fade that runs out of ticks entirely. If the shell
// stops calling for a moment the ring must still be black when it comes
// back -- there is no accumulated state that a gap could strand.
static void test_a_fade_left_unticked_still_lands_on_black(void) {
	const RingConfig c = cfg();
	RingState s;
	ringCommit(s, 0, c);
	ringTick(s, 0, c, false);

	// One tick, long after the fade would have finished.
	const RingUpdate late = ringTick(s, 5000, c, false);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, late.paint.baseLevel,
								  "a fade that ran out of ticks must be black");
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, late.paint.highlightLevel,
								  "a fade that ran out of ticks must be black");
	TEST_ASSERT_TRUE(late.fadeCompleted);
}

// ---------------------------------------------------------------------------
// Purity: brightness is a function of elapsed time, not of tick count.
//
// This is the second shipped bug. The fade was fadeToBlackBy(1) once per
// loop(), so its duration was 255 loop iterations: several seconds on a
// busy loop, far less on a quiet one, and different again the next time
// the firmware changed. The numbers below are the reason it read as
// "ringFlashMs is ignored" -- a 640 ms strike inside a five-second fade
// is invisible.
// ---------------------------------------------------------------------------

// Two rings fading, one sampled every 5 ms and one sampled twice in the
// whole fade, must be the same brightness at the same instant.
static void test_brightness_depends_on_elapsed_time_not_on_tick_count(void) {
	const RingConfig c = cfg();

	RingState fine;
	ringCommit(fine, 0, c);
	uint8_t fineLevel = 0;
	for (uint32_t t = 0; t <= 150; t += 5) {
		fineLevel = ringTick(fine, t, c, false).paint.highlightLevel;
	}

	RingState coarse;
	ringCommit(coarse, 0, c);
	ringTick(coarse, 0, c, false);
	const uint8_t coarseLevel = ringTick(coarse, 150, c, false).paint.highlightLevel;

	TEST_ASSERT_TRUE_MESSAGE(fineLevel < 255, "the fade must be under way");
	TEST_ASSERT_EQUAL_UINT8_MESSAGE(fineLevel, coarseLevel,
									 "brightness must not depend on how often "
									 "the ring was ticked");
}

// The fade takes fadeMs of wall clock, not some number of iterations.
static void test_a_fade_takes_exactly_fade_ms(void) {
	RingConfig c = cfg();
	RingState s;
	ringCommit(s, 0, c);

	// The strike runs first, so the fade starts at flashMs.
	const int took = runUntilFadeDone(s, 0, c);
	TEST_ASSERT_TRUE_MESSAGE(took >= 0, "the fade must complete");
	// 120 ms of strike then 300 ms of fade. The reported tick is the
	// first one past the end, so allow one step of slack.
	TEST_ASSERT_TRUE_MESSAGE(took >= 420 && took <= 430,
							 "the fade must take fadeMs of wall clock");
}

// The ramp is a straight line, so halfway is roughly half as bright.
// Worth pinning because "roughly" is what a perceptual complaint about
// the ring's fade usually turns out to be.
static void test_the_fade_ramps_down_monotonically(void) {
	const RingConfig c = cfg();
	RingState s;
	ringCommit(s, 0, c);
	ringTick(s, 120, c, false);  // strike over, fade starts

	int previous = 256;
	for (uint32_t t = 130; t < 420; t += 10) {
		const int level = ringTick(s, t, c, false).paint.highlightLevel;
		TEST_ASSERT_TRUE_MESSAGE(level <= previous,
								 "the fade must never get brighter");
		previous = level;
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, previous, "the fade must end at black");
}

// ---------------------------------------------------------------------------
// The strike
// ---------------------------------------------------------------------------

// A commit lights the whole ring, for flashMs, and no longer.
static void test_a_commit_strikes_the_whole_ring_for_exactly_flash_ms(void) {
	const RingConfig c = cfg();
	RingState s;
	ringCommit(s, 1000, c);

	const RingUpdate at = ringTick(s, 1010, c, false);
	TEST_ASSERT_EQUAL_INT_MESSAGE(8, lit(at.paint, c.pixelCount),
								  "a strike must light every pixel");
	TEST_ASSERT_EQUAL_UINT8(retroroom_core::kRingFlashLevel, at.paint.highlightLevel);

	// Still striking just before the deadline.
	ringTick(s, 1119, c, false);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::FLASH),
						  static_cast<int>(s.mode));

	// And a fade just after it -- not another strike.
	ringTick(s, 1200, c, false);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::FADING),
						  static_cast<int>(s.mode));
}

// The geometry of the pre-strike picture is carried through the fade. A
// post-strike fade must keep fading the *whole* ring; dropping to a
// single pixel here reads as a partly broken ring rather than as a bug.
static void test_the_post_strike_fade_fades_the_whole_ring(void) {
	const RingConfig c = cfg();
	RingState s;
	ringCommit(s, 0, c);
	ringTick(s, 0, c, false);

	const RingUpdate mid = ringTick(s, 270, c, false);
	TEST_ASSERT_EQUAL_INT_MESSAGE(8, lit(mid.paint, c.pixelCount),
								  "a post-strike fade must keep the whole ring lit");
	TEST_ASSERT_TRUE(mid.paint.highlightLevel < 255);
}

// flashMs = 0 disables the strike. The commit is then a force-off with no
// flash at all, not a zero-length one.
static void test_a_commit_with_no_flash_configured_just_fades(void) {
	RingConfig c = cfg();
	c.flashMs = 0;
	RingState s;
	ringCommit(s, 0, c);

	TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::FADING),
						  static_cast<int>(s.mode));
	TEST_ASSERT_TRUE(runUntilFadeDone(s, 0, c) >= 0);
}

// A turn under a strike must not move the highlight or re-arm a hold
// that the strike is about to end. Letting it through is what makes a
// commit's ring "come back on" after it should have gone dark.
static void test_a_detent_during_a_strike_is_ignored(void) {
	RingConfig c = cfg();
	c.flashMs = 500;
	RingState s;
	ringCommit(s, 1000, c);
	ringDetent(s, 1010, c, 1);

	// Long past the strike. If the detent had been honoured the ring
	// would be IDLE with a fresh idleMs hold rather than fading.
	ringTick(s, 1600, c, false);
	TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::FADING),
								  static_cast<int>(s.mode),
								  "a detent under a strike must not start a hold");
}

// ---------------------------------------------------------------------------
// Proximity. The third shipped bug lived in the seam between this and
// src/controls.cpp.
// ---------------------------------------------------------------------------

// A hand on the pad holds the ring past the idle timeout. The countdown
// is pushed out on every tick, so however long the hand stays the ring
// stays lit -- an engaged operator should not have it expire under them.
static void test_a_hand_on_the_pad_holds_the_ring_past_the_idle_timeout(void) {
	const RingConfig c = cfg();
	RingState s;
	ringProximity(s, 0, true);

	for (uint32_t t = 0; t < 30000; t += 100) {
		const RingUpdate u = ringTick(s, t, c, false);
		TEST_ASSERT_TRUE_MESSAGE(u.paint.highlightLevel > 0,
								 "the ring must stay lit while a hand is present");
		TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::PROXIMITY),
							  static_cast<int>(s.mode));
	}
}

// The bug. A commit ends the interaction even with a hand still resting
// on the pad -- and it has to *stay* ended.
//
// The old code cleared the ring's proximity hold in lightRingForceOff()
// but left the pad's last reading alone in src/controls.cpp, so the two
// disagreed. The ring went dark, and then the next pad edge re-armed a
// hold nobody had asked for, putting the ring back on for another full
// idle timeout. That is the "the commit never makes the ring go out"
// behaviour.
static void test_a_commit_ends_the_hold_with_a_hand_still_on_the_pad(void) {
	const RingConfig c = cfg();
	RingState s;

	// Hand arrives, turns the knob, then commits without moving.
	ringProximity(s, 0, true);
	ringDetent(s, 100, c, 1);
	ringCommit(s, 200, c);

	// Strike, then fade, and it must stay dark -- the hand is still
	// there and must not resurrect it.
	for (uint32_t t = 200; t < 10000; t += 50) {
		const RingUpdate u = ringTick(s, t, c, false);
		if (t > 800) {
			TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::DARK),
										  static_cast<int>(s.mode),
										  "a committed ring must stay dark with a "
										  "hand on the pad");
			TEST_ASSERT_EQUAL_INT_MESSAGE(0, u.paint.highlightLevel,
										  "a committed ring must stay dark with a "
										  "hand on the pad");
		}
	}
}

// ...but a genuine approach after lifting and returning does re-arm it.
static void test_leaving_and_returning_re_arms_the_hold(void) {
	const RingConfig c = cfg();
	RingState s;

	ringProximity(s, 0, true);
	ringCommit(s, 100, c);
	// 120 ms of strike then 300 ms of fade, so the ring is black at 600
	// and not before.
	ringTick(s, 500, c, false);
	TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::FADING),
								  static_cast<int>(s.mode),
								  "500 ms in, the ring must still be fading");
	ringTick(s, 600, c, false);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(RingMode::DARK),
						  static_cast<int>(s.mode));

	ringProximity(s, 600, false);
	ringProximity(s, 700, true);
	ringTick(s, 710, c, false);
	TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::PROXIMITY),
								  static_cast<int>(s.mode),
								  "a fresh approach must light the ring again");
	TEST_ASSERT_TRUE(ringTick(s, 720, c, false).paint.highlightLevel > 0);
}

// Leaving fades immediately rather than waiting out the idle timeout --
// making the operator wait after they have already taken their hand away
// is a delay on a decision that has been made.
static void test_leaving_the_pad_fades_rather_than_waiting(void) {
	const RingConfig c = cfg();
	RingState s;
	ringProximity(s, 0, true);
	ringTick(s, 100, c, false);
	ringProximity(s, 200, false);

	TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::FADING),
								  static_cast<int>(s.mode),
								  "leaving the pad must start a fade at once");
	TEST_ASSERT_TRUE(runUntilFadeDone(s, 200, c) >= 0);
}

// Reading "near" repeatedly is not an edge and must not re-light or
// re-arm anything.
static void test_a_steady_reading_is_not_an_edge(void) {
	const RingConfig c = cfg();
	RingState s;
	ringProximity(s, 0, true);
	ringTick(s, 50, c, false);

	const RingState before = s;
	for (int i = 0; i < 10; ++i) {
		ringProximity(s, 60 + i, true);
	}
	TEST_ASSERT_EQUAL_INT(static_cast<int>(before.mode),
						  static_cast<int>(s.mode));
	TEST_ASSERT_EQUAL_UINT(static_cast<int>(before.pixel),
						   static_cast<int>(s.pixel));
}

// ---------------------------------------------------------------------------
// Idle
// ---------------------------------------------------------------------------

// idleMs = 0 disables the *timeout* -- the ring then holds until something
// explicitly ends the interaction. It must not disable a commit.
static void test_idle_ms_zero_disables_the_timeout_but_not_a_commit(void) {
	RingConfig c = cfg();
	c.idleMs = 0;
	RingState s;
	ringDetent(s, 0, c, 1);

	for (uint32_t t = 0; t < 60000; t += 500) {
		ringTick(s, t, c, false);
		TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::IDLE),
									  static_cast<int>(s.mode),
									  "with idleMs = 0 the ring must never time out");
	}

	ringCommit(s, 60000, c);
	ringTick(s, 60100, c, false);
	TEST_ASSERT_TRUE_MESSAGE(runUntilFadeDone(s, 60100, c) >= 0,
							 "a commit must still end the hold with idleMs = 0");
}

// Every detent re-arms the full timeout, so a browse that never stops
// turning never times out mid-gesture.
static void test_a_detent_re_arms_the_idle_hold(void) {
	const RingConfig c = cfg();
	RingState s;
	ringDetent(s, 0, c, 1);

	for (uint32_t t = 0; t < 5000; t += 250) {
		ringDetent(s, t, c, 1);
		ringTick(s, t, c, false);
		TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::IDLE),
									  static_cast<int>(s.mode),
									  "turning the knob must hold the ring open");
	}

	// Stop turning, and it times out a full idleMs after the last detent.
	TEST_ASSERT_TRUE(runUntilFadeDone(s, 5000, c) >= 1000);
}

// The strip unwinding an abandoned run holds the ring -- and holds the
// *whole* timeout, pushed out rather than shortened, so the operator gets
// the full idle period after the last LED goes rather than the remainder
// of one that started before the retreat did.
static void test_the_retreat_holds_the_timeout(void) {
	const RingConfig c = cfg();
	RingState s;
	ringDetent(s, 0, c, 1);

	for (uint32_t t = 0; t < 2000; t += 100) {
		ringTick(s, t, c, true);
		TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(RingMode::IDLE),
									  static_cast<int>(s.mode),
									  "the retreat must hold the ring lit");
	}

	// Retreat over at 2000: the full idleMs from there, not the 0 ms
	// left of the one that started at 0.
	const int took = runUntilFadeDone(s, 2000, c);
	TEST_ASSERT_TRUE_MESSAGE(took >= 1000,
							 "the retreat must push the timeout out, not "
							 "shorten it");
}

// ---------------------------------------------------------------------------
// The spinner
// ---------------------------------------------------------------------------

// The pixel counter is a free-running spinner, not a console index, and
// wraps at both ends.
static void test_the_spinner_wraps_at_both_ends(void) {
	const RingConfig c = cfg();
	RingState s;

	for (int i = 0; i < 8; ++i) {
		ringDetent(s, 0, c, 1);
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.pixel, "the spinner must wrap forwards");

	for (int i = 0; i < 8; ++i) {
		ringDetent(s, 0, c, -1);
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.pixel, "the spinner must wrap backwards");

	ringDetent(s, 0, c, -1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(7, s.pixel,
								  "backing off the start must land on the last "
								  "pixel");
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

// millis() wraps every 49.7 days and the ring is still running at the
// time. Every comparison here is unsigned-difference based for that
// reason; this is the test that would notice if one were rewritten as a
// plain `>`.
static void test_the_fade_survives_millis_rollover(void) {
	const RingConfig c = cfg();
	const uint32_t nearWrap = 0xFFFFFFFFu - 100u;
	RingState s;

	ringCommit(s, nearWrap, c);
	const int took = runUntilFadeDone(s, nearWrap, c, 10, 2000);
	TEST_ASSERT_TRUE_MESSAGE(took >= 0, "a fade across the rollover must finish");
}

// Unity's setUp/tearDown + main. Every other suite here provides its own
// entry point rather than relying on the framework's runner injecting
// one, and the test binary does not link without it.
int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();
	// Completion -- the properties the shipped bugs violated.
	RUN_TEST(test_an_interrupted_fade_still_reaches_black);
	RUN_TEST(test_a_fade_is_reported_complete_exactly_once);
	RUN_TEST(test_a_fade_left_unticked_still_lands_on_black);
	// Purity -- brightness from elapsed time, not tick count.
	RUN_TEST(test_brightness_depends_on_elapsed_time_not_on_tick_count);
	RUN_TEST(test_a_fade_takes_exactly_fade_ms);
	RUN_TEST(test_the_fade_ramps_down_monotonically);
	// The strike.
	RUN_TEST(test_a_commit_strikes_the_whole_ring_for_exactly_flash_ms);
	RUN_TEST(test_the_post_strike_fade_fades_the_whole_ring);
	RUN_TEST(test_a_commit_with_no_flash_configured_just_fades);
	RUN_TEST(test_a_detent_during_a_strike_is_ignored);
	// Proximity, including the bug that lived in the controls.cpp seam.
	RUN_TEST(test_a_hand_on_the_pad_holds_the_ring_past_the_idle_timeout);
	RUN_TEST(test_a_commit_ends_the_hold_with_a_hand_still_on_the_pad);
	RUN_TEST(test_leaving_and_returning_re_arms_the_hold);
	RUN_TEST(test_leaving_the_pad_fades_rather_than_waiting);
	RUN_TEST(test_a_steady_reading_is_not_an_edge);
	// Idle.
	RUN_TEST(test_idle_ms_zero_disables_the_timeout_but_not_a_commit);
	RUN_TEST(test_a_detent_re_arms_the_idle_hold);
	RUN_TEST(test_the_retreat_holds_the_timeout);
	// The spinner.
	RUN_TEST(test_the_spinner_wraps_at_both_ends);
	// Clock.
	RUN_TEST(test_the_fade_survives_millis_rollover);
	return UNITY_END();
}
