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
