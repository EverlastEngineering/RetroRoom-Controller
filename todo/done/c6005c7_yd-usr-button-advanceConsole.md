# YD-RP2040 USR button advances console index

**Status:** done — commit [`c6005c7`](../../) on `session/merge-pico-json`
**Completed:** 2026-09-12
**File anchor:** [src/consoles.cpp](../../src/consoles.cpp)`advanceConsole()`, [src/controls.cpp](../../src/controls.cpp)`controls_init()`

## What
- Added new free function [src/consoles.cpp](../../src/consoles.cpp)`advanceConsole()` that:
  1. Increments `currentConsoleIndex` modulo `HowManyConsoles()` (wrap-around: last → first).
  2. Paints one bright pixel via `lightSingle(currentConsoleIndex)` for visual confirmation on the perfboard.
  3. Calls `selectConsole(CurrentConsole())` which drives the StackSelector (`selectStack(c.selector_position)`) and the IR blaster (`setInput(c.tvinput)` once IR was restored in `643be7c`).
  4. Logs the new index + name + selector + `tvInput` hex on every press.

- Bound as `touchSensor.onPressed(advanceConsole)` in [src/controls.cpp](../../src/controls.cpp)`controls_init()`. The YD-RP2040 USR button (TOUCH_SENSOR_PIN = GP24) maps to `touchSensor` in [src/configuration.h](../../src/configuration.h).

- On the perfboard the same pin will be the capacitive touch input, so the same handler will fire — no other wiring change needed.

## Implementation notes
- EasyButton's `wasReleased()` only fires `_pressed_callback()` when `_was_btn_held == false`. Since we don't register `onPressedFor` (which would set `_was_btn_held = true on presses longer than the threshold), the callback fires reliably on every release edge. **Do NOT add `touchSensor.onPressedFor(...)`** — this bit us in commits before `1f0502d`.
- In POLL mode, EasyButton requires `touchSensor.read()` (not `update()`) to read the pin and fire callbacks. We call `read()` from `loop()`.
- Removed the legacy `rotaryEncoderTick()` poll that called `touchDetected()` on every iteration while the button was held (that was the bug fixed in `a38f4a0`); the white-pixel-paint path is gone.

## Done-when evidence
- USR button on YD-RP2040 advances `currentConsoleIndex` 0 → 1 → 2 → 0 with wrap-around.
- LED ring on the perfboard tracks the selection (when perfboard lands).
- TV input changes via IR (when IR wiring is confirmed via bench test).
