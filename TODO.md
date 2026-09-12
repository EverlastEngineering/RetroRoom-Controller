# TODO — RetroRoom-Controller

Session-scoped tracker. Resolved items live in [LOG.md](LOG.md); the items
below are open work on `session/merge-pico-json` (and any future branches off
it). Conventions: `[ ]` open, `[x]` done (move to LOG on completion and
remove from here), `// note:` free-form annotation.

## Open

- [ ] **Re-add IR blaster support via `z3t0/IRremote@^4.7.1`.** The lib is
  already in `lib_deps` on all Pico envs. The legacy crankyoldgit-based
  `src/ircontrol.{cpp,h}` was deleted on session/merge-pico-json
  commit `c6005c7`. Plan:
  1. Create `src/ircontrol.h` (declares `ir_control_init()`,
     `setInput(int hexCode)`, optionally `sendSonyPower()` / `discretePowerOn()`
     for parity with the legacy API). Include `<IRremote.hpp>` and use the
     v4 `IrSender` global instance.
  2. Create `src/ircontrol.cpp` that calls `IrSender.begin(IR_CONTROL_PIN,
     ENABLE_IR_SEND, LED_FEEDBACK_DISABLE)` in `ir_control_init()`. Implement
     `setInput(int)` as a thin SIRC wrapper -- the legacy
     `crankyoldgit::IRsend::sendSony(0xA90, 12, 2)` maps to v4
     `IrSender.sendSony(address=0x01, command=0x90, repeats=2)` (the legacy
     `0xa90` is the 12-bit value; split into the SIRC 5-bit address + 7-bit
     command; see legacy `src/ircontrol.cpp` comment block for the math).
  3. Wire `src/consoles.cpp::selectConsole()` to call `setInput(c.tvinput)`
     unconditionally (drop the `#if defined(ESP8266)` gate; ESP is gone).
  4. Drop the special-case "log the hex instead of blasting" branch in
     `selectConsole()` once IR is wired up.
  5. Smoke-test on the YD-RP2040 (GP7 = IR_CONTROL_PIN) -- connect an
     IR LED + resistor to GP7, blast at TV, confirm TV switches input.
  6. Add a `[lib]` flag or `#if defined(HAS_IR)` so the build can opt
     out of the IR layer (default ON for pico_base / picow / pico_yd).

- [ ] **Pick `MANUAL_OE_PIN` GP12 on the perfboard.** Add the `#define`
  to the RP2040 block in `src/configuration.h`:
  `#define MANUAL_OE_PIN 12` (free, away from UART0 GP0/GP1, doesn't
  conflict with anything else in the current pin map). The existing
  `#ifdef MANUAL_OE_PIN` guards in `src/state.cpp` (drives the pin
  via `digitalWrite(MANUAL_OE_PIN, !state);` and `analogWrite(MANUAL_OE_PIN, 127);`)
  and `src/main.cpp` (`pinMode`/`analogWriteFreq`) activate automatically
  once the define is added. Decide on YD-RP2040 later (probably leave
  undefined there since the YD has no manual-OE circuit).

- [ ] CYW43 (Pico-W) WiFi. `src/network.{h,cpp}` is the intended anchor.
  Currently the body is gated on `#if defined(HAS_WIFI)` and the
  `HAS_WIFI` build flag is intentionally NOT set on `[env:picow]` --
  it's a placeholder for the WiFi work. Implementation steps:
  1. Add `cyw43-driver` or `PicoW-async-webserver` to `lib_deps` for
     `[env:picow]`.
  2. Add `-D HAS_WIFI` to `[env:picow] build_flags`.
  3. Map `network.cpp::network_init()` to the CYW43 equivalent:
     start the CYW43 station mode, join the configured SSID
     (WiFiManager or hardcoded for now), bring up the AsyncWebServer
     on port 80.
  4. Move `lightRing` WebSocket handler (currently dangling because
     `network.{h,cpp}` compiles to nothing on `[env:pico_base]`).
  5. Drop the `_was_btn_held` / `EasyButton::read()` vs `update()`
     trivia that bit us on Pico -- the Pico-W board target is
     different from the bare Pico, may need different polling.

