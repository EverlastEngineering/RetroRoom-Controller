#include "state.h"
#include "main.h"
#include <FS.h>

bool flash = false;
int statusLedActive = 0x0;

void setLed(int state) {
	digitalWrite(LED_BUILTIN, state);
	digitalWrite(MANUAL_OE_PIN, !state);
	statusLedActive = state;
}

void ledOff() { setLed(0x1); }

void ledOn() { setLed(0x0); }

void flashLed() {
	analogWrite(MANUAL_OE_PIN, 127);
}