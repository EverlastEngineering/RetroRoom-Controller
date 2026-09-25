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

#include "display.h"

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
	// Boot into the flashing state. `flash` is initialised true in
	// state.cpp and the on-board LED blink is then driven from
	// loop()'s flashLedTick() call. We deliberately don't call
	// flashLed() here -- flashLed() toggles `flash`, so calling it
	// once would DISABLE the boot-time flash. The MANUAL_OE_PIN
	// ring side effect is sacrificed (the operator can hit /flash
	// once to engage it if they want the ring OE PWM).
#ifdef MANUAL_OE_PIN
	analogWrite(MANUAL_OE_PIN, 127);  // ring OE 50% PWM at boot
#endif
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
#if defined(HAS_LCD)
	// 16x2 I2C LCD -- welcome screen + live console name. Must come
	// AFTER consoleDefinitions() so the live display can show the
	// current console from the start.
	display_init();
	// Arm the backlight-off timer now (no selectConsole() call has
	// happened yet, so without this the backlight would stay on
	// forever).
	display_wake();
#endif
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

	// Pump the on-board LED state machine. Owns LED_BUILTIN + ring
	// OE internally; main.cpp never touches the LED pin directly.
	// No-op when `flash` is false; 1 Hz toggle while it's true.
	flashLedTick();

	// Pump the LCD driver -- welcome -> live transition, scrolling,
	// backlight auto-off. No-op when HAS_LCD is undefined.
#if defined(HAS_LCD)
	display_loop();
#endif

	// Liveness heartbeat over Serial. See heartbeatTick() in
	// src/state.cpp -- without this a stuck radio looks identical
	// to a crashed firmware from the host's perspective.
	heartbeatTick();

	// On-board LED is driven exclusively from src/state.cpp:
	// ledOn / ledOff / flashLed toggle the `flash` flag and drive
	// the solid state, flashLedTick (above) does the 1 Hz blink
	// while flashing. The lightCycleTick / lightCycleToggle
	// helpers in src/lighting.{h,cpp} are kept around for the
	// perfboard's WS2812-ring smoke test only.
}
