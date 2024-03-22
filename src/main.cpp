/**
 * RetroRoom for ESP8266
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 */



/* Our own headers */
#include "main.h"



void consoleDefinitions() {
	/**
	 * Console takes:
	 * name: The friendly name of the console.
	 * enum of the Inputs on the television
	 * selector_position: The position in the StackSelector system.
	 * led_position: The position of the first led on the rgb string for this console.
	 * led_width: How many leds in the strip are lit when this console is selected.
	 */

	addConsole(Console(NES, Composite,		1, 5, 1));
	addConsole(Console(N64, SVideo, 		2, 15, 5));
	addConsole(Console(Gamecube, SVideo,	3, 15, 5));
	addConsole(Console(Wii, Component,		4, 15, 5));
	addConsole(Console(SNES, SCART,			5, 10, 5));

	Serial.println(HowManyConsoles());
}

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
	ir_control_init();
	consoleDefinitions();
	selectStack_init();
	Serial.println("Setup Complete.");
}

void loop() {
	if (flash) {
		flashLed();
	}
	rotaryEncoderTick();
}
