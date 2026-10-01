#include "lighting.h"
#include "ledstring.h"
#include "consoles.h"       // ledFeel: the ring's timings come from the config
#include <ConsoleConfig.h>  // LedFeel: the strip's feel, parsed from the config
#include <RingPaint.h>

// The shell around lib/RingPaint.
//
// This file used to own the ring outright: six mutable statics, each
// function partially updating them, and a fade implemented as 255
// fadeToBlackBy(1) calls -- one per main-loop iteration, so the ring took
// as long to go out as the loop happened to take. It also kept the
// brightness in the pixel buffer as accumulated decrements, which meant
// any code path that cancelled a fade stranded the ring part-dimmed with
// no state left able to finish it.
//
// The decision half now lives in lib/RingPaint as a pure state machine
// with a single RingMode, host-tested in test/test_ring. What is left
// here is deliberately only:
//
//   * the CRGB buffer and the FastLED controller,
//   * mapping the config's `led` block onto a RingConfig,
//   * mapping the two colours the core scales onto that buffer, and
//   * forwarding the pad and encoder edges as events.
//
// If you find yourself adding a ring state variable here, it belongs in
// the core instead -- see the "Why this exists" note in RingPaint.h.

using retroroom_core::RingConfig;
using retroroom_core::RingPaint;
using retroroom_core::RingState;
using retroroom_core::RingUpdate;

CRGB leds[NUM_RING_LEDS];

// The master brightness, as a FastLED global scale, and the config
// value that multiplies it.
//
// This was a #define (LED_BRIGHTNESS 150) that read as though it
// belonged to the ring. It does not: FastLED's setBrightness() sets one
// mScale on the FastLED object which is applied to *every* controller
// on show(), so the number has always scaled the GP21 string as well.
// Nothing is changing behaviour here -- the comment was catching up
// with the code.
//
// The base stays fixed and only the percentage moves, because the base
// is the most the fitted supply can drive. led.brightnessPct stops at
// 100 for the same reason: asking for more is not a brighter cabinet,
// it is a brown-out when the strip is fully lit.
static const uint8_t kBrightnessBase = 150;



// How bright the dim base fill is at full level -- DarkBlue's blue
// channel, written as a number so the relationship to CRGB::DarkBlue is
// visible rather than implied. The core decides how bright; the colours
// that "bright" multiplies live here, in the shell, so colour choices
// stay out of the decision logic.
static const uint8_t kBaseBlue = 128;

// The ring's entire state, owned by the core. One object, one owner: the
// thing that made the old version hard to reason about was the same
// information spread across six booleans that any of five functions
// could contradict.
static RingState ringState;

// The last picture actually pushed to the wire, so an unchanged tick does
// not re-clock the ring. FastLED.show() is synchronous on the RP2040 PIO
// backend -- it blocks until the last bit is out -- so calling it every
// main-loop iteration, forever, for a ring that is almost always dark, is
// time taken away from the strip and the network for nothing.
static RingPaint lastPushed;
static bool hasPushed = false;

namespace {

// led.brightnessPct as a FastLED scale, with any runtime override on
// top.
//
// Read live and pushed on a change, not configured once at init. A
// cached copy of a brightness is a brightness nothing can change --
// the same mistake as the browse gate's detent thresholds, and the
// reason those now arrive per call. It is a 0..255 multiplier, applied
// to the pixels this file writes.
//
// It is deliberately NOT FastLED's setBrightness(). That sets one
// global mScale which the RP2040 PIO backend does not appear to
// honour -- brightnessPct was saved, survived a reboot, and the LEDs
// did not move. Scaling where the levels become pixels is one line, is
// the same shape as the strip's, and does not depend on how a
// particular backend happens to treat mScale.
uint8_t ledBrightnessScale() {
	return static_cast<uint8_t>(
		(static_cast<uint32_t>(255) *
		 static_cast<uint32_t>(lightBrightnessPct())) / 100u);
}

// The config's `led` block as the core wants it. Read fresh on every use
// rather than cached at init, because a cached copy of ledFeel is exactly
// what froze detentsPerStep in the browse gate and explodeMs in the
// strip's commit effect. The fix there was to stop caching, not to cache
// more carefully; see RingPaint.h for the long version.
RingConfig ringConfig() {
	RingConfig c;
	c.idleMs = static_cast<uint32_t>(ledFeel.ringIdleMs);
	c.flashMs = static_cast<uint32_t>(ledFeel.ringFlashMs);
	c.offDelayMs = static_cast<uint32_t>(ledFeel.ringOffDelayMs);
	c.fadeMs = static_cast<uint32_t>(ledFeel.ringFadeMs);
	c.pixelCount = NUM_RING_LEDS;
	return c;
}

bool samePaint(const RingPaint& a, const RingPaint& b) {
	return a.baseLevel == b.baseLevel &&
		   a.highlightLevel == b.highlightLevel &&
		   a.highlightIndex == b.highlightIndex &&
		   a.highlightCount == b.highlightCount;
}

// Write the core's answer into the buffer. Absolute levels, not
// increments -- the other half of why a fade can no longer strand: there
// is no running total here to fall out of step with.
void pushPaint(const RingPaint& p) {
	if (hasPushed && samePaint(p, lastPushed)) {
		return;
	}
	const uint8_t scale = ledBrightnessScale();
	const uint8_t base = static_cast<uint8_t>(
		(static_cast<uint32_t>(kBaseBlue) * p.baseLevel * scale) / (255u * 255u));
	for (int i = 0; i < NUM_RING_LEDS; ++i) {
		leds[i] = CRGB(0, 0, base);
	}
	for (int i = 0; i < p.highlightCount; ++i) {
		const int idx = p.highlightIndex + i;
		if (idx < 0 || idx >= NUM_RING_LEDS) {
			continue;
		}
		const uint8_t hot = static_cast<uint8_t>(
			(static_cast<uint32_t>(p.highlightLevel) * scale) / 255u);
		leds[idx] = CRGB(hot, hot, hot);
	}
	FastLED.show();
	lastPushed = p;
	hasPushed = true;
}

}  // namespace

