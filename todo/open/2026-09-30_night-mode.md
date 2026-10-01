# Night mode — the selected console's LEDs stay dark

**Status:** open
**File anchor:** `src/controls.cpp` (double-click detection),
`lib/ConsoleConfig/src/ConsoleConfig.h` (`led.nightMode`),
`src/ledstring.cpp` (the resting paint), `src/html/` (the setup page,
if it grows a control)

## What

A toggle that makes the selected console's window on the GP21 string stay
dark instead of lit, so a cabinet left in a dark room does not glow.

- Double-click the rotary knob while a console is already selected →
  toggles night mode.
- While night mode is on, **every** selection ends with that console's
  window dark. Not "the double-click hides it once" — the mode persists,
  and the next selection stays dark too.
- Browsing still shows the blob and the preview, so the operator can
  still see what they are choosing. Only the *resting* selected window
  is affected. A browse that is abandoned must also settle dark.

**Config:** `led.nightMode`, boolean, default `false`.

## Why

The strip sits next to a television in a dark room. When nothing is
playing, a fully lit window is a glare problem — this is the same
reasoning already behind the conservative default colours in
`LedFeelDefaults.cpp`.

The trigger is a double-click on the knob rather than a menu item
because it is the one gesture that is already in the muscle memory: the
knob has two acts (turn to browse, click to commit) and a third that
only exists in this state is hard to discover and easy to hit by
accident while browsing.

## How

- `EasyButton::onSequence(2, <ms>, cb)` is already available on
  `rotarySelector` and already has a stub for it —
  `sequenceElapsed()` in `src/controls.cpp`, currently dead, with the
  `onSequence` registration commented out two lines above it. Wire it up
  and check a console is selected (i.e. the browse is not mid-step)
  before treating the double-click as the toggle rather than as two
  clicks.
- The blink rate in night mode should be distinguishable from the normal
  resting paint without being *slower* than the idle timeout — the ring
  fades in `ringFadeMs` and a slow blink would read as a fault.
- Decide whether night mode survives a reboot. It almost certainly
  should not: it is a room condition, not a cabinet preference, and a
  cabinet that boots glowing after a power cut is the exact problem
  being solved. Default to not persisted, and say so in the config docs.
- The resting paint lives in `paintResting()` in `src/ledstring.cpp` and
  resolves through `computeStripFrame()` in `lib/LedStringPaint`. Put the
  suppression in the core so it is host-testable, not in the shell — see
  the note in `led-feel-dump.sh` about the core owning the decision.

## Not decided

- Whether the ring should also dim in night mode. It is 8 pixels and
  close to the operator's hand, so probably not, but the operator is the
  one who can say.
- Whether night mode should be a menu item as well as a double-click.
  Probably yes, once there is a menu to put it in.
