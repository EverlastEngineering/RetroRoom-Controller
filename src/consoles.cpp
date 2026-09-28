#include "consoles.h"

#include <ConsoleConfig.h>

#include <string>

#include "lighting.h"
#include "ledstring.h"
#include "consoleconfig_store.h"
#include "display.h"
#if defined(HAS_IR)
#include "ircontrol.h"
#endif
#if defined(HAS_WIFI)
#include "network.h"
#endif
#include "stackselector.h"

// Embedded console-configuration JSON. Source of truth: lib/ConsoleConfig/.
// The shell stores the same string in PROGMEM so it lands in flash and the
// core's parser is fed directly from RAM. (The legacy approach was a
// hand-rolled addConsole(...) sequence in main.cpp; this file replaces it.)
//
// At boot, consoleDefinitions() prefers /consoles.json from LittleFS
// (the slot maintained by the POST /consoles.json handler in
// src/network.cpp). This PROGMEM literal is the fallback used when:
//   - the FS isn't mounted (board with no filesystem partition,
//     e.g. [env:pico_base]),
//   - /consoles.json is missing (factory-fresh device),
//   - or /consoles.json fails to parse (corrupt or partial write).
// Keeping the default here means the device never wedges at boot just
// because no one has POSTed a config yet -- it comes up with the
// 3-console example config and the operator can build from there.
static const char CONFIG_JSON[] PROGMEM = R"({
    "irCodes": {
        "SVideo": "0x030",
        "Front": "0x830",
        "Video": "0x430",
        "YUV":   "0xE30"
    },
    "consoleNames": {
        "NES":  "Nintendo Entertainment System",
        "SNES": "Super Nintendo Entertainment System",
        "GEN":  "Sega Genesis"
    },
    "consoles": [
        {"id": "NES",  "tvInput": "Video", "selectorPosition": 1, "ledPosition": 1,  "ledWidth": 1},
        {"id": "SNES", "tvInput": "YUV",   "selectorPosition": 2, "ledPosition": 7,  "ledWidth": 5},
        {"id": "GEN",  "tvInput": "YUV",   "selectorPosition": 3, "ledPosition": 13, "ledWidth": 5}
    ]
})";

int currentConsoleIndex = 0;
// millis() at the moment we last decided on the current console
// (i.e. immediately after wraparoundNext() resolves in advanceConsole()
// / rewindConsole()). Used by GET /state.json to surface
// `selectedAtUptimeMs` so the e2e harness can verify the device
// actually moved (delta changes) without trusting response codes.
// RAM-only; resets to 0 on every boot (RP2350 .bss is zeroed by crt0
// on every boot, warm or cold -- same lifetime as currentConsoleIndex).
uint32_t currentConsoleSelectedAtMs = 0;
uint32_t lcdBacklightOffAfterMs = 30000;  // default; overwritten by consoleDefinitions()
std::vector<Console> consoles;

void addConsole(const Console& console) {
	consoles.push_back(console);
}

int HowManyConsoles() {
	return static_cast<int>(consoles.size());
}

const Console& CurrentConsole() {
	return consoles[currentConsoleIndex];
}

void restoreLastSelectedConsole() {
	// The console list has to be populated before this is worth
	// calling: the stored value is an index into it.
	int stored = 0;
	if (retroroom_store::loadLastSelectedConsole(stored)) {
		// Clamp rather than trust: the stored index refers to the
		// config that wrote it, and a hand-edited /lastconsole is one
		// bad boot away from being out of range. CurrentConsole()
		// indexes without a bounds check, so this is load-bearing.
		// clampIndex() also folds the empty-list case to 0, which is
		// already the default cursor position.
		const int clamped = retroroom_core::clampIndex(stored, HowManyConsoles());
		Serial.print("consoles: restored selection index=");
		Serial.print(clamped);
		if (clamped != stored) {
			Serial.print(" (clamped from ");
			Serial.print(stored);
			Serial.print(")");
		}
		Serial.println();
		// Just the cursor. The rest of setup() reads
		// currentConsoleIndex as it goes -- ledstring_setConsole()
		// lights the right window, the LCD seeds its live lines from
		// CurrentConsole() at the startup->welcome handover -- so the
		// console is booted into rather than selected after the fact.
		currentConsoleIndex = clamped;
	} else {
		// No file, no filesystem, or a mount failure. The cursor stays
		// where setup() left it, but the latch below is still driven:
		// its physical position is unknown after a power cycle whether
		// or not we remembered a selection.
		Serial.println("consoles: no saved selection");
	}

	// Step the latch to whichever console we ended up on.
	//
	// This is the one case where the cursor and the hardware genuinely
	// disagree after a reboot: the latch shares a power rail with the
	// controller, so it does not hold position across a power cycle,
	// while the cursor is just a number we remembered. That is why the
	// drive below is unconditional rather than tied to whether the
	// cursor actually moved -- a device that saved index 0 and never
	// moved still has an arm sitting somewhere unknown.
	//
	// selectStack() counts a *relative* number of pulses from wherever
	// the arm sits, so it is only meaningful once setup() has homed the
	// latch. The two calls must stay in that order.
	//
	// Deliberately selectStack() and not selectConsole(): the LCD
	// paint inside selectConsole() is exactly what would cut the
	// welcome splash short. The IR code is left alone here too.
	if (HowManyConsoles() > 0) {
		selectStack(CurrentConsole().selector_position);
	}
}

