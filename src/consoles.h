#ifndef CONSOLES_H
#define CONSOLES_H

#include <Arduino.h>
#include "configuration.h"
#include "Console.h"
#include <vector>
#include <ArduinoJson.h>

extern std::vector <Console> consoles; 

void addConsole(Console console);
int howManyConsoles();
Console currentConsole();
void consoleDefinitions_init();

#endif