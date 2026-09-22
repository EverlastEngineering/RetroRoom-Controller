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
	// Default the on-board LED / ring OE line into the flashing state.
	// flashLed() engages the OE pin PWM; `flash` is initialised true in
	// state.cpp so /state.json reports flash=true from boot and the
	// /flash endpoint toggles off cleanly. The legacy 1 Hz heartbeat
	// blink in loop() is gone -- ledOn / ledOff / flashLed are the
	// canonical LED state drivers.
	flashLed();
#if defined(HAS_LEDS)
	lighting_init();
#endif
#if defined(HAS_WIFI)
	// Scan wifi networks BEFORE the AP comes up. The CYW43 radio
	// can't scan while a client is associated with the SoftAP, so the
	// boot window is the only safe place to populate /scan.json's
	// cache. network_init() below brings up the AP that locks the
	// radio onto a single channel.
	network_scan_cache();
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

	// On-board LED is now driven exclusively by ledOn() / ledOff() /
	// flashLed() -- the HTTP /ledOn / /ledOff / /flash endpoints and any
	// future button-bound handlers mutate it via state.cpp. The legacy
	// 1 Hz heartbeat blink lived here and fought with those commands
	// (every toggle got clobbered by the next loop() iteration); it has
	// been removed. The lightCycleTick / lightCycleToggle helpers in
	// src/lighting.{h,cpp} are kept around for the perfboard's
	// WS2812-ring smoke test only.
}