void consoleDefinitions() {
	// Functional-core load: pure parse + validation, no Arduino headers in
	// the parse path. The returned LoadResult has ok=false + a typed error
	// string on any structural problem (missing irCodes, missing consoleNames,
	// unknown tvInput reference, etc.).
	//
	// Source selection:
	//   1. Try /consoles.json from LittleFS (the live slot maintained by
	//      POST /consoles.json in src/network.cpp). If the FS isn't mounted
	//      (boards without a filesystem partition) or the file is missing
	//      (factory-fresh device), fall through silently.
	//   2. Fall back to the embedded PROGMEM CONFIG_JSON literal above.
	//      This means the device always boots with *something* -- the
	//      example 3-console config -- even on a fresh device or a board
	//      with no FS at all.
	std::string source;
	std::string source_label;
	std::string fs_json;
	if (retroroom_store::loadLiveConsoleConfig(fs_json) && !fs_json.empty()) {
		source = std::move(fs_json);
		source_label = "LittleFS /consoles.json";
	} else {
		source.assign(CONFIG_JSON);
		source_label = "PROGMEM default (CONFIG_JSON)";
	}

	retroroom_core::LoadResult result =
		retroroom_core::loadFromJson(source.data(), source.size());
	if (!result.ok) {
		Serial.print("Console config load failed (");
		Serial.print(source_label.c_str());
		Serial.print("): ");
		Serial.println(result.error.c_str());
		return;
	}
	lcdBacklightOffAfterMs = result.lcdBacklightOffAfterMs;  // RAM-only; loaded per boot
	for (const auto& c : result.consoles) {
		addConsole(c);
	}
	Serial.print("Loaded ");
	Serial.print(result.consoles.size());
	Serial.print(" consoles from ");
	Serial.print(source_label.c_str());
	Serial.print("; LCD backlight off after ");
	Serial.print(lcdBacklightOffAfterMs);
	for (const auto& c : consoles) {
		Serial.print(" [");
		Serial.print(c.id.c_str());
		Serial.print(" -> ");
		Serial.print(c.name.c_str());
		Serial.print(" sel=");
		Serial.print(c.selector_position);
		Serial.print(" tvInput=0x");
		Serial.print(c.tvinput, HEX);
		Serial.print(" led=");
		Serial.print(c.led_position);
		Serial.print("..");
		Serial.print(c.led_position + c.led_width - 1);
		Serial.print("]");
	}
	Serial.println();
	Serial.print("LCD backlight off after ");
	Serial.print(lcdBacklightOffAfterMs);
	Serial.println(" ms");
}

