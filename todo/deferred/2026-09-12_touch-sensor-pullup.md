# External 10 kΩ pull-up on TOUCH_SENSOR_PIN (GP5)

**Status:** deferred (waiting on the perfboard)
**Branch:** session/merge-pico-json
**File anchor:** [pico-pin-mapping.md](../../pico-pin-mapping.md) Section 2 wiring notes

## What
PCB reminder: the perfboard schematic must include an external 10 kΩ
pull-up resistor between TOUCH_SENSOR_PIN (GP5) and 3.3V.

## Why
The RP2040's internal pull-up is too weak (~50 kΩ) for reliable
capacitive sensing on the EasyButton-based touch front end. An
external 10 kΩ keeps the line idle-high at boot (when no peripheral
is driving it) and gives the capacitive touch handler a clean
default state.

## How (when the perfboard schematic lands)
1. Place a 10 kΩ resistor between GP5 and the 3.3V rail on the perfboard.
2. Verify the touch front end works (no false triggers at boot, response on touch is reliable).

## Related
- [todo/open/2026-09-12_stackselector-perfboard-pinmap.md](../open/2026-09-12_stackselector-perfboard-pinmap.md) -- same trigger (perfboard lands).
