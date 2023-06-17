#ifndef CONSOLE_H
#define CONSOLE_H

#include <iostream>

class Console
{
  public:
    Console(const std::string& _name, const std::string& tvinput, const int selector_position, const int led_position, const int led_width);
	int led_position;
	int led_width;
	int selector_position;
	 std::string tvinput;
	 std::string name;
};

#endif