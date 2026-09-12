# Pinout doc (pico-pin-mapping.md)

**Status:** done — commit [`b603d4b`](../../) on `session/merge-pico-json`
**Completed:** 2026-09-12

## What
Added [pico-pin-mapping.md](../../pico-pin-mapping.md) (186 lines) with five sections:

1. **Per-role name table** — every `#define` in [src/configuration.h](../../src/configuration.h) + which file consumes it.
2. **Raspberry Pi Pico perfboard target** — GP2-GP10 table + silkscreen legend + KiCad net list + 5 wiring notes (470 Ω series resistor on DATA_PIN, 10 kΩ external pull-up on TOUCH_SENSOR_PIN, MANUAL_OE_PIN purposely undefined at the time, GP0/GP1 reserved for USB-CDC, GP25 reserved for `LED_BUILTIN`).
3. **YD-RP2040 dev board** — overrides for `DATA_PIN=23` (onboard WS2812) and `TOUCH_SENSOR_PIN=24` (USR button), plus the early-board RGB-pad solder-bridge note linking to the upstream gist.
4. **Open questions** in priority order — IR re-add, `MANUAL_OE_PIN` location (suggested GP12), StackSelector daisy-chain, I2C expansion, pull-up confirmation, CYW43 wifi.
5. **Source-of-truth anchors** — points readers back to the actual source files so the doc never drifts silently.

## Implementation notes
- The doc gets updated alongside each item. After commit `643be7c` (IR restore + GP12 manual-OE), the doc was updated to: MANUAL_OE_PIN now wired to GP12, IR_CONTROL_PIN consumer updated to `src/ircontrol.cpp::ir_control_init()`. See this file's "see also" section for the post-commit state.
- The "open questions" section is deliberately tied to [TODO.md](../../TODO.md) (now [todo/](../)) so when an item closes there, this doc gets the same update.
