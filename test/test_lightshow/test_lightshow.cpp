// Host-side Unity tests for lib/LightShow.
//
// The show itself lives in src/ledstring.cpp and needs a strip, a
// FastLED controller and a clock. What is worth checking is everything
// in between: that a frame is bounded, that it moves, that the palette
// list is intact, that the cross-fade actually lands on the next
// palette, and that the arithmetic does not wrap out of range.
//
// Those are all checkable here because the core takes its clock as an
// argument rather than reading one. `nowMs` is a parameter precisely so
// that "what does the show look like at t=41230" is a question this
// suite can ask.

#include <LightShow.h>
#include <unity.h>

#include <cstdio>

using retroroom_core::LightShowConfig;
using retroroom_core::LightShowFrameBuffer;
using retroroom_core::LightShowState;
using retroroom_core::Rgb;
using retroroom_core::computeColorWaveFrame;
using retroroom_core::kPalettes;
using retroroom_core::kPaletteCount;
using retroroom_core::kPlaylist;
using retroroom_core::kPlaylistCount;
using retroroom_core::lightShowNextPalette;
using retroroom_core::lightShowPaletteName;
using retroroom_core::lightShowPrevPalette;
using retroroom_core::lightShowReset;
using retroroom_core::lightShowSelectPalette;
using retroroom_core::lightShowStarted;

void setUp(void) {}
void tearDown(void) {}

// The strip this cabinet actually has. Deliberately not the capacity:
// a show that only works at one length is a show that breaks when
// led.totalLeds changes.
static const int kLeds = 118;

static LightShowConfig fastConfig() {
	LightShowConfig c;
	c.frameIntervalMs = 16;
	c.paletteDwellMs = 400;
	c.paletteFadeMs = 100;
	return c;
}

static void paintFrames(LightShowState& s, const LightShowConfig& c, int frames,
                        int numLeds, uint32_t startMs,
                        LightShowFrameBuffer& buf) {
	for (int i = 0; i < frames; ++i) {
		computeColorWaveFrame(s, c, numLeds, startMs + 16u * i, buf);
	}
}

// One buffer, created once, handed to every frame. The same object the
// shell keeps. Not tidiness: the wave blends over the previous frame,
// so a fresh buffer per call is a garbage smear, and that is exactly
// the bug the type now exists to make impossible.
static void paintFrames(LightShowState& s, const LightShowConfig& c, int frames,
                        int numLeds, uint32_t startMs) {
	LightShowFrameBuffer buf;
	paintFrames(s, c, frames, numLeds, startMs, buf);
}

// Advance the show by `frames` frames, discarding the picture.
//
// The frame function is "compute a frame", and it will NOT advance
// without somewhere to paint into -- a show that moved while nothing was
// drawn is a show that skips ahead every time a caller only wanted the
// clock. Every test below that is really about the *timing* uses this
// rather than passing a null buffer, so that contract is exercised by
// accident and cannot be changed without a test noticing.
static void advance(LightShowState& s, const LightShowConfig& c, int frames,
                    uint32_t startMs) {
	LightShowFrameBuffer scratch;
	paintFrames(s, c, frames, kLeds, startMs, scratch);
}

static int absDiff(int a, int b) { return a > b ? a - b : b - a; }

static bool allZero(const Rgb* buf, int n) {	for (int i = 0; i < n; ++i) {
		if (buf[i].r || buf[i].g || buf[i].b) {
			return false;
		}
	}
	return true;
}

static int litCount(const Rgb* buf, int n) {
	int lit = 0;
	for (int i = 0; i < n; ++i) {
		if (buf[i].r || buf[i].g || buf[i].b) {
			lit++;
		}
	}
	return lit;
}

// ---- the palette set --------------------------------------------------

static void test_the_palette_set_is_intact(void) {
	// Both of these are "someone edited the file" failures, and both are
	// silent at build time: a bad index is a colour from the wrong
	// palette, not a compile error.
	TEST_ASSERT_GREATER_THAN_INT(0, kPaletteCount);
	TEST_ASSERT_GREATER_THAN_INT(0, kPlaylistCount);
	for (int i = 0; i < kPlaylistCount; ++i) {
		TEST_ASSERT_GREATER_OR_EQUAL_INT(0, kPlaylist[i]);
		TEST_ASSERT_LESS_THAN_INT(kPaletteCount, kPlaylist[i]);
		TEST_ASSERT_TRUE_MESSAGE(kPalettes[kPlaylist[i]].length >= 2,
		                         retroroom_core::kPaletteNames[i]);
	}
}

