#include "Console.h"
#include "stackselector.h"
#include "main.h"
#if defined(ESP8266)
#include "ircontrol.h"   // for setInput() -- only present when the IR blaster is built
#endif

Console::Console() {}


Console::Console(const std::string& _name, const int _tvinput, const int _selector_position, const int _led_position, const int _led_width) {
	led_position = _led_position;
	led_width = _led_width;
	selector_position = _selector_position;
	tvinput = _tvinput;
	name = _name;
}

void Console::selectConsole() {
	Serial.print("Select Console: ");
	Serial.println(name.c_str());
	selectStack(selector_position);
#if defined(ESP8266)
	setInput(tvinput);
#endif
}