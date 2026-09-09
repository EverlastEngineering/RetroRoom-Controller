#include "consoles.h"

#include <ConsoleConfig.h>

#include "ircontrol.h"
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
	Serial.println(" consoles from JSON config.");
}

void selectConsole(const Console& c) {
	Serial.print("Select Console: ");
	Serial.println(c.name.c_str());
	selectStack(c.selector_position);
#if defined(ESP8266)
	setInput(c.tvinput);
#endif
}
