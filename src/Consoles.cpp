#include "Consoles.h"

Consoles::Consoles() {
	currentConsoleIndex = 1;
}

int Consoles::getCurrentConsole() {
	return currentConsoleIndex;
}

int Consoles::setCurrentConsole(int console) {
	currentConsoleIndex = console;
	return currentConsoleIndex;
}
