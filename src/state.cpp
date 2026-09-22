#include "state.h"
#include "main.h"

// Defaults: boot into the "flashing" state so the on-board LED is
// blinking at boot. The previous `false` left the LED dark until the
// first /flash toggle; the on-board LED then blinked at the heartbeat
// cadence (1 Hz) controlled from main.cpp::loop(). The heartbeat
// chunk is gone -- flashLedTick() (called from loop()) is the new
// canonical blink driver, gated on `flash`. So the flag is true at
// boot and the LED starts blinking immediately.
bool flash = true;
int statusLedActive = 0x0;

// setLed() drives the on-board LED plus, if MANUAL_OE_PIN is defined by the
// active board configuration, an optional external output-enable line.
// On boards without MANUAL_OE_PIN (e.g. Raspberry Pi Pico until a perfboard
// revision defines the pin) the external drive is a no-op.
//
// The Pico 2 W on-board LED (LED_BUILTIN = GP64) is wired active-high:
// driving HIGH lights it, driving LOW turns it off. Same polarity on
// the Pico / Pico-W (GP25) and YD-RP2040 (GP25). The function names
// ledOn() / ledOff() are the user-facing API -- they map to HIGH / LOW
// here so the labels match what the operator sees.
void setLed(int state) {
	digitalWrite(LED_BUILTIN, state);
#ifdef MANUAL_OE_PIN
	digitalWrite(MANUAL_OE_PIN, !state);
#endif
	statusLedActive = state;
}

void ledOff() {
	// Force the flash driver off so an immediately-prior /flash
	// doesn't keep blinking the LED after the operator asked for
	// "off". flashLed() toggles the `flash` flag; mirror the same
	// shape here so the three top-level callers (ledOn/ledOff/
	// flashLed) all agree on what `flash` means.
	flash = false;
	// Active-high on-board LED: LOW = dark.
	setLed(0x0);
}

void ledOn() {
	flash = false;
	// Active-high on-board LED: HIGH = lit.
	setLed(0x1);
}

void flashLed() {
	// Toggle the flash state. flashLedTick() (this TU) reads
	// `flash` and toggles LED_BUILTIN at 1 Hz while it's true.
	flash = !flash;
#ifdef MANUAL_OE_PIN
	// Ring OE: 50% PWM when flashing, solid off otherwise. The
	// on-board LED blink is owned by flashLedTick() below; this
	// function doesn't touch LED_BUILTIN.
	analogWrite(MANUAL_OE_PIN, flash ? 127 : 0);
#endif
}

void flashLedTick() {
	// On-board LED blink driver. Two cadences picked automatically
	// by network state:
	//   fast flash (500 ms on / 500 ms off): "needs wifi config" --
	//     SoftAP / captive-portal mode or no wifi configured. The
	//     operator can tell at a glance that the device hasn't
	//     joined a network yet.
	//   slow blink (250 ms on / 2750 ms off, ~3 s period): "online
	//     and happy" -- STA mode with an IP, server running, waiting
	//     for UI commands. Replaces the legacy 1 Hz heartbeat from
	//     main.cpp::loop(); slow + asymmetric makes the heartbeat
	//     visually distinct from the "needs config" flash while
	//     still being a clear "alive" signal.
	// The /ledOn / /ledOff / /flash handlers set `flash = false`
	// when the operator takes manual control; this driver then
	// idles and the LED follows whatever setLed() last wrote.
	static unsigned long lastBlink = 0;
	static bool ledState = false;
	if (!flash) {
		lastBlink = 0;
		ledState = false;
		return;
	}
	const unsigned long halfPeriodMs =
		network_inStaMode() ? 2750UL : 500UL;
	if (millis() - lastBlink < halfPeriodMs) {
		return;
	}
	lastBlink = millis();
	ledState = !ledState;
	digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
}

// Liveness heartbeat. Once every kHeartbeatIntervalMs we print a
// single line so a host-side observer (a monitor session, a wrapper
// script, or a future operator UI) can tell at a glance that the
// firmware main loop is still running. This matters specifically
// when WiFi is wedged: without the heartbeat, a stuck CYW43 looks
// identical to a crashed firmware from the host's perspective, and
// the only way to disambiguate is a power-cycle. With the heartbeat
// we can say "the firmware is alive but the radio is dead -- power
// cycle" vs "the firmware is dead -- flash + retry" without
// unplugging anything.
//
// The line starts with `Heartbeat:` (stable prefix) so the wrapper
// (agent-script/pio-upload-monitor.sh) can strip it from build logs
// by default while --keep-heartbeats surfaces it for triage. The
// payload is intentionally the same shape as GET /state.json's
// fields so a passive observer can read the same mental model from
// either source.
void heartbeatTick() {
#if defined(HAS_WIFI)
	constexpr unsigned long kHeartbeatIntervalMs = 2000UL;
	static unsigned long lastBeat = 0;
	const unsigned long now = millis();
	if (now - lastBeat < kHeartbeatIntervalMs) {
		return;
	}
	lastBeat = now;
	// `network_inStaMode()` is the most reliable "is the CYW43
	// happy?" signal we have -- false means SoftAP / captive-
	// portal OR no wifi yet. Surface it as `mode=sta|ap` so a
	// postmortem monitor log can show the operator exactly which
	// state the device ended up in.
	Serial.print("Heartbeat: uptime=");
	Serial.print((unsigned long)now / 1000UL);
	Serial.print("s selected=");
	Serial.print((unsigned long)currentConsoleSelectedAtMs / 1000UL);
	Serial.print("s idx=");
	Serial.print(currentConsoleIndex);
	Serial.print(" total=");
	Serial.print(HowManyConsoles());
	Serial.print(" mode=");
	Serial.print(network_inStaMode() ? "sta" : "ap");
	Serial.println();
#endif // HAS_WIFI
}