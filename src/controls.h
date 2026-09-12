#ifndef CONTROLS_H
#define CONTROLS_H

/* Rotary Encoder Library */
#include <Arduino.h>
#include <RotaryEncoder.h>
#include "configuration.h"

/* EasyButton Library used for rotary encoder selector and capacitive touch sensor in digital mode */
#include <EasyButton.h>

// touchSensor is exposed here (not just in controls.cpp) so the main loop
// can call .read() on it for polling-based debounce. On the YD-RP2040
// dev board TOUCH_SENSOR_PIN maps to GP24 (the USR button); when the
// perfboard lands, the same pin is the capacitive touch input.
extern EasyButton touchSensor;

extern void controls_init();
extern void rotaryEncoderTick();
extern int currentConsoleIndex;

void rotarySelectorPressed();
void sequenceElapsed();
void rotarySelectorISR();
void touchSensorISR();
// Kept as stubs (no caller in src/main.cpp right now); these will be
// re-wired to drive console advance from the YD USR button when the perfboard
// capacitive-touch input is brought up (see TODO.md).
void touchDetected();
void touchReleaseDetected();

#endif