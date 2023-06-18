#include "Console.h"

// Name(string fName, string lName):fName(std::move(fName)), lName(std::move(lName))

Console::Console() {}


Console::Console(const std::string& _name, const std::string& _tvinput, const int _selector_position, const int _led_position, const int _led_width) {
	led_position = _led_position;
	led_width = _led_width;
	selector_position = _selector_position;
	tvinput = _tvinput;
	name = _name;
}