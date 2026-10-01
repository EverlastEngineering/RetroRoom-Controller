#pragma once

// The config a cabinet ships with, as a plain NUL-terminated string.
//
// The document itself lives in src/factory-config.json, wrapped in
// `R""""(` / `)""""` and pulled in by the preprocessor here -- the same
// trick src/html.h uses for the UI files. Two reasons it is a real file
// and not a literal pasted into a .cpp:
//
//   - It is the one place the shipped default is written down. It was
//     previously a 1.5 KB string literal in src/consoles.cpp, which is
//     unreadable in a diff and impossible to check with a JSON parser.
//   - Adding a config field means adding a menu row for it, and a
//     hand-maintained copy of the field list is exactly the thing that
//     silently rots. A test parses this file and asserts that every
//     field in the registry has a row, so the two cannot drift apart
//     without the suite going red.
//
// It is NOT named .json for the same reason src/html/script.js is not
// valid JavaScript: the raw-string wrapper makes it a C++ token
// sequence first and a data file second. The payload between the
// markers is valid JSON, and test_console_config parses it to prove it.
//
// No PROGMEM here on purpose. RP2040 has no separate flash mapping for
// string constants -- arduino-pico defines PROGMEM as empty -- so the
// qualifier buys nothing and would only stop this header being
// included from a host test.

static const char kFactoryConfigJson[] =
#include "factory-config.json"
    ;
