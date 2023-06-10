#include "Console.h"

// Name(string fName, string lName):fName(std::move(fName)), lName(std::move(lName))

Console::Console(int _led_position, int _led_width, int _selector_position, std::string _tvinput, std::string _name) {
	led_position = _led_position;
	led_width = _led_width;
	selector_position = _selector_position;
	tvinput = _tvinput;
	name = _name;
}