static void test_every_palette_name_matches_its_slot(void) {
	// kPaletteNames is parallel to kPlaylist and nothing checks that
	// except a caller reading it. A palette reported under the wrong
	// name is worse than one reported as "?".
	for (int i = 0; i < kPlaylistCount; ++i) {
		TEST_ASSERT_EQUAL_STRING(
		    retroroom_core::kPaletteNames[i], lightShowPaletteName(i));
	}
	// And an index off either end answers "?" rather than reading past
	// the table.
	TEST_ASSERT_EQUAL_STRING("?", lightShowPaletteName(-1));
	TEST_ASSERT_EQUAL_STRING("?", lightShowPaletteName(kPlaylistCount));
}

// ---- starting and stopping --------------------------------------------

static void test_a_fresh_show_is_not_started(void) {
	// The default state is -1, and a frame asked for before a start
	// must not read off the end of the playlist.
	LightShowState s;
	TEST_ASSERT_FALSE(lightShowStarted(s));
}

static void test_reset_starts_on_the_first_palette(void) {
	LightShowState s;
	lightShowReset(&s, 5000);
	TEST_ASSERT_TRUE(lightShowStarted(s));
	TEST_ASSERT_EQUAL_INT(0, s.playlistIndex);
	TEST_ASSERT_EQUAL_UINT32(5000, s.lastFrameMs);
	TEST_ASSERT_EQUAL_UINT32(5000, s.paletteChangeAtMs);
	TEST_ASSERT_EQUAL_INT(0, s.blend);
}

static void test_reset_seeds_the_clock(void) {
	// Without this the first frame's delta is the whole time since boot,
	// and on a cabinet powered up an hour ago that is a hue jump of
	// about a quarter of a million degrees.
	//
	// The property is the DELTA, so the assertion is that a frame at the
	// reset time moves nothing and the next one moves by its own
	// interval -- not that some absolute counter equals a literal.
	LightShowState s;
	lightShowReset(&s, 3600000u);
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer buf;
	computeColorWaveFrame(s, c, kLeds, 3600000u, buf);
	const uint16_t atSeed = s.pseudotime;
	TEST_ASSERT_EQUAL_UINT32(3600000u, s.lastFrameMs);
	TEST_ASSERT_EQUAL_UINT16(atSeed, s.pseudotime);
	computeColorWaveFrame(s, c, kLeds, 3600016u, buf);
	TEST_ASSERT_EQUAL_UINT32(3600016u, s.lastFrameMs);
	TEST_ASSERT_NOT_EQUAL(atSeed, s.pseudotime);
}

// ---- the frame --------------------------------------------------------

static void test_a_frame_lights_the_strip(void) {
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer buf;
	paintFrames(s, c, 8, kLeds, 0, buf);
	TEST_ASSERT_FALSE_MESSAGE(allZero(buf.pixels, kLeds),
	                          "eight frames in and the strip is still black");
	// Not every pixel, either: a solid bar is a bug, not a look.
	TEST_ASSERT_GREATER_THAN_INT(0, litCount(buf.pixels, kLeds));
}

static void test_the_frame_advances_the_clock(void) {
	LightShowState s;
	lightShowReset(&s, 1000);
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer buf;
	computeColorWaveFrame(s, c, kLeds, 1016, buf);
	TEST_ASSERT_EQUAL_UINT32(1016, s.lastFrameMs);
	// The phase accumulators are the only thing that makes this a show
	// rather than a still.
	TEST_ASSERT_NOT_EQUAL(0, s.pseudotime);
}

static void test_the_wave_moves(void) {
	// Two frames a long way apart must not be the same picture. This is
	// the assertion that catches a frozen oscillator, which is the one
	// failure a "is it lit" test sails straight past.
	LightShowState a;
	lightShowReset(&a, 0);
	LightShowState b = a;
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer first;
	LightShowFrameBuffer later;
	computeColorWaveFrame(a, c, kLeds, 2000, first);
	computeColorWaveFrame(b, c, kLeds, 2000 + 4000, later);
	int moved = 0;
	for (int i = 0; i < kLeds; ++i) {
		if (first.pixels[i].r != later.pixels[i].r ||
		    first.pixels[i].g != later.pixels[i].g ||
		    first.pixels[i].b != later.pixels[i].b) {
			moved++;
		}
	}
	TEST_ASSERT_GREATER_THAN_INT(kLeds / 2, moved);
}

