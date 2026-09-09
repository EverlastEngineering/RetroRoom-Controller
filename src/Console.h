#ifndef CONSOLE_H
#define CONSOLE_H

// Re-export the functional-core Console type. The legacy src/Console.h
// declared a hand-rolled class with the same field layout; consolidating on
// the core type means the shell uses one source of truth for "what is a
// console".
#include <ConsoleConfig.h>
using Console = retroroom_core::Console;

#endif
