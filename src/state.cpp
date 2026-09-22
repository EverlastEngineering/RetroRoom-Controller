#include "state.h"
#include "main.h"

// Defaults: boot into the "flashing" state so the on-board LED / ring OE
// line is engaged at startup. The previous `false` left the LED dark
// until the first /flash toggle; the on-board LED then blinked at the
// heartbeat cadence (1 Hz) controlled from main.cpp::loop(). The
// heartbeat is gone now -- flashLed() is the canonical "flash on" path,
// so the flag should be true from the start.
bool flash = true;
int statusLedActive = 0x0;

// setLed() drives the on-board LED plus, if MANUAL_OE_PIN is defined by the
// active board configuration, an optional external output-enable line.
// On boards without MANUAL_OE_PIN (e.g. Raspberry Pi Pico until a perfboard
// revision defines the pin) the external drive is a no-op.
void setLed(int state) {
	digitalWrite(LED_BUILTIN, state);
#ifdef MANUAL_OE_PIN
	digitalWrite(MANUAL_OE_PIN, !state);
#endif
	statusLedActive = state;
}

void ledOff() { setLed(0x1); }

void ledOn() { setLed(0x0); }

void flashLed() {
#ifdef MANUAL_OE_PIN
	analogWrite(MANUAL_OE_PIN, 127);
#endif
}