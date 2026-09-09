#include "lighting.h"



CRGB leds[NUM_LEDS];
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
		fadeToBlackBy(leds,NUM_LEDS,1);
		ringFading = true;
		FastLED.show();
		if (leds[currentRingLED].r + leds[currentRingLED].b + leds[currentRingLED].g == 0) {
			ringFading = ringLit = false;
			// Serial.println("Fade To Black Complete");
		}
	}
}

void lighting_init() {
	// FastLED 3.10+ on RP2040. The addLeds clockless helper signature
	// that binds to a 3-arg `<CHIPSET, DATA_PIN, RGB_ORDER>` call is the
	// one the upstream FastLED RP2040 examples use, and it binds cleanly
	// to WS2812B (which is `template<uint8_t DATA_PIN, EOrder RGB_ORDER>
	// class WS2812B : public WS2812Controller800Khz<DATA_PIN, RGB_ORDER>`).
	// GRB is what WS2812 / NeoPixel / the ring on DATA_PIN expect.
	//
	// DATA_PIN is #undef'd by lighting.h before FastLED.h is included
	// (because FastLED's rp2040 backend uses DATA_PIN as a template
	// parameter name). We use RR_FASTLED_DATA_PIN, defined in lighting.h
	// to the numeric pin number from configuration.h.
	FastLED.addLeds<WS2812B, RR_FASTLED_DATA_PIN, GRB>(leds, NUM_LEDS);
	FastLED.setBrightness(LED_BRIGHTNESS);

	// Boot smoke test: flash red -> green -> blue once, then clear.
	// This is the first end-to-end test that the PIO WS2812 driver is
	// actually outputting a valid waveform on DATA_PIN. On the YD-RP2040
	// dev board this lights the onboard WS2812 on GP23 in three colors;
	// on the perfboard build (DATA_PIN=4) it lights the external ring.
	// If the LED stays dark, the PIO/clockless path is broken.
	const struct { CRGB color; const char *name; } flash[] = {
		{CRGB::Red,    "red"},
		{CRGB::Green,  "green"},
		{CRGB::Blue,   "blue"},
	};
	for (auto &f : flash) {
		fill_solid(leds, NUM_LEDS, f.color);
		FastLED.show();
		Serial.print("Lighting smoke test: ");
		Serial.println(f.name);
		delay(250);
	}
	fill_solid(leds, NUM_LEDS, CRGB::Black);
	FastLED.show();
	Serial.println("Lighting init done.");
}

void lightSingle (int led) {
	// Serial.print("Lit pixel #");
	// Serial.println(currentRingLED);
	fill_solid(leds, NUM_LEDS, CRGB::DarkBlue);
	// fill_rainbow(leds,NUM_LEDS,50,32);
	// fadeLightBy(leds,NUM_LEDS,150);
	leds[led] = CRGB::White;
	FastLED.show();
}

void ringLEDNext() {
	currentRingLED++;
	if (currentRingLED > NUM_LEDS-1) currentRingLED = 0;
	lightSingle(currentRingLED);
}

void ringLEDPrevious() {
	if (currentRingLED == 0) currentRingLED = NUM_LEDS-1;
	else currentRingLED--;
	lightSingle(currentRingLED);
}