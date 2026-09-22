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
	// ~1 Hz blink (500 ms on / 500 ms off). Same cadence as the old
	// heartbeat in main.cpp::loop(), which was the only thing that
	// ever actually drove LED_BUILTIN while flashing. Cheap: one
	// millis() compare and one digitalWrite per tick.
	static unsigned long lastBlink = 0;
	static bool ledState = false;
	if (!flash) {
		// Idle. Reset the phase so re-enabling starts cleanly from
		// the "on" half of the cycle (matches what flashLed() sets
		// when it engages).
		lastBlink = 0;
		ledState = false;
		return;
	}
	if (millis() - lastBlink < 500) {
		return;
	}
	lastBlink = millis();
	ledState = !ledState;
	digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
}