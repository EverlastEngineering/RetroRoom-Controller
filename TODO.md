# TODO — RetroRoom-Controller

Session-scoped tracker. Resolved items live in [LOG.md](LOG.md); the items
below are open work on `session/merge-pico-json` (and any future branches off
it). Conventions: `[ ]` open, `[x]` done (move to LOG on completion and
remove from here), `// note:` free-form annotation.

## Open

- [ ] **CYW43 (Pico-W) WiFi.** `src/network.{h,cpp}` is the intended
  anchor. Currently the body is gated on `#if defined(HAS_WIFI)` and
  the `HAS_WIFI` build flag is intentionally NOT set on `[env:picow]`
  -- it's a placeholder for the WiFi work. Implementation steps:
  1. Add `cyw43-driver` (or similar) to `lib_deps` for `[env:picow]`.
  2. Add `-D HAS_WIFI` to `[env:picow] build_flags`.
  3. Map `network.cpp::network_init()` to the CYW43 equivalent:
     start the CYW43 station mode, join the configured SSID
     (WiFiManager or hardcoded for now), bring up the AsyncWebServer
     on port 80.
  4. Move `lightRing` WebSocket handler (currently dangling because
     `network.{h,cpp}` compiles to nothing on `[env:pico_base]`).

- [ ] **StackSelector perfboard revision.** `src/stackselector.{h,cpp}`
  is ready but the actual module-to-Pico wiring is speculative for
  the production perfboard. Currently uses GP8=ARM, GP9=CYCLE, GP10=ENABLE
  (defined in `src/configuration.h` as placeholder values; see
  `pico-pin-mapping.md` Section 5). When the perfboard lands, confirm
  the wiring or assign real GP numbers.

- [ ] **5-second boot fade.** Currently `lighting_init()` clears the
  ring (CRGB::Black) and waits for `lightSingle()` / `selectConsole()`
  to paint. Add a single-pixel "I'm alive" white flash at boot on the
  perfboard (or a slow color cycle on the WS2812) so the operator gets
  visual confirmation that the firmware got past `setup()`. Skip on
  YD-RP2040 since its onboard LED would just blink once and look like
  a glitch. Tracked in `src/lighting.cpp` `lighting_init()`.

## Deferred (waiting on the perfboard)

The following items only matter once a real perfboard revision lands.
Don't pick these up speculatively; wait for the hardware.

- [ ] **StackSelector daisy-chain length config.** Today
  `selectStack()` hard-codes to `consoles.size() + 1` for the home
  position and loops `consoles.size()` times. If the perfboard ever
  has more than one daisy-chained StackSelector module, this needs
  to know the chain length (e.g. a `STACK_MODULES` config constant).

- [ ] **I2C OLED / encoder expansion.** GP2/GP3 (I2C1) and GP4/GP5 (I2C0)
  are free on the perfboard header. An SSD1306 OLED would integrate
  cleanly with the current code; defer until the perfboard actually
  has an OLED.

- [ ] **External 10 kΩ pull-up on TOUCH_SENSOR_PIN (GP5).** The RP2040
  internal pull-up is too weak for reliable capacitive sensing. The
  perfboard schematic must include this. Soft reminder for whoever
  draws the PCB.

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
  to the actual `#define`s in `src/configuration.h`. Wired MANUAL_OE_PIN
  to GP12 in commit `643be7c` (see Section 2 of the doc); ESP8266
  tables removed since those boards are no longer supported.

- [x] **IR blaster restored via `z3t0/IRremote@^4.7.1`** (commit `643be7c`).
  `src/ircontrol.{h,cpp}` (94 + 42 lines) wrap the v4 `IRsend IrSender`
  global. `setInput(int hex)` decodes a 12-bit Sony SIRC value into
  (address, command) per the legacy mapping (`address = (v >> 7) & 0x1F`,
  `command = v & 0x7F`) and calls `IrSender.sendSony(addr, cmd, 2)`. The
  `<IRremote.hpp>` include is restricted to `src/ircontrol.cpp` to
  avoid linker multiple-definition errors (the library has non-inline
  globals — `IRrecv::decode`, the timer helpers, the feedback LED
  state — that get duplicated if multiple TUs pull in the header).
  `<HAS_IR>` build flag added to all three Pico envs; on a hypothetical
  off-build, `src/ircontrol.cpp` falls back to no-op stubs. `selectConsole()`
  calls `setInput(c.tvinput)` after `selectStack()`, gated on
  `#if defined(HAS_IR)`. The legacy "log-the-hex-instead-of-blasting"
  placeholder branch is gone.

- [x] **`MANUAL_OE_PIN = 12` on the perfboard** (commit `643be7c`). Added
  `#define MANUAL_OE_PIN 12` to the RP2040 pin block in
  `src/configuration.h`. The existing `#ifdef MANUAL_OE_PIN` guards in
  `src/state.cpp` (`digitalWrite(MANUAL_OE_PIN, !state)` + `analogWrite(MANUAL_OE_PIN, 127)`)
  and `src/main.cpp` (`pinMode(MANUAL_OE_PIN, OUTPUT)` + `analogWriteFreq(40000)`)
  activate automatically once the `#define` is set. YD pin block
  leaves it undefined since the YD has no MOSFET circuit.

## Notes

- This file is the single source of truth for outstanding work on the
  Pico port. When in doubt, check here before starting a new task.
- Don't add items here without an explicit user request -- the goal is
  to keep this list lean and reflective of in-flight work.
