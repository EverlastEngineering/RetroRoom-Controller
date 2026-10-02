// The wave maths. See LightShow.h for what this is and why it is a core.
//
// Everything FastLED's `colorwaveswithpalettes.ino` got from the library
// -- sin16, beatsin88, scale8, nblend, ColorFromPalette -- is
// reimplemented here in plain C++. That is the whole reason this file
// exists rather than the sketch's maths sitting in src/: the parts worth
// checking are exactly the parts the library was doing, and a test that
// can only run with a strip attached cannot check them.
//
// Each function below says which one it replaced.

#include "LightShow.h"

#include "SineTable.h"

namespace retroroom_core {

namespace {

// FastLED's sin16(): a 16-bit phase to a signed 16-bit sine.
//
// Interpolated between the 256-entry table rather than served from a
// 64 KB one. A 64 KB table on a board with 512 KB of RAM is not a trade
// worth making for an ambient effect, and the wave does not need 16-bit
// precision in the amplitude -- only in the phase, which is what this
// takes.
int32_t sin16(uint16_t angle) {
	const uint8_t i = static_cast<uint8_t>(angle >> 8);
	const uint8_t frac = static_cast<uint8_t>(angle & 0xFF);
	const int32_t a = kSinTable[i];
	const int32_t b = kSinTable[static_cast<uint8_t>(i + 1)];
	return a + (((b - a) * frac) / 256);
}

// FastLED's scale8(): v scaled by scale/256.
int32_t scale8(int32_t v, int32_t scale) { return (v * scale) / 256; }

// FastLED's scale16(): v scaled by scale/65536.
int32_t scale16(int32_t v, int32_t scale) { return (v * scale) / 65536; }

uint8_t clamp8(int v) {
	if (v < 0) {
		return 0;
	}
	if (v > 255) {
		return 255;
	}
	return static_cast<uint8_t>(v);
}

// FastLED's nblend() for one channel: `a` moved toward `b` by t/256.
//
// Divided rather than shifted, and signed rather than wrapping inside
// uint8_t. The original leans on 8-bit wraparound to make a downward
// blend come out right; that is defined on a byte and undefined on a
// signed shift, and this is a value a test asserts on. The cost is one
// clamp; the benefit is that a=10, b=0, t=128 gives 5 on every
// compiler rather than 133 on the ones that wrap.
int blendChannel(int a, int b, int t) {
	return clamp8(a + ((b - a) * t) / 256);
}

// FastLED's beat88(): a 16-bit sawtooth at a given BPM, Q8.8.
//
// Unsigned arithmetic, and the multiplication is allowed to wrap. That
// is not an oversight: the product overflows 32 bits for every BPM the
// sketch uses (65535 * 400 * 280 is 7.3e9), it overflowed on the AVR
// too, and the wrap is harmless because the result is a phase. Written
// signed it would be undefined; written unsigned the wrap is the
// deliberate thing it always was.
uint16_t beat88(uint32_t bpm88, uint32_t nowMs) {
	return static_cast<uint16_t>((nowMs * bpm88 * 280u) >> 16);
}

// FastLED's beatsin88(): that sawtooth through a sine, into [lo, hi].
int32_t beatsin88(uint32_t bpm88, int32_t lo, int32_t hi, uint32_t nowMs) {
	const int32_t beatsin = sin16(beat88(bpm88, nowMs)) + 32768;
	int32_t result = lo + scale16(beatsin, hi - lo);
	if (result > hi) {
		result = hi;
	}
	return result;
}

// FastLED's ColorFromPalette(), for a palette of `length` stops, at
// full brightness.
//
// The index is spread across `length - 1` segments in 1/256ths, so
// index 0 is the first stop and index 255 is the last. Spreading it
// across `length` segments instead -- which is nearer what FastLED's
// gradient path does -- puts the final stop on the far side of a
// segment nothing can ever land on, so a palette's last colour is one
// the show never actually shows.
Rgb stopAt(const GradientPalette& p, int index) {
	const int len = p.length < 2 ? 2 : p.length;
	const int segs = len - 1;
	int pos = (index * segs * 256) / 255;
	int slot = pos / 256;
	int frac = pos % 256;
	if (slot >= segs) {
		slot = segs - 1;
		frac = 255;
	}
	// Belt and braces: never read past what the palette defines,
	// however the arithmetic above fell out. A palette whose length
	// field disagrees with its data gets clamped rather than a colour
	// out of the zeroed tail.
	if (slot + 1 >= p.length) {
		slot = p.length - 2;
		frac = 255;
	}
	const uint8_t* a = p.rgb[slot];
	const uint8_t* b = p.rgb[slot + 1];
	Rgb c;
	c.r = static_cast<uint8_t>(blendChannel(a[0], b[0], frac));
	c.g = static_cast<uint8_t>(blendChannel(a[1], b[1], frac));
	c.b = static_cast<uint8_t>(blendChannel(a[2], b[2], frac));
	return c;
}

// A palette resolved to 16 evenly spaced stops, so the per-pixel lookup
// below is a table index rather than a walk.
//
// FastLED's CRGBPalette16 is already this shape, which is the shape the
// name promises. Building it once per frame is the cheap version of the
// original, which kept a live palette and walked it toward the target
// with nblendPaletteTowardPalette on a timer of its own -- state
// advanced somewhere other than where the frame is drawn, so a frame
// depended on two clocks.
const int kRampStops = 16;

void buildRamp(int playlistIndex, int blend, Rgb* out) {
	const int hereSlot =
		((playlistIndex % kPlaylistCount) + kPlaylistCount) % kPlaylistCount;
	const int nextSlot = (hereSlot + 1) % kPlaylistCount;
	const int here = kPlaylist[hereSlot];
	const int next = kPlaylist[nextSlot];
	const int t = (blend * (kRampStops - 1)) / 255;
	const int back = kRampStops - 1 - t;
	for (int i = 0; i < kRampStops; ++i) {
		const int idx = (i * 255) / (kRampStops - 1);
		const Rgb a = stopAt(kPalettes[here], idx);
		const Rgb b = stopAt(kPalettes[next], idx);
		out[i].r = static_cast<uint8_t>((a.r * back + b.r * t) /
		                                (kRampStops - 1));
		out[i].g = static_cast<uint8_t>((a.g * back + b.g * t) /
		                                (kRampStops - 1));
		out[i].b = static_cast<uint8_t>((a.b * back + b.b * t) /
		                                (kRampStops - 1));
	}
}

// Colour from the resolved ramp, dimmed to brightness/255.
Rgb rampAt(const Rgb* ramp, int index, int brightness) {
	const int pos = (index * (kRampStops - 1) * 256) / 255;
	int slot = pos / 256;
	int frac = pos % 256;
	if (slot >= kRampStops - 1) {
		slot = kRampStops - 2;
		frac = 255;
	}
	Rgb c;
	c.r = static_cast<uint8_t>(scale8(
		blendChannel(ramp[slot].r, ramp[slot + 1].r, frac), brightness));
	c.g = static_cast<uint8_t>(scale8(
		blendChannel(ramp[slot].g, ramp[slot + 1].g, frac), brightness));
	c.b = static_cast<uint8_t>(scale8(
		blendChannel(ramp[slot].b, ramp[slot + 1].b, frac), brightness));
	return c;
}

}  // namespace

const char* lightShowPaletteName(int playlistIndex) {
	if (playlistIndex < 0 || playlistIndex >= kPlaylistCount) {
		return "?";
	}
	return kPaletteNames[playlistIndex];
}

bool lightShowStarted(const LightShowState& s) {
	return s.playlistIndex >= 0 && kPlaylistCount > 0;
}

void lightShowReset(LightShowState* s, uint32_t nowMs) {
	if (s == nullptr) {
		return;
	}
	s->playlistIndex = kPlaylistCount > 0 ? 0 : -1;
	s->blend = 0;
	s->pseudotime = 0;
	s->hue = 0;
	// Seeded rather than left at zero. The first frame's delta would
	// otherwise be the whole time since boot, which on a cabinet powered
	// up an hour ago is a hue jump of about a quarter of a million
	// degrees and a pseudotime jump that lands the brightness
	// oscillator somewhere arbitrary.
	s->lastFrameMs = nowMs;
	s->paletteChangeAtMs = nowMs;
}

void lightShowSelectPalette(LightShowState* s, int playlistIndex) {
	if (s == nullptr || kPlaylistCount == 0) {
		return;
	}
	s->playlistIndex =
		((playlistIndex % kPlaylistCount) + kPlaylistCount) % kPlaylistCount;
	s->blend = 0;
}

void lightShowNextPalette(LightShowState* s) {
	lightShowSelectPalette(s, (s == nullptr ? 0 : s->playlistIndex) + 1);
}

void lightShowPrevPalette(LightShowState* s) {
	lightShowSelectPalette(s, (s == nullptr ? 0 : s->playlistIndex) - 1);
}

void computeColorWaveFrame(LightShowState& s, const LightShowConfig& cfg,
                           int numLeds, uint32_t nowMs, Rgb* out) {
	if (out == nullptr || numLeds <= 0 || kPlaylistCount == 0) {
		return;
	}
	// `s` is advanced in place. Painting a frame IS advancing the show;
	// there is no version of the wave that does not move it, and a
	// signature suggesting otherwise would be a lie the caller could
	// act on.
	if (!lightShowStarted(s)) {
		lightShowReset(&s, nowMs);
	}

	// Unsigned subtraction, so the wrap at 2^32 is a wrap rather than a
	// multi-billion-millisecond negative. The same idiom as every other
	// deadline in this firmware.
	const uint32_t delta = nowMs - s.lastFrameMs;
	s.lastFrameMs = nowMs;

	// The fade is the tail of the dwell, and it advances with elapsed
	// time rather than per frame. See LightShowConfig::paletteFadeMs for
	// why that is not the same as the sketch's "16 blend steps every
	// 40 ms".
	const int dwell = cfg.paletteDwellMs > 0 ? cfg.paletteDwellMs : 1;
	const int fade = cfg.paletteFadeMs < 0
	                     ? 0
	                     : (cfg.paletteFadeMs > dwell ? dwell
	                                                 : cfg.paletteFadeMs);
	const uint32_t intoDwell = nowMs - s.paletteChangeAtMs;
	if (static_cast<uint32_t>(dwell) <= intoDwell) {
		// Dwell is up: advance, and carry the overshoot so a frame that
		// lands late does not also lose the time it was late by.
		const uint32_t over = intoDwell - static_cast<uint32_t>(dwell);
		s.playlistIndex = (s.playlistIndex + 1) % kPlaylistCount;
		s.blend = 0;
		s.paletteChangeAtMs = nowMs + over;
	} else if (fade > 0 &&
	           static_cast<uint32_t>(intoDwell) >=
	               static_cast<uint32_t>(dwell - fade)) {
		s.blend = (static_cast<int>(intoDwell) - (dwell - fade)) * 255 /
		          fade;
	}

	// ---- the wave, from colorwaves() in the sketch --------------------
	//
	// The four oscillators, all off the same clock. None of them takes a
	// clock argument in FastLED -- they read millis() themselves -- which
	// is why nowMs is threaded through the helpers above rather than
	// read from a global. A frame that can be asked for a time can be
	// tested at that time, and that is the whole reason any of this is a
	// core rather than src/.
	//
	// The sketch also computes a `sat8` from triwave8() and never uses
	// it. Not ported: dead code is worth deleting, not translating.
	const int32_t brightdepth = beatsin88(341, 96, 224, nowMs);
	const int32_t brightnessthetainc =
		beatsin88(203, 25 * 256, 40 * 256, nowMs);
	const int32_t msmultiplier = beatsin88(147, 23, 60, nowMs);
	const int32_t hueinc = beatsin88(113, 300, 1500, nowMs);

	Rgb ramp[kRampStops];
	buildRamp(s.playlistIndex, s.blend, ramp);

	s.pseudotime = static_cast<uint16_t>(
		s.pseudotime +
		static_cast<uint32_t>(delta) * static_cast<uint32_t>(msmultiplier));
	s.hue = static_cast<uint16_t>(
		s.hue + static_cast<uint32_t>(delta) *
		            static_cast<uint32_t>(beatsin88(400, 5, 9, nowMs)));

	// 256 steps of the wave, sampled once and indexed by per-pixel
	// position below. The wave is a function of one index and a frame's
	// worth of state, so 256 samples cover any strip length; evaluating
	// the oscillators per *pixel* instead would make a 118-LED strip and
	// a 512-LED strip do the same total work, which is backwards.
	Rgb wave[256];
	{
		uint16_t h = s.hue;
		uint16_t btheta = s.pseudotime;
		for (int step = 0; step < 256; ++step) {
			h = static_cast<uint16_t>(h + static_cast<uint16_t>(hueinc));
			const uint16_t h16_128 = static_cast<uint16_t>(h >> 7);
			const int hue8 = (h16_128 & 0x100) ? (255 - (h16_128 >> 1))
			                                   : (h16_128 >> 1);
			btheta = static_cast<uint16_t>(
				btheta + static_cast<uint16_t>(brightnessthetainc));
			const int32_t b16 = sin16(btheta) + 32768;
			const int32_t bri16 = (b16 * b16) / 65536;
			const int32_t bri8 =
				(bri16 * brightdepth) / 65536 + (255 - brightdepth);
			// The sketch's scale8(index, 240): 240/256 of the range, so
			// the last few percent of the palette is never reached. Kept,
			// because it is why these shows do not use the whole palette
			// and changing it changes the look.
			wave[step] = rampAt(ramp, (hue8 * 240) / 255,
			                    static_cast<int>(bri8));
		}
	}

	for (int i = 0; i < numLeds; ++i) {
		const int step = (i * 256) / (numLeds > 1 ? numLeds - 1 : 1);
		const Rgb c = wave[step];
		// The strip runs the other way: the sketch walks the wave from
		// the far end back to zero, so the motion runs the way the
		// pixels are numbered.
		Rgb& px = out[numLeds - 1 - i];
		px.r = static_cast<uint8_t>(blendChannel(px.r, c.r, 128));
		px.g = static_cast<uint8_t>(blendChannel(px.g, c.g, 128));
		px.b = static_cast<uint8_t>(blendChannel(px.b, c.b, 128));
	}
}

}  // namespace retroroom_core
