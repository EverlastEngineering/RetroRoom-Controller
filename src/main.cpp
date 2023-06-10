/**
 * RetroRoom for ESP8266
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 */

// define the input # on the televsion
#define COMPOSITE 3
#define COMPONENT 4
#define SVIDEO 2

/** Consoles */
#define NES "Nintendo Entertainment System"
#define SNES "Super Nintendo"
#define Gamecube "Nintendo Gamecube"
#define N64 "Nintendo 64"
#define Wii "Nintendo Wii"

/** Inputs */
#define Composite "Composite"
#define SVideo "SVideo"
#define Component "Component"
#define SCART "SCART"

/* Our own headers */
#include "main.h"
#include "controls.h"
#include "state.h"
#include "network.h"
#include "lighting.h"
#include "Console.h"
#include "ircontrol.h"


#include <map>
#include <string>
using namespace std;

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

	/**
	 * Console takes:
	 * led_position: The position of the first led on the rgb string for this console.
	 * led_width: How many leds in the strip are lit when this console is selected.
	 * selector_position: The position in the StackSelector system.
	 * enum of the Inputs on the television
	 * name: The friendly name of the console.
	*/
	
	Console consoles[2] = {
		Console(1,5,1,Composite,NES),
		Console(2,5,2,SCART,SNES)
	};
	// Serial.println(consoles[0].led_position);

	

	Serial.println("Setup Complete.");
}

void loop() {
	if (flash) {
		flashLed();
	}
	rotaryEncoderTick();
}
