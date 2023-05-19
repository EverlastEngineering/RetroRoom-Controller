
/* FastLED Library */
#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>
#define NUM_LEDS 10
#define DATA_PIN D4 
CRGB leds[NUM_LEDS];

/* Rotary Encoder Library */
#include <Arduino.h>
#include <RotaryEncoder.h>

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_NANO_EVERY)
	#define PIN_IN1 A2
	#define PIN_IN2 A3
#elif defined(ESP8266)
	#define PIN_IN1 D5
	#define PIN_IN2 D6
#endif

/* Our own headers */
#include "status.h"
#include "network.h"
#include "Console.h"

/* HARD_RESET is a hack to completely nuke the onboard PRAM (or is it SRAM or..) that contains the saved wifi settings */
// #define HARD_RESET

/**
 * To Do
 * 
 * - make the list of consoles editable from the web UI <- ooo
 * - make the console class settings save to whatever it is the wifi saves to, so it remembers your console selection
 * 
*/

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
	
	/**
	 * Console takes:
	 * led_position: the position of the first led on the rgb string for this console
	 * selector_position: the position in the selector system
	 * name: the friendly name of the console )
	*/
	Console consoles[2] = {
		Console(1,5,1,"NES"),
		Console(2,5,2,"SNES")
	};
	Serial.println(consoles[0].name.c_str());
}

void loop() {
	if (flash) {
		flashLed();
	}
}
