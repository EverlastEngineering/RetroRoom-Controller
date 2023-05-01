#ifndef RR_STATUS_H
#define RR_STATUS_H

extern bool flash;
extern int statusLedActive;

void setLed(int state);
void ledOff();
void ledOn();
void flashLed();

#endif