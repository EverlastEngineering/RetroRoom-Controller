#include "lighting.h"



CRGB leds[NUM_RING_LEDS];
#define LED_BRIGHTNESS 150

int currentRingLED = 0;
bool ringLit = false;
bool ringFading = false;

void lightRing(bool lit) {
	if (lit && (ringFading || !ringLit)) {
		ringLit = true;
		ringFading = false;
		lightSingle(currentRingLED);
		FastLED.show();
	}
	else if (!lit && ringLit) {
		fadeToBlackBy(leds,NUM_RING_LEDS,1);
		ringFading = true;
		FastLED.show();
		if (leds[currentRingLED].r + leds[currentRingLED].b + leds[currentRingLED].g == 0) {
			ringFading = ringLit = false;
			// Serial.println("Fade To Black Complete");
		}
	}
}

// Drives the lightRing(false) fade to completion. lightRing() takes a
// single fadeToBlackBy step per call and re-arms ringFading, so whoever
// asked for the ring to go dark has to keep asking until ringFading
// clears itself.
//
// lightSingle() / ringLEDNext() / ringLEDPrevious() do not consult
// ringFading -- they repaint unconditionally -- so turning the encoder
// during a fade simply overwrites it, which is the wanted behaviour:
// the operator is actively using the control.
void lighting_loop() {
	if (ringFading) {
		lightRing(false);
	}
}

void lighting_init() {
	// FastLED 3.10+ on RP2040 / RP2350 (the rpcommon PIO backend
	// transparently supports both chips). The addLeds clockless helper
	// signature that binds to a 3-arg `<CHIPSET, DATA_PIN, RGB_ORDER>`
	// call is the one the upstream examples use, and it binds cleanly
	// to WS2812B (which is `template<uint8_t DATA_PIN, EOrder RGB_ORDER>
	// class WS2812B : public WS2812Controller800Khz<DATA_PIN, RGB_ORDER>`).
	// GRB is what WS2812 / NeoPixel / the ring on DATA_PIN expect.
	//
	// DATA_PIN is #undef'd by lighting.h before FastLED.h is included
	// (because FastLED's rp2040 backend uses DATA_PIN as a template
	// parameter name). We use RR_FASTLED_DATA_PIN, defined in lighting.h
	// to the numeric pin number from configuration.h.
	FastLED.addLeds<WS2812B, LED_RING_DATA_PIN, GRB>(leds, NUM_RING_LEDS);
	FastLED.setBrightness(LED_BRIGHTNESS);
	// Clear the ring at boot. The previous boot-time R/G/B smoke test was
	// removed on session/merge-pico-json (per user request); the LED will
	// stay dark until something (lightSingle, lightRing, lightCycleTick,
	// etc.) drives it.
	fill_solid(leds, NUM_RING_LEDS, CRGB::Black);
	FastLED.show();
}

// Continuous RGB-cycle smoke test for the YD-RP2040 onboard WS2812.
// Called from loop() on the pico_yd env (and harmless on the other Pico
// envs since FastLED.show() is cheap). Cycles red -> green -> blue with
// a soft crossfade so the PIO driver is repeatedly exercised at boot-time
// cadence. Returns nothing; uses static state to track current color and
// last update tick.
//
// lightCycleEnabled defaults true and nothing in the current build
// toggles it. When disabled, lightCycleTick() blacks out the LED strip
// and returns immediately; the FastLED PIO driver stays initialized but
// no frames are pushed.
//
// NOTE: this is a temporary smoke-test helper. Once the StackSelector
// perfboard lands and the rotary encoder drives ringLEDNext / ringLEDPrevious,
// this function should be removed (or gated on a build flag).
static bool lightCycleEnabled = true;
static int lightCyclePhase = 0;
static unsigned long lightCycleLastUpdate = 0;
static bool lightCycleNeedsBlack = false;

bool lightCycleIsEnabled() { return lightCycleEnabled; }
void lightCycleToggle() {
	// No current caller. If this is rebound to a button, note that
	// EasyButton's onPressed() fires on the RELEASE edge, not the press.
	lightCycleEnabled = !lightCycleEnabled;
	Serial.print("USR toggle -> lightCycle ");
	Serial.println(lightCycleEnabled ? "ON" : "OFF");
	if (!lightCycleEnabled) {
		// Force the strip to black on the next tick and reset the phase
		// so re-enabling starts cleanly from red.
		lightCycleNeedsBlack = true;
		lightCyclePhase = 0;
		lightCycleLastUpdate = 0;
	}
}

void lightCycleTick() {
	const unsigned long cyclePeriodMs = 1000;  // 1 second per color

	if (!lightCycleEnabled) {
		// Push one black frame after a toggle-off, then idle until
		// the user re-enables.
		if (lightCycleNeedsBlack) {
			fill_solid(leds, NUM_RING_LEDS, CRGB::Black);
			FastLED.show();
			lightCycleNeedsBlack = false;
			Serial.println("lightCycle: strip cleared");
		}
		return;
	}

	unsigned long now = millis();
	if (now - lightCycleLastUpdate < cyclePeriodMs) {
		return;
	}
	lightCycleLastUpdate = now;

	lightCyclePhase = (lightCyclePhase + 1) % 3;
	switch (lightCyclePhase) {
	case 0:
		fill_solid(leds, NUM_RING_LEDS, CRGB::Red);
		Serial.println("lightCycle: red");
		break;
	case 1:
		fill_solid(leds, NUM_RING_LEDS, CRGB::Green);
		Serial.println("lightCycle: green");
		break;
	case 2:
		fill_solid(leds, NUM_RING_LEDS, CRGB::Blue);
		Serial.println("lightCycle: blue");
		break;
	}
	FastLED.show();
}

void lightSingle (int led) {
	// Serial.print("Lit pixel #");
	// Serial.println(currentRingLED);
	fill_solid(leds, NUM_RING_LEDS, CRGB::DarkBlue);
	// fill_rainbow(leds,NUM_LEDS,50,32);
	// fadeLightBy(leds,NUM_LEDS,150);
	leds[led] = CRGB::White;
	FastLED.show();
}

void ringLEDNext() {
	currentRingLED++;
	if (currentRingLED > NUM_RING_LEDS-1) currentRingLED = 0;
	lightSingle(currentRingLED);
}

void ringLEDPrevious() {
	if (currentRingLED == 0) currentRingLED = NUM_RING_LEDS-1;
	else currentRingLED--;
	lightSingle(currentRingLED);
}