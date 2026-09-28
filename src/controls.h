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

// nextConsoleButton / prevConsoleButton are the hardware counterpart to
// the /next + /prev HTTP endpoints and the "next" / "prev" WebSocket
// commands. Wired via EasyButton (same library as touchSensor) so the
// debounce / interrupt-or-poll path is identical to the existing buttons.
// Pin numbers come from src/configuration.h (NEXT_CONSOLE_PIN, PREV_CONSOLE_PIN);
// see pin-map-chart.md for the authoritative perfboard layout.
extern EasyButton nextConsoleButton;
extern EasyButton prevConsoleButton;

extern void controls_init();
extern void rotaryEncoderTick();
extern int currentConsoleIndex;

void rotarySelectorPressed();
void sequenceElapsed();
void rotarySelectorISR();
void touchSensorISR();
void nextConsolePressed();
void prevConsolePressed();
void nextConsoleISR();
void prevConsoleISR();
// Kept as stubs (no caller in src/main.cpp right now); these will be
// re-wired to drive console advance from the YD USR button when the perfboard
// capacitive-touch input is brought up (see todo/README.md).
void touchDetected();
void touchReleaseDetected();

#endif