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
// Exposed so the main loop can poll .read() on it. It is deliberately
// NOT interrupt-driven: see the comment in controls.cpp::controls_init()
// for the edge loss that cost. A callback fired from this read() runs in
// loop() context, which is also why the flag pattern below exists for the
// other buttons.
extern EasyButton rotarySelector;

// Defer flags for the interrupt-driven buttons, following the same
// hasTouchInterruptFired pattern: the ISR sets the flag, the actual
// EasyButton::read() (which may invoke the _pressed_callback
// synchronously) is drained from loop() in main.cpp. Running the
// callback chain from the ISR starves the CYW43 WiFi driver for the
// duration of selectConsole() -- that surfaced as a 1-2 s stall on
// every rotary click.
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
// End the browse: drop the accumulated detents, re-anchor the blob on
// the selected console, and return the LED string to its resting paint.
//
// Called from selectConsole() in src/consoles.cpp, which is the single
// commit point for every selection path (rotary click, NEXT/PREV
// buttons, /next, /prev, WebSocket). Doing it there rather than at each
// caller means a browse can never be left half-finished -- a leftover
// detent count would make the operator's next turn start a step in the
// wrong place.
extern void controls_browseReset();
extern int currentConsoleIndex;

void rotarySelectorPressed();
// A double-click on the knob, from EasyButton's onSequence(2, ...).
// Toggles night mode. Declared here because controls_init() registers
// the callback by name.
void doubleClicked();
void sequenceElapsed();
// The config menu, opened by a long press on the rotary. It takes the
// knob from the browse while open: these are how a turn and a click get
// handed over, and controls_menuLoop() paints it and runs its clock.
extern void controls_menuDetent(int direction);
extern void controls_menuClick();
extern void controls_menuLoop();
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