// src/Console.h is now a shim that re-exports retroroom_core::Console. The
// functional-core type has no class methods to define here -- everything
// (parsing, selection math, wraparound) lives in lib/ConsoleConfig/. This
// .cpp is kept so the legacy `src/Console.cpp` build path remains stable
// for whatever build system assumes it exists.
#include "Console.h"
