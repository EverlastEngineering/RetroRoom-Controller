#ifndef CONTROLS_H
#define CONTROLS_H

/* Rotary Encoder Library */
#include <Arduino.h>
#include <RotaryEncoder.h>
#include "configuration.h"

/* EasyButton Library used for rotary encoder selector and capacitive touch sensor in digital mode */
#include <EasyButton.h>

// touchSensor is the capacitive PROXIMITY pad on TOUCH_SENSOR_PIN, not a
// button. The electrode is the metal rotary knob: approaching it lights the
// ring as an affordance cue, and that is the only effect -- it never
// selects a console. Driven from controls_touchTick() rather than an
// onPressed() callback; see the comment in controls.cpp for why. Pin per
// pin-map-chart.md.
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

extern void controls_init();
extern void rotaryEncoderTick();
// Proximity tick for touchSensor. Reads it and drives the ring's
// proximity hold off the approach and departure edges.
extern void controls_touchTick();
// Called when the ring's fade completes. Snaps the browsed cursor back
// to the selected console.
extern void controls_ringFadedOut();
extern int currentConsoleIndex;

void rotarySelectorPressed();
void sequenceElapsed();
void rotarySelectorISR();
void nextConsolePressed();
void prevConsolePressed();
void nextConsoleISR();
void prevConsoleISR();
// Kept as stubs (no caller anywhere). The capacitive pad is proximity
// only and deliberately does not drive console advance -- that is the
// next/prev buttons' job (GP6/GP7) and the rotary's own click.
void touchDetected();
void touchReleaseDetected();

#endif