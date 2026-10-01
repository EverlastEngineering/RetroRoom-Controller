# Overall LED brightness level, in the config

**Status:** open
**File anchor:** `lib/ConsoleConfig/` (`led.brightnessPct`),
`src/ledstring.cpp` (`pushFrame`), `src/lighting.cpp` (`LED_BRIGHTNESS`)

## What

One number in `/consoles.json` that scales every LED the cabinet
drives — the GP21 string and the ring together.

**Config:** `led.brightnessPct`, 1..100, default 100.

This is the first item to build, because
`2026-09-30_long-press-menu.md` needs a real `int` menu item to exist
before the menu has anything worth showing.

## Why

The colours in `LedFeelDefaults.cpp` are deliberately conservative
because the strip sits next to a television in a dark room — and the
comment there says so. But conservatism baked into a default is not the
same as control: a cabinet in a bright room wants *more* than the
default, and today the only way to get it is to edit the per-role colours
one at a time and get the relative balance wrong on the way.

## How

- **Apply it as a single global scale, not per role.** The role
  percentages (`selfPct`, `dimPct`, `fillPct`, …) are *relative* and are
  tuned against each other. Multiplying them all by one factor preserves
  the balance the bench work established; adding a brightness field to
  each role would invite exactly the drift the single field avoids.
- Put the scale at the point the levels become pixels, not in the parser
  and not in the role maths — otherwise every percentage in the config
  silently means something different. `pushFrame()` in
  `src/ledstring.cpp` is the natural place for the string, and
  `LED_BRIGHTNESS` / `FastLED.setBrightness()` for the ring.
  **Note the two are not currently in step**: the string is scaled by
  role percentage and the ring by a compile-time `LED_BRIGHTNESS = 150`,
  so "one number scales everything" means unifying two mechanisms.
- The ring's `LED_BRIGHTNESS` is a `#define` in `src/lighting.cpp`.
  Moving it into the `led` block is the same migration `ringIdleMs` and
  `ringFlashMs` already went through, and the reason they are there is
  recorded in the note `src/configuration.h` leaves behind.
- Consider a floor as well as a scale. At very low percentages the ring's
  DarkBlue base may round to nothing and the ring will read as a single
  floating pixel rather than a lit ring.
- Clamp and report like everything else in the `led` block — the parser
  has a working accept-and-clamp path and a warning list, and a
  brightness of 500 in the file must not be allowed through silently.

## Not decided

- Whether brightness should be remembered across a reboot. The room
  changes; the cabinet does not move. A default of 100 with a
  config-file setting is probably the right answer, but if the menu can
  change it, somebody will want it to stick.
