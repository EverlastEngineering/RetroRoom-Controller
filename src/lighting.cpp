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
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
	FastLED.setBrightness(LED_BRIGHTNESS);
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