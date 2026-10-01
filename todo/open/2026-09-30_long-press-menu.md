# Long press the knob → an on-screen menu

**Status:** open
**File anchor:** `src/controls.cpp` (long-press detection),
`src/display.cpp` (the LCD rendering), `lib/ConsoleConfig/` (the
`menu` schema + the generic setter), new `lib/CabinetMenu/` (the
navigation state machine)

## What

Hold the rotary knob to open a menu on the LCD, from which the operator
can adjust the cabinet without a laptop.

- **Long press** (not click) enters and leaves the menu. A click is
  already "commit the console", and overloading it would make browsing
  twitchy.
- Rotary **turns** move between menu items.
- Rotary **click** changes the value of the highlighted item.
- The menu is **defined in `/consoles.json`**, not compiled in: the
  operator adds, removes and reorders items to suit the cabinet, and can
  leave out anything they do not want to be able to change by accident.

**Config shape** — an array, each entry naming a *path into the same
`led` block everything else uses*, so a menu item and the JSON key it
edits cannot drift apart:

```json
"menu": [
  { "label": "Brightness", "set": "led.brightnessPct",
    "type": "int", "min": 10, "max": 100, "step": 10 },
  { "label": "Night mode",  "set": "led.nightMode", "type": "bool" }
]
```

Item types to start with: `bool` (click toggles) and `int` (click
cycles up, long-press-and-turn down, or click cycles and shift reverses).
`choice` (a fixed set of named values) if there is a need for it.

**LCD stays fully lit while the menu is open**, whatever
`lcd.backlightOffAfterMs` says — see
`2026-09-30_menu-lcd-backlight-override.md`.

## Why

Every knob of this cabinet currently needs a text editor and a reboot.
That is fine at commissioning and wrong for an operator standing in
front of it. The knob already has two well-learned gestures; a third —
hold — is the natural home for a mode that is not about choosing a
console.

Making the *contents* of the menu data-driven rather than compiled in is
the part worth being careful about, and the user was explicit about it:
a menu that only offers what the firmware happens to implement is a menu
with a fixed set of opinions. Letting the operator write it means the
cabinet can be adjusted for the room it is in, and can be *locked down*
by deleting entries.

## How

- **Long press**: `EasyButton::onSequence(1, <ms>, cb)` on
  `rotarySelector`. There is already a dead `sequenceElapsed()` in
  `src/controls.cpp` with the registration commented out above it, so the
  plumbing exists. Pick the threshold by feel and say where the number
  came from.
- **The navigation state machine goes in `lib/CabinetMenu/`,** not in
  `src/display.cpp`. This is the lesson of the ring refactor: a state
  machine in `src/` is a state machine nothing can test, and the menu
  has exactly the properties worth pinning — wrapping at the ends,
  clamping an int at its max, a bool actually toggling, a disabled item
  being skipped.
- **The generic setter** (`setLedFeel(LedFeel*, const char* dottedPath,
  int)`) lives in `lib/ConsoleConfig` next to the parser, and is the
  only writer of a config value from runtime. Both the menu and a future
  live reload go through it. A dotted-path setter needs no per-key
  plumbing, which is what makes "add a menu item" a JSON edit rather than
  a firmware change.
- **LCD**: 16×2 is tight. One item per screen, label and value on one
  line, position on the other. Do not try to fit a list.
- Exiting the menu must return the strip and ring to their normal state
  and hand the knob back to browsing, including if the exit is a timeout
  rather than a press.

## Sub-items

- `2026-09-30_menu-attract-mode.md`
- `2026-09-30_menu-light-show-mode.md`
- `2026-09-30_menu-lock-down-mode.md`
- `2026-09-30_led-brightness-config.md` — the first menu item to exist
- `2026-09-30_menu-lcd-backlight-override.md`

## Not decided

- What a click does on a `choice` item — cycles forward, and how does
  the operator go backwards? A second gesture is needed, and on a knob
  with one button that is the whole design problem.
- Whether the menu should be reachable over HTTP as well as the knob. If
  so it wants the same `lib/CabinetMenu` and the same setter, which is
  an argument for building it that way from the start.
