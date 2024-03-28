#ifndef CONSOLES_H
#define CONSOLES_H

#include <Arduino.h>
#include "configuration.h"
#include "Console.h"
#include <vector>

extern std::vector <Console> consoles; 

void addConsole(Console console);
int HowManyConsoles();
Console CurrentConsole();

#endif