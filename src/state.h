#ifndef RR_STATE_H
#define RR_STATE_H

extern bool flash;
extern int statusLedActive;

void setLed(int state);
void ledOff();
void ledOn();
void flashLed();
// Per-loop pump for the on-board LED blink state-machine. No-op when
// `flash` is false; 1 Hz toggle (500 ms on / 500 ms off) while it's
// true. Called from main.cpp::loop() -- main.cpp never touches the
// LED pin directly, this is the only writer to LED_BUILTIN while
// flashing.
void flashLedTick();

#endif