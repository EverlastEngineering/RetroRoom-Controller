# StackSelector daisy-chain length config

**Status:** deferred (waiting on the perfboard)
**Branch:** session/merge-pico-json
**File anchor:** [src/stackselector.cpp](../../src/stackselector.cpp) `selectStack(int position)`

## What
Add a `STACK_MODULES` config constant (or runtime NVR-stored value)
that drives the daisy-chain reset cycle in `selectStack()`.

## Why
Today `selectStack(int position)` hard-codes the "home" position to
`HowManyConsoles() + 1` (line 13 of [src/stackselector.cpp](../../src/stackselector.cpp))
and the cycle duration to `HowManyConsoles()` (line 36). These work
for a single StackSelector module with one LED per console, but if
the perfboard ever has two modules daisy-chained, the home position
needs to be `HowManyConsoles() + HowManyConsolesPerChain` and the
cycle duration doubles.

## How (when the perfboard lands)
1. Add `#define STACK_MODULES 1` to [src/configuration.h](../../src/configuration.h) (default).
2. Update `selectStack()` to use `STACK_MODULES * HowManyConsoles()` for the reset cycle.
3. When persisting the current console selection is added (separate TODO), persist `STACK_MODULES` too.

## Related
- [src/stackselector.cpp](../../src/stackselector.cpp) -- the routine itself.
- [todo/open/2026-09-12_stackselector-perfboard-pinmap.md](../open/2026-09-12_stackselector-perfboard-pinmap.md) -- the perfboard wiring TODO.
