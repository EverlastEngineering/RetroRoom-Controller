# Stack selector cycle speed, in the config

**Status:** open
**File anchor:** `src/stackselector.cpp`, `src/consoles.cpp`
(`selectStack()`), `lib/ConsoleConfig/`

## What

The dwell or inter-pulse timing the StackSelector uses when stepping
through positions becomes a config value rather than a `#define`.

## Why

Same reason as everything else in the `led` block: it is a feel setting
dressed as a wiring constant. The StackSelector has to move at whatever
speed the hardware module can do reliably, and that speed is not
something the operator of a finished cabinet should have to recompile to
change.

This is also the one config item on the list that touches hardware
timing, which makes it the one most worth being careful about.

## How

- Find out whether the StackSelector's timing is actually a software
  dwell or a property of the module. If `src/stackselector.cpp` already
  waits between pulses, the software owns it and this is a
  read-and-clamp job like any other. If the module has a fixed
  requirement, the config value is a *ceiling* the firmware will not
  exceed, not a free parameter — and the parser should say so.
- `pin-map-chart.md` is the repo's standing answer to "is this a setting
  or is it wiring". A minimum pulse width that the module physically
  needs is wiring. A comfort margin above that minimum is a setting.
  Split them when this is written.
- Whatever it becomes, it belongs in its own block rather than in `led` —
  `led` is the strip's feel, and this is not the strip. A `stack` block,
  or a top-level key.
- One caution worth recording: the StackSelector is homed at boot
  (`selectStack_init()`) and shares a power rail with the controller, so
  it does not hold position across a power cycle. Anything that changes
  its timing interacts with the homing pass. Do not change the speed
  mid-homing.
- See `2026-09-30_reload-config-without-reboot.md` — a live reload that
  changes selector timing mid-run is a much worse idea than a live
  reload that changes a colour, and if reload lands this should be on
  the list of things it refuses to apply live.

## Not decided

- Whether the StackSelector is still expected to exist. There is a
  `todo/deferred/2026-09-12_stackselector-daisy-chain.md` and a
  perfboard pin-map item, so the module is at least planned. If the
  perfboard work changes the mechanism, this item's premise changes with
  it.
