#include "state.h"
#include "main.h"

bool flash = false;
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