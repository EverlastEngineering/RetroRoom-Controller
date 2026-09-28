// Host-side Unity tests for lib/LedStringPaint's computeConsoleWindow().
//
// The shell-side paint code lives in src/ledstring.cpp and depends on
// FastLED + Arduino -- not available in [env:test_native]. Lifting the
// paint decision ("which pixel range should be lit given a console's
// ledPosition/ledWidth?") into the functional core lets us unit-test
// the decision on the host without emulating FastLED or the WS2812B
// strip.
//
// Invariants under test:
//   1. Happy path: a console in the middle of the strip returns a
//      window whose start equals ledPosition and whose width equals
//      ledWidth.
//   2. A console at the very start (ledPosition == 0) returns a
//      window that starts at 0.
//   3. A console whose declared width would overflow totalLeds gets
//      its window truncated -- not dropped, not wrapped -- to the
//      strip boundary.
//   4. A console with ledWidth <= 0 returns {0, 0} (paint nothing).
//   5. A console with ledPosition < 0 returns {0, 0} (malformed JSON
//      defense -- a negative ledPosition is a schema violation but we
//      fail safe rather than crash).
//   6. A console with ledPosition >= totalLeds returns {0, 0} (the
//      window starts past the end of the strip -- there's nothing to
//      paint; the operator must fix the config).
//   7. A totalLeds of 0 returns {0, 0} (defensive -- the host stub
//      calling this with a zero-sized buffer won't accidentally paint
//      out-of-bounds memory).

#include <LedStringPaint.h>
#include <unity.h>

using retroroom_core::LedRange;
using retroroom_core::computeConsoleWindow;

void setUp(void) {}
void tearDown(void) {}

void test_window_in_middle_of_strip(void) {
	// NES-style: position 7, width 5, into a 64-LED strip.
	// Mirrors the SMS entry in example-configurations/example2.json.
	LedRange r = computeConsoleWindow(7, 5, 64);
	TEST_ASSERT_EQUAL(7, r.start);
	TEST_ASSERT_EQUAL(5, r.width);
}

void test_window_at_start_of_strip(void) {
	// NES at offset 1 with width 1 (a single LED right at the start).
	LedRange r = computeConsoleWindow(1, 1, 64);
	TEST_ASSERT_EQUAL(1, r.start);
	TEST_ASSERT_EQUAL(1, r.width);
}

void test_window_at_pixel_zero(void) {
	// Boundary: ledPosition == 0 is the most-starting legal value.
	LedRange r = computeConsoleWindow(0, 3, 64);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(3, r.width);
}

void test_window_truncates_when_overflowing_strip(void) {
	// MAME-style: ledPosition=27, ledWidth=15 on a 64-LED strip is
	// exactly 27+15=42, which fits. But bump ledWidth to 50 -- the
	// window now wants pixels [27, 77) but the strip ends at 63.
	// The window MUST be clamped to [27, 64), not wrapped to [27, 13)
	// or dropped entirely.
	LedRange r = computeConsoleWindow(27, 50, 64);
	TEST_ASSERT_EQUAL(27, r.start);
	TEST_ASSERT_EQUAL(37, r.width);  // 64 - 27 = 37
}

void test_window_exactly_at_strip_end_returns_full_width(void) {
	// ledPosition=20, ledWidth=44, totalLeds=64.
	// 20 + 44 == 64, so the window covers the last 44 pixels exactly
	// -- no truncation needed, no overflow.
	LedRange r = computeConsoleWindow(20, 44, 64);
	TEST_ASSERT_EQUAL(20, r.start);
	TEST_ASSERT_EQUAL(44, r.width);
}

void test_ledwidth_zero_returns_no_window(void) {
	// Console says "no strip mapping" -- don't light anything.
	LedRange r = computeConsoleWindow(5, 0, 64);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(0, r.width);
}

void test_ledwidth_negative_returns_no_window(void) {
	// Malformed JSON defense.
	LedRange r = computeConsoleWindow(5, -1, 64);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(0, r.width);
}

void test_ledposition_negative_returns_no_window(void) {
	// Malformed JSON defense.
	LedRange r = computeConsoleWindow(-1, 5, 64);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(0, r.width);
}

void test_ledposition_at_strip_end_returns_no_window(void) {
	// The window starts AT totalLeds -- there are no pixels to paint.
	// Returns {0, 0} rather than a zero-width "valid" window so
	// callers can detect "nothing lit" with a single width==0 check.
	LedRange r = computeConsoleWindow(64, 1, 64);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(0, r.width);
}

void test_ledposition_past_strip_end_returns_no_window(void) {
	LedRange r = computeConsoleWindow(100, 5, 64);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(0, r.width);
}

void test_zero_total_leds_returns_no_window(void) {
	// Defensive: an uninitialized strip is a config error, but we
	// mustn't write out of bounds.
	LedRange r = computeConsoleWindow(0, 5, 0);
	TEST_ASSERT_EQUAL(0, r.start);
	TEST_ASSERT_EQUAL(0, r.width);
}

// Unity's setUp/tearDown + main. PlatformIO's Unity harness auto-runs
// main() if it's not provided. Including the default implementation
// here so the test binary links on its own (not relying on the
// framework's runner injecting the entry point).
int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();
	RUN_TEST(test_window_in_middle_of_strip);
	RUN_TEST(test_window_at_start_of_strip);
	RUN_TEST(test_window_at_pixel_zero);
	RUN_TEST(test_window_truncates_when_overflowing_strip);
	RUN_TEST(test_window_exactly_at_strip_end_returns_full_width);
	RUN_TEST(test_ledwidth_zero_returns_no_window);
	RUN_TEST(test_ledwidth_negative_returns_no_window);
	RUN_TEST(test_ledposition_negative_returns_no_window);
	RUN_TEST(test_ledposition_at_strip_end_returns_no_window);
	RUN_TEST(test_ledposition_past_strip_end_returns_no_window);
	RUN_TEST(test_zero_total_leds_returns_no_window);
	return UNITY_END();
}
