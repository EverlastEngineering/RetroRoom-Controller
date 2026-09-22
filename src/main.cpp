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
	// Give the host USB-CDC driver a full second to enumerate and attach
	// to /dev/cu.usbmodem* before we touch USB at all. On the RP2350
	// (Pico 2 W) the CDC-ACM buffer discards writes issued before the
	// host opens the port, so a 1 s pre-begin delay is the cheapest way
	// to make sure every subsequent Serial.println() actually lands on
	// the wire. The delay happens before Serial.begin() because the
	// USB stack init can race with the host-side enumeration.
	delay(1000);

	// 115200 on Pico native USB-CDC is conventional. On the RP2350 (Pico 2 W)
	// the CDC-ACM buffer drops writes issued before the host opens the
	// port, so we wait briefly for a host connection before printing
	// anything. Bounded by 3 s so the firmware still boots unattended.
	Serial.begin(115200);
	const unsigned long waitStart = millis();
	while (!Serial && millis() - waitStart < 3000) {
		delay(10);
	}
	pinMode(LED_BUILTIN, OUTPUT);
#if defined(HAS_LEDS)
	lighting_init();
#endif
#if defined(HAS_WIFI)
	network_init();
#endif
	controls_init();
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

	// Pump the network stack (currently the captive-portal DNS server).
	// No-op when WiFi is not active.
#if defined(HAS_WIFI)
	network_loop();
#endif

	// Heartbeat blink on the on-board LED. LED_BUILTIN resolves to:
	//   - GP25 on Pico / Pico-W (green)
	//   - GP64 on Pico 2 W (green)
	//   - GP25 on YD-RP2040 (blue)
	// 1 Hz toggle (500 ms on / 500 ms off). Provides visual confirmation
	// that setup() completed past pinMode(LED_BUILTIN, OUTPUT) and that
	// loop() is running -- even when there's no host attached to read
	// serial output. Cheap (one digitalWrite + a millis() compare).
	static unsigned long lastBlink = 0;
	static bool ledState = false;
	if (millis() - lastBlink >= 500) {
		lastBlink = millis();
		ledState = !ledState;
		// output millis() to serial so the user can confirm the firmware is running even

		Serial.print("Heartbeat: ");
		Serial.println(millis());
		digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
	}

	// The legacy WS2812 red/green/blue "light cycle" + USR-toggle handler
	// + diagnostic per-second pin prints were removed from loop() on
	// session/merge-pico-json. The lightCycleTick / lightCycleToggle /
	// lightCycleIsEnabled functions in src/lighting.{h,cpp} are kept around
	// in case the perfboard ever wants a background pattern. See TODO.md.
}