// See lighting.h for the contract. Returns true on the single tick a fade
// completed, which is the cue for an abandoned browse to revert.
bool lighting_loop() {
	// The strip still owes the operator a retreat -- either waiting out
	// fillRetreatDelayMs before the first LED comes back, or part way
	// through giving them back. The ring holds its countdown and
	// withholds its "the browse is over" signal for the whole of it.
	//
	// Both halves matter. Clearing the browse mid-retreat empties the
	// strip in one step instead of letting it unwind, and clearing it
	// during the delay does the same thing earlier -- the run has not
	// even started giving itself back yet. So this asks
	// ledstring_browseRetreatPending() rather than the narrower
	// "is it running", which left the delay uncovered and made any
	// ringIdleMs below fillRetreatDelayMs snap the strip back every time.
	const RingUpdate u = retroroom_core::ringTick(
		ringState, millis(), ringConfig(),
		ledstring_browseRetreatPending());
	pushPaint(u.paint);
	return u.fadeCompleted;
}

void lightRingSetProximityHold(bool held) {
	// The pad's *reading*, not a change: the core detects the edges
	// itself. Passing an edge instead would let the two disagree about
	// what counted as one, which is precisely how the old commit cleared
	// the ring's hold without clearing the pad's memory of it -- and the
	// next edge then resurrected a hold the operator had ended.
	retroroom_core::ringProximity(ringState, millis(), held);
}

void lightRingSelectStrike() {
	retroroom_core::ringCommit(ringState, millis(), ringConfig());
}

void ringLEDNext() {
	retroroom_core::ringDetent(ringState, millis(), ringConfig(), 1);
}

void ringLEDPrevious() {
	retroroom_core::ringDetent(ringState, millis(), ringConfig(), -1);
}

void lighting_init() {
	// FastLED 3.10+ on RP2040 / RP2350 (the rpcommon PIO backend
	// transparently supports both chips). The addLeds clockless helper
	// signature that binds to a 3-arg `<CHIPSET, DATA_PIN, RGB_ORDER>`
	// call is the one the upstream examples use, and it binds cleanly to
	// WS2812B (which is `template<uint8_t DATA_PIN, EOrder RGB_ORDER>
	// class WS2812B : public ClocklessController<800KHz, DATA_PIN,
	// RGB_ORDER>`). GRB is what WS2812 / NeoPixel / the ring on DATA_PIN
	// expect.
	//
	// DATA_PIN is #undef'd by lighting.h before FastLED.h is included
	// (because FastLED's rp2040 backend uses DATA_PIN as a template
	// parameter name). We use RR_FASTLED_DATA_PIN, defined in lighting.h
	// to the numeric pin number from configuration.h.
	FastLED.addLeds<WS2812B, LED_RING_DATA_PIN, GRB>(leds, NUM_RING_LEDS);
	FastLED.setBrightness(kBrightnessBase);
	// Clear the ring at boot. The previous boot-time R/G/B smoke test was
	// removed on session/merge-pico-json (per user request); the LED will
	// stay dark until something drives it.
	//
	// The core's state is reset here rather than left to its
	// initialisers: lighting_init() is the ring's start of day, and the
	// two should not be able to disagree about it.
	ringState = RingState();
	lastPushed = RingPaint();
	hasPushed = false;
	fill_solid(leds, NUM_RING_LEDS, CRGB::Black);
	FastLED.show();
}
