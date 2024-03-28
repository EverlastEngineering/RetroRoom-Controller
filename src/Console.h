#ifndef CONSOLE_H
#define CONSOLE_H

#include <iostream>

class Console
{
  public:
  	Console();
    Console(const std::string& _name, const int tvinput, const int selector_position, const int led_position, const int led_width);
	int led_position;
	int led_width;
	int selector_position;
	int tvinput;
	 std::string name;
	void selectConsole();
};


#endif