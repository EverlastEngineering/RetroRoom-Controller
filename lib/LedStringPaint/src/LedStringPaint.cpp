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

}  // namespace retroroom_core
