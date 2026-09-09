/**
 * RetroRoom firmware main loop. Originally written for ESP8266; the
 * Raspberry Pi Pico port (see session/pico-migration) gates any
 * ESP-only code on HAS_WIFI so the same init sequence runs on
 * pico_base (no wireless, no web UI) without dragging in WiFiManager
 * / AsyncWebServer / etc.
 *
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 */

#include "main.h"

// consoleDefinitions() is defined in src/consoles.cpp and reads the embedded
// JSON via the functional core (lib/ConsoleConfig). The old hand-rolled
// addConsole(...) sequence that used to live here is gone.

// HARD_RESET is a hack to completely nuke the onboard PRAM (or is it SRAM or..) that contains the saved wifi settings
// #define HARD_RESET
#if defined(HARD_RESET) && defined(ESP8266)
#warning HARD_RESET defined: will erase ESP WiFi config and reboot on next setup()
#endif

void setup() {
	#if defined(HARD_RESET) && defined(ESP8266)
	Serial.println("Resetting");
	delay(1000);
	WiFi.disconnect();
	ESP.eraseConfig();
	delay(1000);
	*((int *)0) = 0; // boom (ESP-only)
	return;
	#endif

	// 115200 on Pico native USB-CDC is conventional. ESP8266 still gets the
	// legacy 76800 baud (the comment in the old line about "native esp8266
	// speed" was misleading -- 76800 is not actually native; left as-is so the
	// ESP build is unchanged for now).
#if defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
	Serial.begin(115200);
#else
	Serial.begin(76800);
#endif

	// The classic Arduino `while (!Serial) {};` pattern is for boards whose
	// native USB CDC requires the host to open the port before printing works
	// (Leonardo-style). On ESP it was always a no-op. On Pico native USB-CDC
	// it can hang on some hosts. Drop it -- Serial.print() before any
	// host-side read just goes into the USB buffer and is read on next open.
	pinMode(LED_BUILTIN, OUTPUT);
#ifdef MANUAL_OE_PIN
	// MANUAL_OE_PIN is currently commented out in src/configuration.h for the
	// ESP build too, so this guard exists to keep setup() referencing an
	// optional pin only when the build defines it.
	pinMode(MANUAL_OE_PIN, OUTPUT);
	analogWriteFreq(40000);
#endif
#if defined(HAS_LEDS)
	lighting_init();
#endif
#if defined(HAS_WIFI)
	network_init();
#endif
	controls_init();
	// ir_control_init() is deferred until the IRremote v4.x swap lands
	// (z3t0/IRremote@^4.7.1 in lib_deps; the existing ESP-only IRremoteESP8266
	// call has been moved into ircontrol.cpp gated on ESP8266+IR).
#if defined(ESP8266)
	ir_control_init();
#endif
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
