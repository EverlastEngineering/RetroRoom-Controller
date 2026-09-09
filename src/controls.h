#ifndef CONTROLS_H
#define CONTROLS_H

/* Rotary Encoder Library */
#include <Arduino.h>
#include <RotaryEncoder.h>
#include "configuration.h"

/* EasyButton Libary used for rotary encoder selector and capacitive touch sensor in digital mode */
#include <EasyButton.h>

// touchSensor is exposed here (not just in controls.cpp) so the main loop
// can call .update() on it for polling-based debounce. On the YD-RP2040
// dev board TOUCH_SENSOR_PIN maps to GP24 (the USR button), so this also
// drives the smoke-test toggle for the WS2812 red/green/blue cycle.
extern EasyButton touchSensor;

extern void controls_init();
extern void rotaryEncoderTick();
extern int currentConsoleIndex;

void rotarySelectorPressed();
void sequenceElapsed();
void rotarySelectorISR();
void touchSensorISR();
void touchDetected();
void touchReleaseDetected();

#endif