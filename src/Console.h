#ifndef CONSOLE_H
#define CONSOLE_H

#include <iostream>
#include "main.h"

class Console
{
  public:
    Console(int _led_position, int _led_width, int _selector_position, std::string _tvinput, std::string _name);
	int led_position;
	int led_width;
	int selector_position;
	std::string tvinput;
	std::string name;
};

#endif