static void test_nothing_is_written_outside_the_buffer(void) {
	// Canaries either side of the pixel array. The wave writes
	// numLeds-1-i, which is an easy expression to get wrong in a way
	// that only shows up as a corrupted stack on the device.
	struct {
		Rgb before[2];
		LightShowFrameBuffer frame;
		Rgb after[2];
	} guarded;
	for (int i = 0; i < 2; ++i) {
		guarded.before[i] = Rgb{1, 2, 3};
		guarded.after[i] = Rgb{4, 5, 6};
	}
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();
	paintFrames(s, c, 4, kLeds, 0, guarded.frame);
	TEST_ASSERT_EQUAL_UINT8(1, guarded.before[0].r);
	TEST_ASSERT_EQUAL_UINT8(2, guarded.before[0].g);
	TEST_ASSERT_EQUAL_UINT8(3, guarded.before[0].b);
	TEST_ASSERT_EQUAL_UINT8(1, guarded.before[1].r);
	TEST_ASSERT_EQUAL_UINT8(4, guarded.after[0].r);
	TEST_ASSERT_EQUAL_UINT8(5, guarded.after[0].g);
	TEST_ASSERT_EQUAL_UINT8(6, guarded.after[0].b);
	TEST_ASSERT_EQUAL_UINT8(4, guarded.after[1].r);
}

static void test_a_zero_length_strip_writes_nothing(void) {
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer buf;
	for (int i = 0; i < 4; ++i) {
		buf.pixels[i] = Rgb{9, 9, 9};
	}
	computeColorWaveFrame(s, c, 0, 100, buf);
	computeColorWaveFrame(s, c, -5, 200, buf);
	for (int i = 0; i < 4; ++i) {
		TEST_ASSERT_EQUAL_UINT8(9, buf.pixels[i].r);
	}
}

static void test_one_led_strip_does_not_divide_by_zero(void) {
	// The step index is (i * 256) / (numLeds - 1), and numLeds == 1
	// makes that a divide by zero. Guarded above by a ternary; this is
	// the test that keeps the guard honest.
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer one;
	computeColorWaveFrame(s, c, 1, 100, one);
	computeColorWaveFrame(s, c, 1, 200, one);
	TEST_ASSERT_TRUE(one.pixels[0].r || one.pixels[0].g ||
	                    one.pixels[0].b);
}

static void test_every_channel_stays_in_range(void) {
	// A long run at several frame rates. The blend and the oscillator
	// maths are all signed and all wrap somewhere; this is the check
	// that none of them wraps somewhere visible.
	LightShowState s;
	lightShowReset(&s, 0);
	LightShowConfig c = fastConfig();
	for (int step = 0; step < 4; ++step) {
		c.frameIntervalMs = 1 << (step * 2);
		LightShowState run = s;
		LightShowFrameBuffer buf;
		uint32_t t = 0;
		for (int f = 0; f < 300; ++f) {
			t += c.frameIntervalMs;
			computeColorWaveFrame(run, c, kLeds, t, buf);
		}
		for (int i = 0; i < kLeds; ++i) {
			// A uint8_t cannot be out of range, so what is actually
			// being checked is that the strip is not simply black --
			// a run that went all-zero would look like a working
			// "is it lit" test and be a dead show.
			TEST_ASSERT_TRUE_MESSAGE(buf.pixels[i].r || buf.pixels[i].g ||
			                         buf.pixels[i].b,
			                         "a pixel went black mid-show");
		}
	}
}

static void test_the_clock_may_wrap(void) {
	// millis() wraps every 49.7 days. The delta is unsigned
	// subtraction, so a frame just after the wrap must still advance by
	// a small amount and not by four billion.
	//
	// Reset AT the near-top time, not at zero: a reset at zero followed
	// by a frame near the top is a four-billion-millisecond first frame,
	// which is a different test and a wrong one. (It is also harmless --
	// the wrap is defined and nothing goes out of range -- but it is not
	// what this is checking.)
	LightShowState s;
	const uint32_t nearTop = 0xFFFFFF00u;
	lightShowReset(&s, nearTop);
	const LightShowConfig c = fastConfig();
	LightShowFrameBuffer buf;
	computeColorWaveFrame(s, c, kLeds, nearTop, buf);
	const uint16_t before = s.pseudotime;
	const uint32_t wrapped = nearTop + 32u;  // past 2^32
	computeColorWaveFrame(s, c, kLeds, wrapped, buf);
	TEST_ASSERT_EQUAL_UINT32(wrapped, s.lastFrameMs);
	// 32 ms of pseudotime, not four billion of it. A signed or widened
	// delta would show here as a wildly different phase.
	TEST_ASSERT_NOT_EQUAL(before, s.pseudotime);
	TEST_ASSERT_LESS_THAN_UINT16(0x4000, s.pseudotime);
}

