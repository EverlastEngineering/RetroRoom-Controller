#include "consoles.h"

#include <ConsoleConfig.h>

#include <string>

#include "lighting.h"
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

void consoleDefinitions() {
	// Functional-core load: pure parse + validation, no Arduino headers in
	// the parse path. The returned LoadResult has ok=false + a typed error
	// string on any structural problem (missing irCodes, missing consoleNames,
	// unknown tvInput reference, etc.).
	retroroom_core::LoadResult result = retroroom_core::loadFromJson(CONFIG_JSON);
	if (!result.ok) {
		Serial.print("Console config load failed: ");
		Serial.println(result.error.c_str());
		return;
	}
	for (const auto& c : result.consoles) {
		addConsole(c);
	}
	Serial.print("Loaded ");
	Serial.print(result.consoles.size());
	Serial.print(" consoles from JSON:");
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
}

void advanceConsole() {
	// Wrap-around console advance. Safe on an empty vector.
	int n = HowManyConsoles();
	if (n <= 0) {
		Serial.println("advanceConsole: no consoles loaded");
		return;
	}
	currentConsoleIndex = (currentConsoleIndex + 1) % n;
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
