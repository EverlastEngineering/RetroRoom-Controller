#include "lighting.h"



CRGB leds[NUM_LEDS];

void lighting_init() {
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
}
