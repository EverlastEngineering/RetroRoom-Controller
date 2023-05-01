
#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>

#include "status.h"
#include "network.h"
#include "Consoles.h"

#define NUM_LEDS 10
#define DATA_PIN 2

// #define HARD_RESET

CRGB leds[NUM_LEDS];

void setup() {
#ifdef HARD_RESET // force reset code, set to true to nuke eeprom saved wifi
				  // info
	Serial.println("Resetting");
	delay(1000);
	WiFi.disconnect();
	ESP.eraseConfig();
	delay(1000);
	*((int *)0) = 0; // boom
	return;
#endif // end force reset
	Serial.begin(76800); // native esp8266 speed. Also, upload is possible at 6 times this rate, 460800.
	pinMode(LED_BUILTIN, OUTPUT);
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
	initializeNetwork();
	Serial.println("Running!");
	flashLed();

	Consoles consoles;
	int cc = consoles.getCurrentConsole();
	Serial.print("Current Console: ");
	Serial.println(cc);
	cc = consoles.setCurrentConsole(2);
	Serial.print("Current Console: ");
	Serial.println(cc);
}

void loop() {
	if (flash) {
		flashLed();
	}
}
