#ifndef CONSOLE_H
#define CONSOLE_H

// Re-export the functional-core Console type. The legacy src/Console.h
// declared a hand-rolled class with the same field layout; consolidating on
// the core type means the shell uses one source of truth for "what is a
// console".
#include <ConsoleConfig.h>
using Console = retroroom_core::Console;
// Same re-export, for the shelf-extent record. See the note above for
// why the shell types these without the namespace.
using Shelf = retroroom_core::Shelf;

#endif
