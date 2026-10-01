#ifndef CONSOLES_H
#define CONSOLES_H

#include <Arduino.h>

#include "consoleconfig_store.h"  // SaveResult
#include "configuration.h"
#include "Console.h"
#include <vector>

extern std::vector<Console> consoles;
// The physical extent of each shelf, from the config's optional
// `shelves` block, keyed by the `shelf` value its consoles carry.
// Empty when the config declares none, which is normal -- the LED
// animations then derive each shelf's extent from the consoles on it.
//
// A shelf is a piece of furniture, so its extent is not derivable from
// the console list: the string runs past the last console on a shelf,
// and that bare space at either end is what the commit animation
// expands into.
extern std::vector<Shelf> shelfBounds;
// How the LED string looks and feels, from the config's optional `led`
// block. Filled with the defaults before the config is even read, so it
// is never in a half-parsed state, and the defaults live in exactly one
// place -- lib/ConsoleConfig -- rather than beside the code that uses
// them.
//
// src/consoles.cpp, controls.cpp, lighting.cpp and the LED-string shell
// all read this rather than a #define each. That is the point: one
// number, one home.
extern retroroom_core::LedFeel ledFeel;
// The *selected* console. Written only by a commit: the rotary click
// (which moves it to browsedConsoleIndex first), selectConsole(),
// advanceConsole(), rewindConsole() and the post-boot restore. Every
// downstream consumer -- ledstring_setConsole, display_show_console,
// GET /state.json, the console: WS broadcast, the LittleFS save -- reads
// this, and so all of them report the selection rather than whatever
// the operator is currently browsing toward.
extern int currentConsoleIndex;
// The console the operator is browsing to with the rotary, distinct from
// currentConsoleIndex until they commit. Reverts to currentConsoleIndex
// when the ring's idle timeout expires, so an abandoned spin does not
// leave the cursor stranded away from the live console.
extern int browsedConsoleIndex;
// millis() at the moment we last decided on the current console.
// RAM-only (zeroed on every boot along with currentConsoleIndex).
// Exposed so GET /state.json can surface `selectedAtUptimeMs`.
extern uint32_t currentConsoleSelectedAtMs;
// LCD backlight-off timeout in ms, populated from the top-level
// `lcd.backlightOffAfterMs` field in /consoles.json (default 30000).
// 0 means "never off". Consumed by src/display.cpp. RAM-only --
// reloaded from the config on every boot.
extern uint32_t lcdBacklightOffAfterMs;
// Whether a failed WiFi join should put "No WIFI, Look" / "For
// RetroRoom AP" on the LCD for a couple of seconds before the SoftAP
// comes up. Populated from `network.showWIFIConnectionFailureMessage`
// in /consoles.json (default true), readable and writable through
// applyConfigValue() like any other setting, and consulted by
// src/network.cpp. RAM-only -- reloaded on every boot.
extern bool showWifiConnectionFailureMessage;
// Whether the radio is left off entirely, from `network.disable` in
// /consoles.json (default false). Read by src/network.cpp before it
// touches the radio, and by src/state.cpp so the on-board LED and the
// heartbeat report "off" rather than a network problem the operator
// asked for.
//
// Boot-time only: the radio is consulted once, before anything is
// brought up, so a change takes effect on the next restart -- which
// the menu prompt says, because configFieldNeedsReboot() declares it.
// RAM-only -- reloaded on every boot.
extern bool networkDisabled;

// The brightness the LEDs are actually using, right now: the runtime
// override if there is one, otherwise the config's
// `led.brightnessPct`.
//
// This is the only function src/lighting.cpp and src/ledstring.cpp
// should ask about brightness. They used to read `ledFeel.brightnessPct`
// directly, which is fine until something needs to change the level
// without changing the config -- at which point every writer of pixels
// is a second place to remember, and the strip and the ring disagree
// the moment one of them is missed.

