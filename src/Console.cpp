#include "Console.h"
#include "stackselector.h"
#include "main.h"
#include "ircontrol.h"

// Name(string fName, string lName):fName(std::move(fName)), lName(std::move(lName))

Console::Console() {}


Console::Console(const std::string& _name, const int _tvinput, const int _selector_position, const int _led_position, const int _led_width) {
	led_position = _led_position;
	led_width = _led_width;
	selector_position = _selector_position;
	tvinput = _tvinput;
	name = _name;
}

void Console::selectConsole() {
	// huh, these are all the same.
	// int currentconsole = (*this).selector_position;
	// int currentconsole = this->selector_position;
	// int currentconsole = selector_position;

	Serial.print("Select Console: ");
	Serial.println(name.c_str());
	selectStack(selector_position);
	setInput(tvinput);
}