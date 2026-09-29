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

// Rotary selector (the push-button built into the rotary encoder).
// Exposed so the main loop can call .read() on it; the ISR only sets
// the matching IRQ flag, see hasRotarySelectorInterruptFired below.
extern EasyButton rotarySelector;

// Defer flags for the interrupt-driven buttons, following the same
// hasTouchInterruptFired pattern: the ISR sets the flag, the actual
// EasyButton::read() (which may invoke the _pressed_callback
// synchronously) is drained from loop() in main.cpp. Running the
// callback chain from the ISR starves the CYW43 WiFi driver for the
// duration of selectConsole() -- that surfaced as a 1-2 s stall on
// every rotary click.
extern volatile bool hasRotarySelectorInterruptFired;
extern volatile bool hasNextConsoleInterruptFired;
extern volatile bool hasPrevConsoleInterruptFired;
// Touch sensor: the touchSensor.read() used to be polled every tick,
// but that ran the _pressed_callback synchronously from a poll and
// carried the same callback-in-non-loop context hazard as the others
// when the perfboard capacitive input is wired. Now flag-deferred.
extern volatile bool hasTouchInterruptFired;

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