// -1 means "no override": follow the config. 0..100 is a temporary
// level that is not saved, not in the menu, and gone on restart.
// That is the whole point: a "turn the lights off for the evening"
// must not become a config change nobody asked for, and must not come
// back by itself after a power cut.
//
// Clamped to 0..100 rather than refused, so a caller that computes a
// level cannot put the LEDs somewhere the config could not have asked
// for.
extern int lightBrightnessPct();
extern void setLightBrightnessOverride(int pct);
// The raw override, for reporting. -1 when following the config.
extern int lightBrightnessOverridePct();

void addConsole(const Console& console);
int HowManyConsoles();
const Console& CurrentConsole();
const Console& BrowsedConsole();

// Loads the embedded console-configuration JSON via the functional core
// (lib/ConsoleConfig). The legacy "consoleDefinitions()" name is kept so
// main.cpp's setup() call site stays the same; the body is now driven by
// the core parser rather than a hand-rolled hardcoded array.
// The operator's config menu, from the `menu` array in /consoles.json.
// Empty when the config declares none, which is a menu with only "Go
// Back" in it rather than a missing menu -- see
// todo/open/2026-09-30_menu-lock-down-mode.md.
const retroroom_core::Menu CabinetMenu();

// True once consoleDefinitions() has loaded a config that came off
// flash, and it parsed. False means the cabinet is running the
// built-in PROGMEM default: nobody has set this cabinet up yet.
//
// Read by main.cpp to decide whether to put the "not set up yet" pages
// on the LCD at boot, and by the serial and HTTP status paths to say
// the same thing in words.
//
// Deliberately *not* auto-cleared by anything, and deliberately never
// satisfied by writing the default out: the absence of
// /consoles.json *is* the flag. Auto-saving it would destroy the only
// way to tell "nobody has configured this" from "configured to the
// factory settings", and would make the notice fire once and then
// never again -- including after a firmware reflash, which leaves
// LittleFS intact. The way to make this go away is to put a real
// config on the cabinet, which is the thing we want done anyway.
bool consoleConfigIsUploaded();

// ---- applying and saving settings at runtime -----------------------------
//
// Apply changes what the cabinet is doing; save changes what is on
// flash. They are separate operations because the menu and the API are
// both clients of them and neither should have its own idea of what
// "save" means -- and because the whole point of the split is that an
// operator can try a setting, live, and walk away from it.

// Apply one value by path -- "led.detentsPerStep",
// "lcd.backlightOffAfterMs". Takes effect immediately and is recorded
// as pending. Returns false for a path that is not a known setting.
//
// NOT persisted. Walking away from an unsaved change loses it on the
// next power cycle, which is the point: trying a setting must not be
// able to break the cabinet.
bool applyConfigValue(const char* path, int value);

// The live value of a path, or `fallback` if the path is unknown.
int configValueOf(const char* path, int fallback);

// What the menu's prompt needs to know, both derived from one pending
// set so the two can never disagree.
bool configHasUnsavedChanges();
// True when a *pending* change is one that only takes effect after a
// restart -- today exactly led.totalLeds, which is bound into
// FastLED.addLeds() at init.
bool configNeedsReboot();

// The pending set itself, for the API and for the "Saving..." report.
// configPendingPath() gives the bare key; the block is implied by
// whatever the field table says.
int configPendingCount();
const char* configPendingPath(int i);
int configPendingValue(int i);

// Write the pending changes to /consoles.json, rotating the backup
// slots on the way exactly as an upload does. A save from the menu can
// write a bad document just as an upload can, and the backups exist for
// precisely that write.
//
// The empty case is a success, not a failure: there is nothing to do,
// and a caller asking to save nothing should not be told it failed.
retroroom_store::SaveResult configSave();

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

// Pump the debounced LittleFS save of the last-selected console. Call
// from main.cpp::loop(). Each commit through selectConsole() requests
// a save; the actual write fires only after kSaveQuietMs of silence so
// back-to-back rotary clicks coalesce into one LittleFS write rather
// than paying a write+sync per click.
void consoles_loop();

#endif
