#include "lighting.h"

#define NUM_LEDS 10
#define DATA_PIN D7

CRGB leds[NUM_LEDS];

void lighting_init() {
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
}