- [ ] StackSelector perfboard revision. `src/stackselector.{h,cpp}` is
  ready but the actual module-to-Pico wiring is speculative. When the
  perfboard lands, settle on real GP numbers for ARM/CYCLE/ENABLE
  instead of the placeholder `2..10` values in
  `src/configuration.h`'s RP2040 block.

- [ ] Defer: StackSelector daisy-chain length config (only matters if
  perfboard has >1 module; current `selectStack()` hard-codes to
  `consoles.size()` + 1).

- [ ] Defer: I2C OLED/encoder expansion (GP2/GP3 I2C1 and GP4/GP5 I2C0
  are free on the perfboard; add when the perfboard has an OLED).

- [ ] **5-second boot fade** -- currently `lighting_init()` clears the
  ring and waits for `lightSingle()` / `selectConsole()` to paint. Add
  a single-pixel "I'm alive" white flash at boot on the perfboard (or
  a slow color cycle on the WS2812) so the operator gets visual
  confirmation that the firmware actually got past `setup()`.
  Skip on YD-RP2040 since its onboard LED would just blink once and
  look like a glitch.
  `currentConsoleIndex` by one with wrap-around (last → first). Behaviour:
  on each release edge, increment `currentConsoleIndex`, clamp
  modulo `HowManyConsoles()`, then:
  - `Serial.print("Console Index: "); Serial.println(<index>);`
  - `lightSingle(<index>)` (existing helper; white pixel at the index)
  - `selectStack(consoles[index].selector_position)` (StackSelector ARM/CYCLE/ENABLE dance)
  - `setInput(consoles[index].tvinput)` once IR is wired back in
  - For now `setInput` is the no-op stub in `src/consoles.cpp` (TODO
    below). The serial log should show the selection so the user can
    confirm without a working TV.
  Hardware target: the existing `EasyButton touchSensor(TOUCH_SENSOR_PIN, 35, true, false)`
  in `src/controls.cpp` is already the right instance. Replace the
  `lightCycleToggle` registration with the new handler.

- [ ] Restore `setInput()` in `src/consoles.cpp::selectConsole()`. Currently
  the line is gated on `#if defined(ESP8266)` which is gone. Either:
  (a) re-add `#if defined(HAS_IR)` so the call compiles only when the
  IR layer is wired in, or (b) remove the call entirely and mark it
  TODO'd. Once `z3t0/IRremote@^4.7.1` is integrated, the call site
  becomes unconditional again. Tracked under: `src/consoles.cpp:69-71`
  and the `#include "ircontrol.h"` at `src/consoles.cpp:5`.

- [ ] Wire `consoleDefinitions()` startup logs in `src/consoles.cpp::setup()` / `loop()`.
  Today the function only logs on the failure path (`Serial.println("Console config load failed: ...")`).
  Add a success-path log: `Serial.print("Loaded N consoles:"); for (auto& c : consoles) Serial.print(" ...")`.
  Also: when a touch button-press advances the selection, log the
  new index AND the console's `name`, `selector_position`, and
  `tvinput` hex code so the operator can verify wiring on the bench.

