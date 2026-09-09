/**
 * RetroRoom for ESP8266
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 */



/* Our own headers */
#include "main.h"

// consoleDefinitions() is defined in src/consoles.cpp and reads the embedded
// JSON via the functional core (lib/ConsoleConfig). The old hand-rolled
// addConsole(...) sequence that used to live here is gone.

// HARD_RESET is a hack to completely nuke the onboard PRAM (or is it SRAM or..) that contains the saved wifi settings
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
	pinMode(MANUAL_OE_PIN, OUTPUT);
	analogWriteFreq(40000);
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
