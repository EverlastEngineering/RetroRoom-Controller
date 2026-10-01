# Reload /consoles.json without rebooting

**Status:** open
**File anchor:** `src/network.cpp` (`POST /consoles.json`),
`src/consoles.cpp` (`consoleDefinitions()`), `src/main.cpp` (`setup()`)

## What

Decide whether a `POST /consoles.json` should take effect immediately or
require a reboot, and implement the answer.

**The question, as asked:** can we reload the JSON config without
rebooting the machine, or is it safer to just reboot?

## Why

It is not a free choice. Reloading live means every value in `LedFeel`
can change under running code, and the session found three bugs today
that were all "a value was read before the config was parsed or cached
at the wrong time". A live reload is the same hazard wearing a new hat,
except it can happen while the cabinet is in use.

Concretely, these are all read at init and would go stale:

- `FastLED.addLeds<...>(selectedLeds, ledFeel.totalLeds)` in
  `ledstring_init()` — the strip buffer is sized at the call.
- The ring's `pixelCount` mapping, though that one is harmless.
- Anything holding a `RingState` or a `DetentGate` across the reload.

**My answer, for the record:** reload is doable and worth having, but it
needs the values that are compile-time-bound to be explicitly immutable
rather than accidentally so. That is a smaller change than it sounds
because `configuration.h` already documents which pins are the build
(`pin-map-chart.md` is the standing answer to "is this a setting or is it
wiring"), and `totalLeds` is already clamped to the build capacity by
the parser.

## How

- Split the parse from the apply. `consoleDefinitions()` currently does
  both, and the apply half touches LittleFS-free state, so the split is
  mostly mechanical.
- Make the reload *validate first, apply second*: parse into a
  `LoadResult`, and only if `ok` and no warnings, swap it in. A rejected
  file must leave the running cabinet exactly as it was.
- Re-apply what is re-appliable (every `led.*` timing and colour, the
  console list, the LCD fields) and log clearly what is not
  (`totalLeds` clamped to capacity, anything bound into a FastLED
  controller).
- The generic setter the menu needs
  (`setLedValue(LedFeel*, path, value)`) should be the *only* writer, so
  a live reload and a menu edit go through one path and cannot disagree.
- Until this lands, a reboot after upload is the honest answer, and
  `POST /consoles.json` should say so in its response body. It may
  already.

## Open question for the operator

Is the upload a commissioning-time thing (edit the file, reboot once,
done) or an operator thing (tune from a phone while the cabinet is
running)? The answer decides whether live reload is worth the risk at
all.
