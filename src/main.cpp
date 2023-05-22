/**
 * RetroRoom for ESP8266
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 * 
 * To Do
 * - make the list of consoles editable from the web UI <- ooo
 * - make the console class settings save to whatever it is the wifi saves to, so it remembers your console selection
 * - add a secret mode using the easybutton onSequence for a light show
 * - see if we can remove the encoder tick call from the loop to purely use interupts
*/

/* Our own headers */
#include "main.h"
#include "controls.h"
#include "status.h"
#include "network.h"
#include "lighting.h"
#include "Console.h"

/* HARD_RESET is a hack to completely nuke the onboard PRAM (or is it SRAM or..) that contains the saved wifi settings */
// #define HARD_RESET

void setup() {
	#ifdef HARD_RESET // force reset code, set to true to nuke eeprom saved wifi info
	Serial.println("Resetting");
	delay(1000);
	WiFi.disconnect();
	ESP.eraseConfig();
	delay(1000);
	*((int *)0) = 0; // boom
	return;
	#endif // end force reset
	Serial.begin(76800); // native esp8266 speed. Also, upload is possible at 6 times this rate, 460800.
	while (!Serial) {};
	pinMode(LED_BUILTIN, OUTPUT);
	lighting_init();
	network_init();
	controls_init();

	/**
	 * Console takes:
	 * led_position: The position of the first led on the rgb string for this console.
	 * led_width: How many leds in the strip are lit when this console is selected.
	 * selector_position: The position in the selector system.
	 * name: The friendly name of the console.
	*/
	Console consoles[2] = {
		Console(1,5,1,"NES"),
		Console(2,5,2,"SNES")
	};
	Serial.println(consoles[0].name.c_str());

	Serial.println("Setup Complete.");
}

void loop() {
	if (flash) {
		flashLed();
	}
	rotaryEncoderTick();
}
