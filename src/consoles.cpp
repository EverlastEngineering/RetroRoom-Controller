#include "consoles.h"

int currentConsoleIndex = 0;
std::vector<Console> consoles;

void addConsole(Console console) { consoles.push_back(console); }

int HowManyConsoles() { return consoles.size(); }

Console CurrentConsole() { return consoles[currentConsoleIndex]; }