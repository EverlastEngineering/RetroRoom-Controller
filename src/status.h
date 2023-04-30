#ifndef STATUS_H
#define STATUS_H

#include "main.h"

extern bool flash;
extern int statusLedActive;

void setLed(int state);
void ledOff();
void ledOn();
void flashLed();

#endif