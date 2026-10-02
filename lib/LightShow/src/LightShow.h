#pragma once

// The cyclical light show, as a pure core.
//
// Ported from Mark Kriegsman's `colorwaveswithpalettes.ino` (FastLED's
// own example sketch, August 2015), which drives a strip of colour
// waves through a cross-fading playlist of cpt-city gradient palettes.
// This file is that sketch's *decision* -- the phase maths, the palette
// blend, the per-pixel colour -- with none of its I/O.
//
// Same split as lib/LedStringPaint, and for the same reason: this
// compiles on the host under [env:test_native] with no Arduino and no
// FastLED, so the wave can be checked without a strip attached. The
// shell in src/ledstring.cpp owns millis(), owns the CRGB buffer, and
// calls FastLED.show().
//
// Two deliberate departures from the original, both because the original
// was written for an AVR where neither was a problem:
//
//   - The original truncates millis() to 16 bits. On a Pico that wraps
//     every 65.5 seconds. The clock is 32-bit here; the *accumulators*
//     (pseudotime, hue) stay 16-bit because wrapping them is what makes
//     them oscillators.
//
//   - The original's per-pixel work calls FastLED's triwave8, sin16,
//     beatsin88, scale8, nblend and ColorFromPalette. Those are
//     reimplemented below in plain C++ so the core has no dependency on
//     the library, and so their behaviour is something this repo can
//     state and test rather than inherit. The maths is the same shape;
//     see the individual functions for what each one is.

#include <cstddef>
#include <cstdint>

namespace retroroom_core {

// An 8-bit RGB triple. Deliberately its own type rather than
// retroroom_core::LedColor from lib/LedStringPaint: this library has no
// reason to depend on the paint library, and the two are never mixed in
// one frame.
struct Rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

// A gradient palette: 2..16 colour stops, interpolated between.
//
// The length is carried because the cpt-city set uses 2 to 5 stops and
// does not pad to 16 -- FastLED's DEFINE_GRADIENT_PALETTE makes a
// palette of whatever length you wrote. A 16-entry struct with a
// length field keeps the array a fixed stride (so it can be indexed
// without an offset table) without pretending the unused stops are
// real.
struct GradientPalette {
	uint8_t length;
	uint8_t rgb[16][3];
};

// The cpt-city palettes, in definition order. See LightShowPalettes.cpp
// for the provenance and the conversion.
extern const GradientPalette kPalettes[];
extern const int kPaletteCount;

// The playlist: indices into kPalettes, in the order they play.
//
// Separate from the table on purpose. The table is the data; this is
// the *set*, and trimming the show to the four palettes someone actually
// likes is deleting lines from here rather than from a 500-line array.
// The original sketch's `gGradientPalettes[]` was the same idea and had
// commented-out entries in it, which is the only editability it had.
extern const uint8_t kPlaylist[];
extern const int kPlaylistCount;

// Names, parallel to kPlaylist, so a cabinet sitting on
// "es_rivendell_15_gp" can be identified from the log or the web API
// without a photograph. The sketch had no names at all -- it was a
// demo -- so this is the one thing here the original does not have.
extern const char* const kPaletteNames[];

// The playlist index as a human name. Never null: an index outside the
// table answers "?" rather than reading off the end.
const char* lightShowPaletteName(int playlistIndex);

// How the show is driven. Both are timings, and both live here rather
// than as literals in the maths so the shell can fill them from config
// or from a constant without the core caring.
struct LightShowConfig {
	// How often a frame is pushed. Matches `led.frameIntervalMs`, which
	// is what the rest of the strip uses, so a cabinet that tuned it for
	// its own power budget does not have to tune it twice. The core does
	// not use it -- the shell owns the cadence -- but it lives here so
	// one struct describes the whole show.
	int frameIntervalMs = 16;
	// How long one palette is up, cross-fade included.
	int paletteDwellMs = 10000;
	// How long the cross-fade between two palettes takes, taken out of
	// the tail of the dwell.
	//
	// Time-proportional rather than "16 steps of a 256 blend every
	// 40 ms" as the original wrote it, because a step-per-frame-ish
	// rule makes the fade length depend on how often the loop happens
	// to run. At 16 ms frames the original faded in 640 ms; at 8 ms it
	// took 320. Same look at one speed, different look at another, on
	// a machine where the speed is set by what else the loop is doing.
	int paletteFadeMs = 1200;
};

// Everything the wave accumulates. Not a cache of the config and not a
// view of anything: this is the whole of the show's state, and a frame
// is a function of it plus a timestamp.
struct LightShowState {
	// Where in kPlaylist we are. -1 is "not started".
	int playlistIndex = -1;
	// How far through the cross-fade into the next palette, 0..255.
	// The original used FastLED's nblendPaletteTowardPalette with an
	// amount of 16 per 40 ms; this is the same 16, expressed as a
	// 0..255 progress so the maths is one multiply.
	int blend = 0;
	// 16-bit phase accumulators. They wrap, and that is the point: the
	// wrap IS the oscillation.
	uint16_t pseudotime = 0;
	uint16_t hue = 0;
	// 32-bit. The original's were 16-bit because millis() was; on a
	// Pico that would restart the whole show every 65.5 seconds.
	uint32_t lastFrameMs = 0;
	uint32_t paletteChangeAtMs = 0;
};

// Start, or restart, on the first playlist entry. `nowMs` seeds the
// clock so the first frame does not compute a delta against zero --
// which would be a two-minute first step.
void lightShowReset(LightShowState* s, uint32_t nowMs);

// Step to the next / previous palette in the playlist, wrapping. The
// cross-fade restarts from 0 either way: a backwards step in a
// cross-fading show is a backwards cross-fade, not a jump.
void lightShowNextPalette(LightShowState* s);
void lightShowPrevPalette(LightShowState* s);

// Jump straight to a playlist index, resetting the fade.
void lightShowSelectPalette(LightShowState* s, int playlistIndex);

// Paint one frame, and advance the show to match.
//
// `s` is advanced in place. Painting a frame IS advancing the show --
// there is no version of the wave that does not move it -- so the state
// is a reference rather than a copy, and a caller that wanted a frame
// without disturbing the show cannot have one.
//
// `out` is READ as well as written. The original blends each new colour
// halfway over whatever was already on the pixel, and that is where its
// trailing smear comes from: the wave leaves a wake behind it rather
// than replacing the frame. A caller that zeroes the buffer every time
// gets a harsher, more strobing show; one that hands the previous
// buffer back gets the original. Documented because it looks like a bug
// when it is done the other way round.
//
// Writes exactly `numLeds` entries. `numLeds` <= 0 writes nothing.
void computeColorWaveFrame(LightShowState& s, const LightShowConfig& cfg,
                           int numLeds, uint32_t nowMs, Rgb* out);

// True once a palette has been chosen. A show in state -1 has no
// playlist entry and nothing to fade from, so the shell must not paint
// one -- an unstarted show is a strip that is sitting on whatever the
// last frame left, which is worse than not starting.
bool lightShowStarted(const LightShowState& s);

}  // namespace retroroom_core