// ---- the cross-fade ---------------------------------------------------

static void test_the_palette_advances_after_its_dwell(void) {
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();  // 400 ms dwell
	TEST_ASSERT_EQUAL_INT(0, s.playlistIndex);
	advance(s, c, 10, 16);  // 160 ms: still the first
	TEST_ASSERT_EQUAL_INT(0, s.playlistIndex);
	// Past the dwell (400 ms) but not past a second one.
	advance(s, c, 30, 16 * 11);  // 176 .. 640 ms
	TEST_ASSERT_EQUAL_INT(1, s.playlistIndex);
}

static void test_the_fade_is_the_tail_of_the_dwell(void) {
	// The fade must not start before dwell - fade, or the palette is
	// never shown on its own, which is the whole point of a dwell.
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();  // 400 dwell, 100 fade
	TEST_ASSERT_EQUAL_INT(0, s.blend);
	advance(s, c, 15, 16);  // 240 ms: inside the dwell, fade not open
	TEST_ASSERT_EQUAL_INT(0, s.blend);
	advance(s, c, 5, 16 * 16);  // 320 ms: inside the fade window
	TEST_ASSERT_GREATER_THAN_INT(0, s.blend);
	TEST_ASSERT_LESS_OR_EQUAL_INT(255, s.blend);
}

static void test_no_fade_when_the_config_asks_for_none(void) {
	LightShowState s;
	lightShowReset(&s, 0);
	LightShowConfig c = fastConfig();
	c.paletteFadeMs = 0;
	advance(s, c, 40, 16);
	TEST_ASSERT_EQUAL_INT(0, s.blend);
	// And it still advanced, so "no fade" is not "no show".
	TEST_ASSERT_EQUAL_INT(1, s.playlistIndex);
}

static void test_a_fade_longer_than_the_dwell_is_clamped(void) {
	// A config asking for a 60 s fade on a 1 s dwell is asking for
	// nonsense. The dwell wins rather than a blend that never completes
	// or a negative window.
	LightShowState s;
	lightShowReset(&s, 0);
	LightShowConfig c;
	c.paletteDwellMs = 1000;
	c.paletteFadeMs = 60000;
	advance(s, c, 40, 16);  // 640 ms: inside the clamped fade
	TEST_ASSERT_EQUAL_INT(0, s.playlistIndex);
	TEST_ASSERT_GREATER_THAN_INT(0, s.blend);
	TEST_ASSERT_LESS_OR_EQUAL_INT(255, s.blend);
}

// ---- palette selection ------------------------------------------------

static void test_next_and_prev_wrap(void) {
	LightShowState s;
	lightShowReset(&s, 0);
	lightShowPrevPalette(&s);
	TEST_ASSERT_EQUAL_INT(kPlaylistCount - 1, s.playlistIndex);
	lightShowNextPalette(&s);
	TEST_ASSERT_EQUAL_INT(0, s.playlistIndex);
	lightShowNextPalette(&s);
	TEST_ASSERT_EQUAL_INT(1, s.playlistIndex);
}

static void test_select_wraps_from_either_end(void) {
	LightShowState s;
	lightShowReset(&s, 0);
	lightShowSelectPalette(&s, kPlaylistCount + 3);
	TEST_ASSERT_EQUAL_INT(3, s.playlistIndex);
	lightShowSelectPalette(&s, -1);
	TEST_ASSERT_EQUAL_INT(kPlaylistCount - 1, s.playlistIndex);
	lightShowSelectPalette(&s, -kPlaylistCount - 1);
	TEST_ASSERT_EQUAL_INT(kPlaylistCount - 1, s.playlistIndex);
}

