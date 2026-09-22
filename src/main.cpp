/**
 * RetroRoom firmware main loop. Targets the Raspberry Pi Pico (RP2040)
 * via Earle Philhower's arduino-pico core. Originally written for
 * ESP8266; ESP support was dropped on session/merge-pico-json (see
 * TODO.md and LOG.md).
 *
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 */

#include "main.h"

// consoleDefinitions() is defined in src/consoles.cpp and reads the embedded
// JSON via the functional core (lib/ConsoleConfig). On boot it prints the
// number of consoles loaded so the user can confirm the JSON parser
// succeeded without having to look at the WS or run any test.

void setup() {
	// 115200 on Pico native USB-CDC is conventional. On the RP2350 (Pico 2 W)
	// the CDC-ACM buffer drops writes issued before the host opens the
	// port, so we wait briefly for a host connection before printing
	// anything. Bounded by 3 s so the firmware still boots unattended.
	Serial.begin(115200);
	const unsigned long waitStart = millis();
	while (!Serial && millis() - waitStart < 3000) {
		delay(10);
	}
	// The classic Arduino `while (!Serial) {};` pattern can hang on the Pico's
	// native USB-CDC on some hosts. Drop it -- Serial.print() before any
	// host-side read just goes into the USB buffer and is read on next open.
	pinMode(LED_BUILTIN, OUTPUT);
	// MANUAL_OE_PIN is intentionally undefined (see src/configuration.h);
	// when the perfboard lands, the pinMode/analogWriteFreq lines go back.
#if defined(HAS_LEDS)
	lighting_init();
#endif
#if defined(HAS_WIFI)
	network_init();
#endif
	controls_init();
	// Initialize the IR sender early so selectConsole() can blast codes
	// during consoleDefinitions()'s initial setStack + post-load apply
	// cycle. HAS_IR is set in build_flags for all Pico envs; when HAS_IR
	// is undefined this is a no-op (see src/ircontrol.cpp).
#if defined(HAS_IR)
	ir_control_init();
#endif
	consoleDefinitions();
	selectStack_init();
	Serial.println("Setup Complete.");
	Serial.flush();
}

void loop() {
	// Track the rotary encoder for console switching.
	rotaryEncoderTick();

	// Service the touch sensor (YD-RP2040 USR button on GP24 is mapped to
	// TOUCH_SENSOR_PIN). EasyButton in POLL mode requires .read() (not
	// .update()) to fire onPressed / wasReleased callbacks. The handler
	// registered in controls_init() -- see TODO.md.
	touchSensor.read();

	// The legacy WS2812 red/green/blue "light cycle" + USR-toggle handler
	// + diagnostic per-second pin prints + heartbeat blink have all been
	// removed from loop() on session/merge-pico-json. The lightCycleTick /
	// lightCycleToggle / lightCycleIsEnabled functions in src/lighting.{h,cpp}
	// are kept around in case the perfboard ever wants a background pattern.
	// See TODO.md.
}
