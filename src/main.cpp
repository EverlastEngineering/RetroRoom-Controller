/**
 * RetroRoom firmware main loop. Targets the Raspberry Pi Pico (RP2040)
 * via Earle Philhower's arduino-pico core. Originally written for
 * ESP8266; ESP support was dropped on session/merge-pico-json (see
 * todo/ and LOG.md).
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
#if defined(HAS_LCD)
	// 16x2 I2C LCD -- brings up the hardware and paints the startup
	// "RetroRoom" / "Loading" screen. Deliberately FIRST, before the
	// network bring-up below: that path blocks for seconds, and this
	// is the only thing telling the operator the cabinet is alive
	// while it happens.
	//
	// The startup -> welcome -> live progression is owned by
	// display_loop() in loop(). Nothing further is needed here, and
	// calling display_init() again would just repeat ~1 s of I2C
	// bring-up and blank the panel.
	display_init();
#endif
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
	// Home the console latch. selectStack_init() also issues the
	// initial selectStack() pass, which establishes the known starting
	// position that restoreLastSelectedConsole() below steps relative
	// to -- and the latch shares a power rail with the controller, so
	// it does not hold its position across a power cycle. The two must
	// run in this order.
	selectStack_init();
	// Restore the console that was selected before the last power loss.
	// Must run between consoleDefinitions() -- which populates the list
	// the stored index refers to -- and ledstring_setConsole() below,
	// which lights the strip for currentConsoleIndex. Everything after
	// it (the strip, the LCD's live-line seed, /state.json) then reads
	// the restored index, so the device simply boots into the operator's
	// console rather than being switched to it afterwards.
	restoreLastSelectedConsole();
	// Paint the initial console's window on the second strip now
	// that consoleDefinitions() has populated src/consoles.cpp::consoles.
	// ledstring_init() powers the strip up dark; ledstring_setConsole(0)
	// lights the [ledPosition, ledPosition+ledWidth) range of the first
	// console in the list so the operator sees the feature live before
	// they've turned the dial. Both calls no-op when HAS_LEDS is
	// undefined (boards without the second strip wired).
#if defined(HAS_LEDS)
	ledstring_init();
	ledstring_setConsole(currentConsoleIndex);
#endif
#if defined(HAS_LCD)
	// Arm the backlight-off timer now (no selectConsole() call has
	// happened yet, so without this the backlight would stay on
	// forever). The LCD's phase progression is NOT touched here --
	// loop()'s display_loop() picks up the startup phase on its first
	// tick and takes it from there.
	display_wake();
#endif
	Serial.println("Setup Complete.");
	Serial.flush();
}

void loop() {
	// Track the rotary encoder for console switching.
	rotaryEncoderTick();

	// Pump the deferred-button ISRs. The actual EasyButton::read()
	// (which may invoke the _pressed_callback synchronously) is
	// called here in loop() context, NOT in the ISR, so the
	// callback chain -- which drives the latch, the LCD, the IR
	// blaster, and FastLED -- never runs with interrupts blocked.
	// Doing it in the ISR starved the CYW43 WiFi driver and surfaced
	// as a 1-2 s stall on every rotary click.
	if (hasRotarySelectorInterruptFired) {
		hasRotarySelectorInterruptFired = false;
		rotarySelector.read();
	}
	if (hasNextConsoleInterruptFired) {
		hasNextConsoleInterruptFired = false;
		nextConsoleButton.read();
	}
	if (hasPrevConsoleInterruptFired) {
		hasPrevConsoleInterruptFired = false;
		prevConsoleButton.read();
	}
	// The capacitive pad is a proximity sensor, not a button, so it has
	// no callback to defer -- controls_touchTick() reads it and drives
	// the ring light off the state edges itself.
	controls_touchTick();

	// Pump the network stack (currently the captive-portal DNS server).
	// No-op when WiFi is not active.
#if defined(HAS_WIFI)
	network_loop();
#endif

	// Pump the on-board LED state machine. Owns LED_BUILTIN + ring
	// OE internally; main.cpp never touches the LED pin directly.
	// No-op when `flash` is false; 1 Hz toggle while it's true.
	flashLedTick();

	// Finish any ring fade that a rotary turn or the proximity pad
	// started, and decide when one should start. The tick that completes
	// a fade is the cue to snap the browsed cursor back to the selected
	// console, so an abandoned spin does not leave it stranded.
	// No-op when HAS_LEDS is undefined.
#if defined(HAS_LEDS)
	if (lighting_loop()) {
		controls_ringFadedOut();
	}
	// Advance whatever the GP21 string is animating -- the preview pulse
	// while a browse is snapped on a console, the twinkle-then-settle
	// after a commit. Returns immediately when the strip is resting, so
	// this costs one comparison in the common case.
	ledstring_loop();
#endif

	// Pump the LCD driver -- welcome -> live transition, scrolling,
	// backlight auto-off. No-op when HAS_LCD is undefined.
#if defined(HAS_LCD)
	display_loop();
#endif

	// Liveness heartbeat over Serial. See heartbeatTick() in
	// src/state.cpp -- without this a stuck radio looks identical
	// to a crashed firmware from the host's perspective.
	heartbeatTick();

	// Pump the debounced LittleFS save of the last-selected console.
	// See consoles_loop() in src/consoles.cpp -- selectConsole()
	// arms a deadline; this loop body is what actually fires the
	// save after the quiet window. Keeps the LittleFS write+sync off
	// the rotary click path (which used to make the dial feel
	// unresponsive on Pico's on-flash FS).
	consoles_loop();

	// On-board LED is driven exclusively from src/state.cpp:
	// ledOn / ledOff / flashLed toggle the `flash` flag and drive
	// the solid state, flashLedTick (above) does the 1 Hz blink
	// while flashing. The lightCycleTick / lightCycleToggle
	// helpers in src/lighting.{h,cpp} are kept around for the
	// perfboard's WS2812-ring smoke test only.
}