- [ ] Drop legacy framework files / lib_deps that are no longer needed.
  Confirm the smoke build runs cleanly without:
  - `crankyoldgit/IRremoteESP8266` (already removed in platformio.ini)
  - `ottowinter/ESPAsyncWebServer-esphome` and `alanswx/ESPAsyncWiFiManager`
    (already removed but the docs/comments still reference them in
    `platformio.ini`, `LOG.md`, etc.; sweep these to say "removed on
    session/merge-pico-json")
  - `[env:nodemcuv2]` block in `platformio.ini` (already removed)
  - the `ArduinoJson` lib_dep entries if not strictly required by the
    host-side `[env:test_native]`
  - `src/Console.cpp` (the entire file is `#include "Console.h"` only --
    left as a stub for backward compat; can be deleted if no one imports it)

- [ ] Re-add IR blaster support via `z3t0/IRremote@^4.7.1` (already in
  `lib_deps`). The current `ircontrol.{cpp,h}` files were deleted in
  this session; they need to be re-created using the v4 API
  (`IRsend` with raw pulse-distance timing -- the legacy
  `crankyoldgit/IRremoteESP8266::sendSony(0xA90, 12, 2)` call maps to
  `IrSender.sendSony(0xA90, 12, 2)` or to the v4 raw-pulse API).
  Wire on the YD-RP2040 first (GP7 = IR_CONTROL_PIN per
  `src/configuration.h`), then move to the perfboard.

- [ ] CYW43 (Pico-W) WiFi. `src/network.{h,cpp}` is the intended anchor.
  Currently the body is gated on `#if defined(HAS_WIFI)` and the
  `HAS_WIFI` build flag is intentionally NOT set on `[env:picow]` --
  it's a placeholder for the WiFi work. Implementation steps:
  1. Add `cyw43-driver` or `PicoW-async-webserver` to `lib_deps` for
     `[env:picow]`.
  2. Add `-D HAS_WIFI` to `[env:picow] build_flags`.
  3. Map `network.cpp::network_init()` to the CYW43 equivalent:
     start the CYW43 station mode, join the configured SSID
     (WiFiManager or hardcoded for now), bring up the AsyncWebServer
     on port 80.
  4. Move `lightRing` WebSocket handler (currently dangling because
     `network.{h,cpp}` compiles to nothing on `[env:pico_base]`).
  5. Drop the `_was_btn_held` / `EasyButton::read()` vs `update()`
     trivia that bit us on Pico -- the Pico-W board target is
     different from the bare Pico, may need different polling.

- [ ] StackSelector perfboard revision. `src/stackselector.{h,cpp}` is
  ready but the actual module-to-Pico wiring is speculative. When the
  perfboard lands, settle on real GP numbers for ARM/CYCLE/ENABLE
  instead of the placeholder `2..10` values in
  `src/configuration.h`'s RP2040 block.

## Done (moved to LOG.md, `session/merge-pico-json`)

- [x] **Map the YD-RP2040 USR button to advance the console index** (commit
  `c6005c7`). New free function
  [`src/consoles.cpp::advanceConsole()`](src/consoles.cpp) increments
  `currentConsoleIndex` modulo `HowManyConsoles()`, paints one bright
  pixel via `lightSingle()`, calls `selectConsole()`. Bound as
  `touchSensor.onPressed(advanceConsole)` in
  [`src/controls.cpp::controls_init()`](src/controls.cpp). On the
  perfboard the same pin will be the capacitive touch input and the
  same handler fires.

- [x] **Pinout doc (pico-pin-mapping.md)** with clear Pin → role tables for
  the perfboard target (Raspberry Pi Pico, GP2-GP10) and the YD-RP2040
  dev board (GP23 onboard WS2812, GP24 USR button, rest matches Pico).
  Sources-of-truth anchor section at the bottom pointing readers back
  to the actual `#define`s in `src/configuration.h`. Also drops the
  legacy ESP8266 / NodeMCU v2 pin tables (those boards are no longer
  supported as of session/merge-pico-json) and adds wiring notes
  (470 Ω series resistor on DATA_PIN, 10 kΩ external pull-up on
  TOUCH_SENSOR_PIN, MANUAL_OE_PIN deliberately undefined).

## Notes

- This file is the single source of truth for outstanding work on the
  Pico port. When in doubt, check here before starting a new task.
- Don't add items here without an explicit user request -- the goal is
  to keep this list lean and reflective of in-flight work.