static void test_selecting_resets_the_fade(void) {
	// Otherwise jumping to a palette mid-fade cross-fades from the one
	// the operator just left, which is not what "show me palette 7" means.
	LightShowState s;
	lightShowReset(&s, 0);
	const LightShowConfig c = fastConfig();
	// 720 ms: one dwell done (400) and inside the next fade window
	// (300..400 of that dwell, i.e. 700..800 absolute).
	advance(s, c, 45, 16);
	TEST_ASSERT_GREATER_THAN_INT(0, s.blend);
	lightShowSelectPalette(&s, 5);
	TEST_ASSERT_EQUAL_INT(0, s.blend);
	TEST_ASSERT_EQUAL_INT(5, s.playlistIndex);
}

static void test_a_long_run_stays_inside_the_playlist(void) {
	// Ten minutes at 16 ms is ~37,500 palette changes. If the index is
	// ever allowed outside the table this is where it shows, and the
	// symptom would be a colour from a random palette rather than a
	// crash.
	LightShowState s;
	lightShowReset(&s, 0);
	LightShowConfig c;
	c.paletteDwellMs = 16;  // one frame per palette
	c.paletteFadeMs = 0;
	LightShowFrameBuffer buf;
	uint32_t t = 0;
	for (int f = 0; f < 2000; ++f) {
		t += 16;
		computeColorWaveFrame(s, c, kLeds, t, buf);
		TEST_ASSERT_GREATER_OR_EQUAL_INT(0, s.playlistIndex);
		TEST_ASSERT_LESS_THAN_INT(kPlaylistCount, s.playlistIndex);
	}
}

// ---- the buffer contract ----------------------------------------------

static void test_the_frame_blends_over_what_was_there(void) {
	// The wave's trailing smear comes from blending each new colour
	// halfway over the pixel. A caller that hands in a fresh buffer
	// every frame is not making a different choice about the look, they
	// are reading uninitialised memory -- which is the bug this
	// function's sibling caught, so the assertion is here to keep the
	// contract true from the other direction too.
	LightShowState a;
	lightShowReset(&a, 0);
	LightShowState b = a;
	const LightShowConfig c = fastConfig();
	// Two buffers with different history, handed to the same frame.
	LightShowFrameBuffer warm;
	LightShowFrameBuffer cold;
	for (int i = 0; i < kLeds; ++i) {
		warm.pixels[i] = Rgb{200, 200, 200};
		cold.pixels[i] = Rgb{0, 0, 0};
	}
	computeColorWaveFrame(a, c, kLeds, 1000, warm);
	computeColorWaveFrame(b, c, kLeds, 1000, cold);
	// Same frame, different history: both lit, and the warm one is
	// brighter in at least one channel because it started there.
	TEST_ASSERT_FALSE(allZero(warm.pixels, kLeds));
	TEST_ASSERT_FALSE(allZero(cold.pixels, kLeds));
	int warmer = 0;
	for (int i = 0; i < kLeds; ++i) {
		if (warm.pixels[i].r > cold.pixels[i].r) {
			warmer++;
		}
	}
	TEST_ASSERT_GREATER_THAN_INT(0, warmer);
}

