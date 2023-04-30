#include "status.h"

bool flash = false;
int statusLedActive = 0x0;

void setLed(int state)
{
	digitalWrite(LED_BUILTIN, state);
	statusLedActive = state;
}

void ledOff()
{
	setLed(0x1);
}

void ledOn()
{
	setLed(0x0);
}

void flashLed()
{
	ledOn();
	delay(20);
	ledOff();
	delay(50);
}