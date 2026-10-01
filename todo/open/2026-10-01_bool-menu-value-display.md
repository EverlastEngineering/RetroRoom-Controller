# Show True / False for a bool menu item, not 1 / 0

**Status:** open
**File anchor:** `lib/CabinetMenu/src/CabinetMenu.cpp` (the editor's
value row), `MenuItem::isBool`

## What

A menu item declared `"type": "bool"` edits a 0/1 range, and the editor
prints the number. So the row for `network.disable` reads `New: 1` and
the operator has to know that 1 means on. Turn the knob and watch it
go 0, 1, 0, 1.

It should read `On` / `Off`, or `true` / `false`, and the *display* is
the whole change. The stored value stays 0/1, because that is what the
config field is and what `applyConfigValue()` clamps against.

## Why

There are now four bools in the registry and two of them are switches
whose state changes what the cabinet does. `New: 1` next to a label
called "WiFi off" is a genuinely bad row: the operator has to read the
label, remember which way round the number is, and then trust it. A
label that says `Off` next to "WiFi off" needs nothing remembered.

The parser already works this out -- `"type": "bool"` sets
`MenuItem::isBool` and forces the range to 0..1 -- so the information
is there and only the rendering ignores it.

## How

- In the editor branch of `menuView()`, format the draft through the
  item's `isBool` flag rather than with `%d`. The core owns the
  rendering, which is the right place: it is the only place that knows
  both the value and the flag.
- Pick one spelling and use it everywhere: in the editor, in the
  "Current:" confirmation, and in the value the menu commits. Mixed
  spellings in two rows of the same screen is worse than either.
- Sixteen columns is the budget. `true` / `false` fits with room;
  `Enabled` / `Disabled` needs the label to be short. Check the longest
  label in `src/factory-config.json` against the widest value.
- Add a test alongside the existing `test_a_bool_alternates`, which
  covers the *behaviour*. This is about what is painted, and the two
  are separate -- a bool that alternates 0/1 correctly and displays as
  a number is still wrong.
- Worth checking at the same time: the knob's own response. A bool that
  alternates has no detent gradient, so the operator turning it gets no
  feel of how far they have got. Whether that wants a sound, a ring
  flash, or nothing is a separate question and probably a separate todo.

## Not decided

- `On`/`Off` versus `true`/`false`. The config file says `true` /
  `false` and the menu would then agree with the file, which is worth
  something. But `On`/`Off` is what a person reads faster. Tie this to
  the bool spellings the Web Serial UI is using, so the three surfaces
  do not disagree.
- Whether a bool should be a single row that toggles, or an editor you
  enter. It alternates already, so a single row is smaller -- but then
  the value is only visible while the knob is turning, which is
  arguably worse than a static `1`.