static void test_the_smear_comes_from_reusing_the_buffer(void) {
	// The regression test for the bench report: "left it for a couple of
	// hours and it looked like random flickering".
	//
	// The shell declared `Rgb wave[...]` INSIDE its paint function, so
	// every frame blended over uninitialised stack -- the core READS the
	// buffer it is given, because the wave's trailing smear comes from
	// blending over the previous frame. It looked fine for minutes,
	// because the stack slot happened to hold the previous frame, and
	// turned to a strobe later, once enough different code had run
	// underneath that slot to change what was in it. That is why it
	// looked time-dependent and was not.
	//
	// The other side of the comparison is JUNK in the buffer, not a
	// cleared one. A cleared buffer is a real, if dull, case -- the wave
	// at half brightness, moving the same amount each frame as it does
	// with a smear. Uninitialised memory is not dull, it is wrong, and
	// it is wrong by a different amount every frame, which is what
	// flickered.
	LightShowState s;
	lightShowReset(&s, 0);
	// A long dwell, so the window below is steady state and the
	// cross-fade is not the thing being measured. The fade moves every
	// pixel anyway and would swamp the difference.
	LightShowConfig c = fastConfig();
	c.paletteDwellMs = 600000;
	c.paletteFadeMs = 0;

	LightShowState reusedState = s;
	LightShowState junkState = s;
	LightShowFrameBuffer reused;
	LightShowFrameBuffer junk;

	// A deterministic stand-in for whatever is on the stack. Fixed seed
	// and a local generator rather than rand(), so a failure is
	// reproducible and the test does not depend on the C library's
	// rand() being what it was.
	uint32_t noise = 0x1234567u;
	auto fillJunk = [&]() {
		for (int i = 0; i < kLeds; ++i) {
			noise = noise * 1664525u + 1013904223u;
			junk.pixels[i].r = static_cast<uint8_t>(noise >> 16);
			noise = noise * 1664525u + 1013904223u;
			junk.pixels[i].g = static_cast<uint8_t>(noise >> 16);
			noise = noise * 1664525u + 1013904223u;
			junk.pixels[i].b = static_cast<uint8_t>(noise >> 16);
		}
	};

	// Warm up past the first frame, which legitimately fades up from
	// black and would otherwise dominate the mean.
	for (int f = 0; f < 20; ++f) {
		computeColorWaveFrame(reusedState, c, kLeds, 8u * f, reused);
		fillJunk();
		computeColorWaveFrame(junkState, c, kLeds, 8u * f, junk);
	}

	const int scored = 60;
	long reusedTotal = 0;
	long junkTotal = 0;
	for (int f = 0; f < scored; ++f) {
		LightShowFrameBuffer beforeReused = reused;
		const uint32_t t = 1000u + 8u * f;
		computeColorWaveFrame(reusedState, c, kLeds, t, reused);
		fillJunk();
		computeColorWaveFrame(junkState, c, kLeds, t, junk);
		for (int i = 0; i < kLeds; ++i) {
			reusedTotal +=
			    absDiff(beforeReused.pixels[i].r, reused.pixels[i].r) +
			    absDiff(beforeReused.pixels[i].g, reused.pixels[i].g) +
			    absDiff(beforeReused.pixels[i].b, reused.pixels[i].b);
			// Against the previous JUNK frame, which is the comparison
			// that matters: consecutive outputs of a frame that is
			// blending over noise differ by the noise.
			junkTotal +=
			    absDiff(beforeReused.pixels[i].r, junk.pixels[i].r) +
			    absDiff(beforeReused.pixels[i].g, junk.pixels[i].g) +
			    absDiff(beforeReused.pixels[i].b, junk.pixels[i].b);
		}
	}
	const double reusedMean = (double)reusedTotal / (scored * kLeds * 3);
	const double junkMean = (double)junkTotal / (scored * kLeds * 3);

	TEST_ASSERT_TRUE_MESSAGE(reusedMean > 0.0, "the reused buffer never moved");
	// The whole claim of this test, as a ratio rather than a value: the
	// exact numbers move as the palettes and the wave are tuned, but a
	// smear that has stopped being a smear collapses the ratio at once,
	// and that is what a future change would break. Measured around
	// 200x; the threshold is well under it so tuning cannot make this
	// flaky, and far above the ~1x a lost smear would give.
	char msg[144];
	snprintf(msg, sizeof(msg),
	         "blending over junk moved pixels only %.0fx more than reusing "
	         "the buffer (reused=%.2f junk=%.2f); the smear is gone",
	         junkMean / (reusedMean > 0 ? reusedMean : 1), reusedMean,
	         junkMean);
	TEST_ASSERT_TRUE_MESSAGE(junkMean > reusedMean * 20.0, msg);
}

// ---- the pixel -> wave-step mapping ---------------------------------

// The regression test for a real bug: the strip is walked backwards, so
// an LED that read one past the end of the 256-entry sample table was
// LED 0, and it sat on one colour for as long as the show ran. Found on
// the bench as "the first LED is red no matter what".
//
// Walked over a spread of strip lengths rather than the 118 this cabinet
// happens to have, and with a per-failure message, because the bad
// value only appears at the LAST pixel of every length -- which is
// exactly what a single-length check would have missed the first time
// led.totalLeds changed.
static void test_every_led_maps_inside_the_sample_table(void) {
	for (int numLeds = 1; numLeds <= 600; numLeds += 7) {
		for (int i = 0; i < numLeds; ++i) {
			const int step = retroroom_core::waveStepFor(i, numLeds);
			char msg[96];
			snprintf(msg, sizeof(msg),
			         "LED %d of %d maps to step %d, outside 0..255", i,
			         numLeds, step);
			TEST_ASSERT_TRUE_MESSAGE(step >= 0 && step <= 255, msg);
		}
	}
}

