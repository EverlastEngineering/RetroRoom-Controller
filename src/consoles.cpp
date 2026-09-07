#include "consoles.h"

int currentConsoleIndex = 0;
std::vector<Console> consoles;

void addConsole(Console console) { consoles.push_back(console); }

int howManyConsoles() { return consoles.size(); }

Console currentConsole() { return consoles[currentConsoleIndex]; }

void consoleDefinitions_init() {
	char json[] = "{\"IRCodes\":{\"SVideo\":\"0x030\",\"Front\":\"0x830\",\"Video\":\"0x430\",\"YUV\":\"0xE30\"},\"ConsoleDefinitions\":{\"NES\":\"Nintendo Entertainment System\",\"SNES\":\"Super Nintendo Entertainment System\",\"GC\":\"Nintendo Gamecube\",\"N64\":\"Nintendo 64\",\"Wii\":\"Nintendo Wii\",\"TG16\":\"TurboGrafx-16\",\"PS1\":\"Sony PlayStation\",\"PS2\":\"Sony PlayStation 2\",\"SMS\":\"Sega Master System\",\"GEN\":\"Sega Genesis\",\"DREAM\":\"Sega Dreamcast\",\"XBOX\":\"Microsoft Xbox\"},\"Consoles\":[{\"NES\":[3,1,5],\"SNES\":[4,5,5],\"GEN\":[4,10,5]}]}";

	JsonDocument doc;
	deserializeJson(doc, json);

	const char* sensor = doc["sensor"];
	long time          = doc["time"];
	double latitude    = doc["data"][0];
	double longitude   = doc["data"][1];
	Serial.println("Setup Complete.");
	Serial.println(sensor);
	Serial.println(longitude);
	Serial.println(latitude);
	Serial.println(time);
}

/**
 * Deprecated
 */
void ___consoleDefinitions() {
	/**
	 * Console takes:
	 * name: The friendly name of the console.
	 * enum of the Inputs on the television
	 * selector_position: The position in the StackSelector system.
	 * led_position: The position of the first led on the rgb string for this console.
	 * led_width: How many leds in the strip are lit when this console is selected.
	 */

	addConsole(Console(NES, Composite,		1, 5, 	1));
	addConsole(Console(SNES, SCART,			2, 15,	5));
	addConsole(Console(Genesis, SCART,		3, 15,	5));

	Serial.println(howManyConsoles());
}

/**
 * example json
 */

void ___notused() {
	char json[] = "{\"sensor\":\"gps\",\"time\":1351824120,\"data\":[48.756080,2.302038]}";

	JsonDocument doc;
	deserializeJson(doc, json);

	const char* sensor = doc["sensor"];
	long time          = doc["time"];
	double latitude    = doc["data"][0];
	double longitude   = doc["data"][1];
	Serial.println("Setup Complete.");
	Serial.println(sensor);
	Serial.println(longitude);
	Serial.println(latitude);
	Serial.println(time);
}