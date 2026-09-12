# StackSelector perfboard pin map

**Status:** open
**Branch:** session/merge-pico-json
**File anchor:** [src/stackselector.h](../../src/stackselector.h), [src/stackselector.cpp](../../src/stackselector.cpp), [src/configuration.h](../../src/configuration.h)
**Related doc:** [pico-pin-mapping.md](../../pico-pin-mapping.md) Section 5

## What
When the StackSelector perfboard revision lands, settle on the actual
physical pin numbers for the ARM/CYCLE/ENABLE trio and update
[src/configuration.h](../../src/configuration.h)'s RP2040 block.

Current placeholders (from [src/configuration.h](../../src/configuration.h)):
```c
#define ARM_PIN           8
#define CYCLE_PIN         9
#define ENABLE_PIN        10
```

## Why
The current numbers (GP8/GP9/GP10) are *placeholder* values chosen to
avoid strapping pins on the off-the-shelf Pico header. The real
perfboard will have specific GPIO assignments that may differ.

## How
1. Wait for the perfboard schematic.
2. Map StackSelector ARM/CYCLE/ENABLE pins from the perfboard's GPIO
   header to the three roles above.
3. Update the `#define` values in [src/configuration.h](../../src/configuration.h).
4. If anything on the perfboard overlaps with GP0/GP1 (UART0) or GP25
   (LED_BUILTIN), document the conflict and pick a different pin.
5. Update [pico-pin-mapping.md](../../pico-pin-mapping.md) Section 2 + KiCad net list.
6. Smoke test: load the firmware on the perfboard, run
   `advanceConsole()` (USR button → wraps `currentConsoleIndex`), and
   confirm the LEDs track on the actual hardware.

## Related
- [src/stackselector.cpp](../../src/stackselector.cpp): `selectStack(int position)`
  routine -- the 4-pulse `enableStack/armStack/clockCycle/disarmStack`
  sequence that drives the shift register chain.
