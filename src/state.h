#ifndef RR_STATE_H
#define RR_STATE_H

extern bool flash;
extern int statusLedActive;

void setLed(int state);
void ledOff();
void ledOn();
void flashLed();

#endif