static void test_the_mapping_spans_the_whole_table(void) {
	// Not just in range: the first LED must be step 0 and the last must
	// be step 255, or the wave is not reaching the ends of the strip. A
	// mapping that clamped to 0..254 would pass the range test and
	// quietly waste the last fifth of the table.
	for (int numLeds = 2; numLeds <= 512; ++numLeds) {
		char msg[96];
		snprintf(msg, sizeof(msg), "%d LEDs: first is step %d, not 0",
		         numLeds, retroroom_core::waveStepFor(0, numLeds));
		TEST_ASSERT_TRUE_MESSAGE(retroroom_core::waveStepFor(0, numLeds) == 0,
		                         msg);
		snprintf(msg, sizeof(msg), "%d LEDs: last is step %d, not 255",
		         numLeds, retroroom_core::waveStepFor(numLeds - 1, numLeds));
		TEST_ASSERT_TRUE_MESSAGE(
		    retroroom_core::waveStepFor(numLeds - 1, numLeds) == 255, msg);
	}
}

static void test_the_mapping_never_moves_backwards(void) {
	// The wave runs along the strip, so the step must not go backwards
	// as the LED number goes up. A wrap or a sign slip would still be
	// "in range" and would still light every LED.
	for (int numLeds = 2; numLeds <= 300; numLeds += 13) {
		int prev = 0;
		for (int i = 0; i < numLeds; ++i) {
			const int step = retroroom_core::waveStepFor(i, numLeds);
			char msg[96];
			snprintf(msg, sizeof(msg),
			         "%d LEDs: step went backwards at LED %d (%d after %d)",
			         numLeds, i, step, prev);
			TEST_ASSERT_TRUE_MESSAGE(step >= prev, msg);
			prev = step;
		}
	}
}

static void test_the_mapping_survives_degenerate_strips(void) {
	// 0 and negative lengths are the shell's problem to avoid, but the
	// function is public and the arithmetic has a divide in it.
	TEST_ASSERT_EQUAL_INT(0, retroroom_core::waveStepFor(0, 0));
	TEST_ASSERT_EQUAL_INT(0, retroroom_core::waveStepFor(0, 1));
	TEST_ASSERT_EQUAL_INT(0, retroroom_core::waveStepFor(0, -5));
	TEST_ASSERT_EQUAL_INT(0, retroroom_core::waveStepFor(-1, 10));
	// An LED index past the end clamps rather than reading past the
	// table, which is the whole point.
	TEST_ASSERT_EQUAL_INT(255, retroroom_core::waveStepFor(1000, 10));
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();
	RUN_TEST(test_the_palette_set_is_intact);
	RUN_TEST(test_every_palette_name_matches_its_slot);
	RUN_TEST(test_a_fresh_show_is_not_started);
	RUN_TEST(test_reset_starts_on_the_first_palette);
	RUN_TEST(test_reset_seeds_the_clock);
	RUN_TEST(test_a_frame_lights_the_strip);
	RUN_TEST(test_the_frame_advances_the_clock);
	RUN_TEST(test_the_wave_moves);
	RUN_TEST(test_nothing_is_written_outside_the_buffer);
	RUN_TEST(test_a_zero_length_strip_writes_nothing);
	RUN_TEST(test_one_led_strip_does_not_divide_by_zero);
	RUN_TEST(test_every_channel_stays_in_range);
	RUN_TEST(test_the_clock_may_wrap);
	RUN_TEST(test_the_palette_advances_after_its_dwell);
	RUN_TEST(test_the_fade_is_the_tail_of_the_dwell);
	RUN_TEST(test_no_fade_when_the_config_asks_for_none);
	RUN_TEST(test_a_fade_longer_than_the_dwell_is_clamped);
	RUN_TEST(test_next_and_prev_wrap);
	RUN_TEST(test_select_wraps_from_either_end);
	RUN_TEST(test_selecting_resets_the_fade);
	RUN_TEST(test_a_long_run_stays_inside_the_playlist);
	RUN_TEST(test_the_frame_blends_over_what_was_there);
	RUN_TEST(test_the_smear_comes_from_reusing_the_buffer);
	RUN_TEST(test_every_led_maps_inside_the_sample_table);
	RUN_TEST(test_the_mapping_spans_the_whole_table);
	RUN_TEST(test_the_mapping_never_moves_backwards);
	RUN_TEST(test_the_mapping_survives_degenerate_strips);
	return UNITY_END();
}