void selectConsole(const Console& c) {
	// Drives the StackSelector AND blasts the IR code. Order matters:
	// the StackSelector takes a few ms to settle the ARM/CYCLE/ENABLE
	// state machine; blasting IR in parallel is fine (one-shot send).
	Serial.print("Select Console index=");
	Serial.print(currentConsoleIndex);
	Serial.print(": ");
	Serial.print(c.name.c_str());
	Serial.print(" selector=");
	Serial.print(c.selector_position);
	Serial.print(" tvInput=0x");
	Serial.print(c.tvinput, HEX);
	selectStack(c.selector_position);
#if defined(HAS_IR)
	setInput(c.tvinput);
#endif
	// LCD update + backlight wake. The display_* calls are inline
	// no-ops when HAS_LCD is undefined (see display.h).
	display_show_console(c.name.c_str(), c.tagline.c_str());
	display_wake();
#if defined(HAS_LEDS)
	// Paint the active console's LED window on the second strip (GP21).
	// This one insertion point covers every commit path: rotary press,
	// NEXT_CONSOLE_BTN, PREV_CONSOLE_BTN, HTTP /next, /prev, and the
	// WebSocket "console" message -- because they all funnel through
	// selectConsole() on the way to driving the StackSelector.
	//
	// No animation here; this is the steady-state "light the right
	// pixels on commit" path. The pull-tween animation between zones
	// is tracked under
	// todo/open/2026-09-25_led-string-light-shows_DRAFT.md (S1) and is
	// NOT part of this commit.
	ledstring_setConsole(currentConsoleIndex);
#endif

	// Remember the selection so it survives a power cycle.
	// selectConsole() is the single commit point for every selection
	// path (rotary press, NEXT/PREV buttons, /next, /prev, the WS
	// commands and the post-boot restore), so saving here covers them
	// all with no per-path bookkeeping.
	//
	// Best-effort by design: a failed write costs the selection across
	// the next power cycle and nothing more, so it must never take
	// down a selection that otherwise worked. The rotary *detent*
	// handler in src/controls.cpp moves currentConsoleIndex without
	// committing, which is right -- it only re-lights the ring, and
	// the press is what actually selects.
	retroroom_store::saveLastSelectedConsole(currentConsoleIndex);
}

void advanceConsole() {
	// Wrap-around console advance. Safe on an empty vector.
	// Forward step: +1, mirror of advanceConsole()'s pre-refactor
	// behavior (USR button on YD-RP2040 still advances forward on
	// every press).
	int n = HowManyConsoles();
	if (n <= 0) {
		Serial.println("advanceConsole: no consoles loaded");
		return;
	}
	currentConsoleIndex = retroroom_core::wraparoundNext(currentConsoleIndex, n, +1);
	// Stamp the selection time at-the-moment-of-decision (after
	// wraparoundNext but before any side effects / WS broadcast) so the
	// e2e harness can read `selectedAtUptimeMs` from /state.json and
	// assert the device actually moved (delta changes across calls).
	currentConsoleSelectedAtMs = millis();
	Serial.print("Button: advance -> index ");
	Serial.println(currentConsoleIndex);
	const Console& c = CurrentConsole();
	// Paint the new selection on the LED ring so the operator gets visual
	// confirmation on the perfboard. lightSingle writes one bright pixel
	// at the index (the rest dark blue) and is harmless if no ring is
	// wired (e.g. pico_yd with no external LEDs).
	lightSingle(currentConsoleIndex);
	selectConsole(c);
#if defined(HAS_WIFI)
	// Mirror the change to any connected web UI over WebSocket so the
	// page doesn't need to poll /state.json to stay in sync.
	{
		std::string msg = "console:";
		msg += c.name;
		msg += ":";
		msg += std::to_string(currentConsoleIndex);
		broadcastSocketMessage(msg);
	}
#endif
}

void rewindConsole() {
	// Wrap-around console rewind (counterpart of advanceConsole()).
	// Exposed for the /prev HTTP endpoint and the "prev" WebSocket
	// command so external scripts can drive the device in either
	// direction without needing the physical button. Same wraparound
	// math as advanceConsole() but stepping -1; the math itself
	// lives in the functional core (wraparoundNext) so the policy
	// stays unit-testable on the host.
	int n = HowManyConsoles();
	if (n <= 0) {
		Serial.println("rewindConsole: no consoles loaded");
		return;
	}
	currentConsoleIndex = retroroom_core::wraparoundNext(currentConsoleIndex, n, -1);
	// Stamp the selection time at-the-moment-of-decision (after
	// wraparoundNext but before any side effects / WS broadcast) -- same
	// contract as advanceConsole(). See comment there.
	currentConsoleSelectedAtMs = millis();
	Serial.print("Button: rewind -> index ");
	Serial.println(currentConsoleIndex);
	const Console& c = CurrentConsole();
	lightSingle(currentConsoleIndex);
	selectConsole(c);
#if defined(HAS_WIFI)
	{
		std::string msg = "console:";
		msg += c.name;
		msg += ":";
		msg += std::to_string(currentConsoleIndex);
		broadcastSocketMessage(msg);
	}
#endif
}
