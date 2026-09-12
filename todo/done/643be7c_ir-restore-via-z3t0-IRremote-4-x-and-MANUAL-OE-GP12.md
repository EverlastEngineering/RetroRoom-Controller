# IR blaster restored via z3t0/IRremote@4.x + MANUAL_OE_PIN = GP12

**Status:** done — commit [`643be7c`](../../) on `session/merge-pico-json`
**Completed:** 2026-09-12
**File anchor:** [src/ircontrol.h](../../src/ircontrol.h), [src/ircontrol.cpp](../../src/ircontrol.cpp), [src/configuration.h](../../src/configuration.h)

## What
- Re-created [src/ircontrol.{h,cpp}](../../src/ircontrol.cpp) as thin wrappers around the v4 `IRsend IrSender` global.
- `setInput(int hex)` decodes a 12-bit Sony SIRC value into (address, command) per the legacy mapping (`address = (v >> 7) & 0x1F`, `command = v & 0x7F`) and calls `IrSender.sendSony(addr, cmd, 2)`.
- `[src/consoles.cpp](../../src/consoles.cpp)``selectConsole()` now calls `setInput(c.tvinput)` after `selectStack()`, gated on `#if defined(HAS_IR)`.
- `#define MANUAL_OE_PIN 12` added to the RP2040 pin block in [src/configuration.h](../../src/configuration.h); the existing `#ifdef MANUAL_OE_PIN` guards in [src/state.cpp](../../src/state.cpp) + [src/main.cpp](../../src/main.cpp) activate automatically.
- `-D HAS_IR` added to all three Pico envs' `build_flags` in [platformio.ini](../../platformio.ini).
- The `<IRremote.hpp>` include is restricted to [src/ircontrol.cpp](../../src/ircontrol.cpp) to avoid linker multiple-definition errors (the library has non-inline globals — `IRrecv::decode`, the timer helpers, the feedback LED state — that get duplicated if multiple TUs pull in the header).
- [pico-pin-mapping.md](../../pico-pin-mapping.md) updated with `MANUAL_OE_PIN = GP12` in the per-role table and KiCad net list, IR_CONTROL_PIN consumer updated to `src/ircontrol.cpp::ir_control_init()`.

## Implementation notes
- The include discipline matters: I learned (the hard way) that `<IRremote.hpp>` from a header file (vs just a .cpp) causes linker multiple-definition errors because the library's `IRrecv::decode`, `IRsend::setLEDFeedback`, etc., are non-inline definitions that get pulled into every TU. Including it only in `src/ircontrol.cpp` was the fix.
- Default feedback LED (`USE_DEFAULT_FEEDBACK_LED_PIN`) is left on because LED_BUILTIN (= GP25) isn't used for anything else right now; the blip on every send is informative on the bench. Disable later with `-D NO_LED_SEND_FEEDBACK_CODE` in lib_extra_options.

## Done-when evidence
- All three Pico envs build clean: `pico_base` 4.0% RAM / 2.8% Flash, `picow` 26.6% / 15.0%, `pico_yd` 3.7% / 0.3%.
- Flash via [pico_yd env](platformio.ini): boot prints `IR sender initialized on GP7`. Each `advanceConsole()` press logs `Select Console index=N: ... tvInput=0xH` and bursts the SIRC code on GP7.
