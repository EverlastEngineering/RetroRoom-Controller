#include "lighting.h"



CRGB leds[NUM_LEDS];
#define LED_BRIGHTNESS 150

int currentRingLED = 0;

void lighting_init() {
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
	FastLED.setBrightness(LED_BRIGHTNESS);
}

void lightSingle (int led) {
	Serial.print("Lit pixel #");
	Serial.println(currentRingLED);
	fill_solid(leds, NUM_LEDS, CRGB::DarkBlue);
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