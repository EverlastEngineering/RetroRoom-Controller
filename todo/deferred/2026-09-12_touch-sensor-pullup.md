# External 10 kΩ pull-up on TOUCH_SENSOR_PIN (GP12)

**Status:** deferred (waiting on the perfboard)
**Deferred on:** 2026-09-12, rewritten 2026-09-24 (pin corrected from GP5 → GP12)
**Branch:** session/pico-2-wireless
**File anchor:** [pin-map-chart.md](../../pin-map-chart.md)

## What
PCB reminder: the perfboard schematic must include an external 10 kΩ
pull-up resistor between `TOUCH_SENSOR_PIN` (**GP12**, per
[pin-map-chart.md](../../pin-map-chart.md)) and 3.3V.

## Why
The RP2040's internal pull-up is too weak (~50 kΩ) for reliable
capacitive sensing on the EasyButton-based touch front end. An
external 10 kΩ keeps the line idle-high at boot (when no peripheral
is driving it) and gives the capacitive touch handler a clean
default state.

## How (when the perfboard schematic lands)
1. Place a 10 kΩ resistor between GP12 and the 3.3V rail on the
   perfboard.
2. Verify the touch front end works (no false triggers at boot,
   response on touch is reliable).

## History note
An earlier revision of this file (pre-2026-09-24) referenced GP5 as
the touch pin. That was incorrect — the pin-map reshuffle (see
[pin-map-chart.md](../../pin-map-chart.md) and the deleted
`pin-map-plan.md`) moved `TOUCH_SENSOR_PIN` from GP5 to GP12, freeing
GP4/GP5 for I2C0. This rewrite aligns the file with the current chart.

## Related
- [pin-map-chart.md](../../pin-map-chart.md) — GP12 = `TOUCH_SENSOR_PIN`.
- [todo/deferred/2026-09-12_stackselector-perfboard-pinmap.md](../deferred/2026-09-12_stackselector-perfboard-pinmap.md) -- same trigger (perfboard lands).
