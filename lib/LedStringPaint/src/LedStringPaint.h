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

}  // namespace retroroom_core
