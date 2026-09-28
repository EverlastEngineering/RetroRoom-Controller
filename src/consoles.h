#ifndef CONSOLES_H
#define CONSOLES_H

#include <Arduino.h>
#include "configuration.h"
#include "Console.h"
#include <vector>

extern std::vector<Console> consoles;
// millis() at the moment we last decided on the current console.
// RAM-only (zeroed on every boot along with currentConsoleIndex).
// Exposed so GET /state.json can surface `selectedAtUptimeMs`.
extern uint32_t currentConsoleSelectedAtMs;
// LCD backlight-off timeout in ms, populated from the top-level
// `lcd.backlightOffAfterMs` field in /consoles.json (default 30000).
// 0 means "never off". Consumed by src/display.cpp. RAM-only --
// reloaded from the config on every boot.
extern uint32_t lcdBacklightOffAfterMs;

void addConsole(const Console& console);
int HowManyConsoles();
const Console& CurrentConsole();

// Loads the embedded console-configuration JSON via the functional core
// (lib/ConsoleConfig). The legacy "consoleDefinitions()" name is kept so
// main.cpp's setup() call site stays the same; the body is now driven by
// the core parser rather than a hand-rolled hardcoded array.
void consoleDefinitions();

// Restore the console that was selected when the device last lost
// power, from the /lastconsole file on LittleFS. Call ONCE from
// setup(), after consoleDefinitions() and selectStack_init(), and
// before anything that reads currentConsoleIndex.
//
// Two things happen here, and they are not the same thing:
//
//  1. The cursor moves. Everything else in the boot sequence reads
//     currentConsoleIndex as it runs -- ledstring_setConsole() lights
//     the strip, the LCD seeds its live lines from CurrentConsole()
//     at the startup -> welcome handover, /state.json reports it -- so
//     the device boots *into* the operator's console rather than
//     being switched to it afterwards.
//
//  2. The latch is stepped to that console. The latch shares a power
//     rail with the controller, so it does not hold position across a
//     power cycle even though the cursor is just a number we
//     remembered. This happens unconditionally, including when the
//     remembered index is the one that was already active: a device
//     that never moved has an arm sitting somewhere unknown, and
//     "the cursor didn't change" says nothing about where it is.
//
// Deliberately not a selectConsole() call. That paints the LCD, which
// would cut the welcome splash short. selectStack() and the cursor
// assignment are the two halves that were actually wanted.
//
// Requires selectStack_init() to have homed the latch first --
// selectStack() pulses a relative number of times from wherever the arm
// currently sits, so it is only meaningful against a known start.
//
// A missing, unreadable or out-of-range stored value is not an error:
// the cursor stays on console 0 and the latch still gets driven. No-op
// when LittleFS isn't mounted.
void restoreLastSelectedConsole();

// Drives the StackSelector + IR blaster for a given console. Free function
// (rather than a Console::selectConsole() method) so the core type stays
// pure and the shell side is in src/consoles.cpp / src/Console.cpp free of
// hardware includes.
void selectConsole(const Console& c);

// Bound to the YD-RP2040 USR button (TOUCH_SENSOR_PIN = GP24). Each press
// advances currentConsoleIndex by one with wrap-around (last -> first),
// then calls selectConsole() on the new index. Logs the index + name +
// selector_position + tvinput hex on every press.
void advanceConsole();

// Wrap-around rewind (first -> last). Counterpart of advanceConsole()
// for the /prev HTTP endpoint and the "prev" WS command. Same shell
// side-effects (LED single-light, StackSelector + IR drive, WS
// broadcast) as advanceConsole(), just stepping -1 instead of +1.
void rewindConsole();

#endif
