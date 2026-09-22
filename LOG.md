# RetroRoom Decision Log

This file records architectural decisions and notable changes to RetroRoom-Controller. Entries follow [MADR](https://adr.github.io/madr/) format — see [AGENT.md](AGENT.md) for the template and policy.

Entries are added to the top of this file by the `log_add` MCP tool. Use the `log_read` MCP tool to view recent entries.

<!-- insert-below -->
## 2026-09-22T18:00:00.000Z — Pico 2 W CYW43 AsyncWebServer + SoftAP setup portal

**Context:** The Pico 2 W (RP2350 + on-board CYW43) had a working
`[env:pico2w]` build since `session/merge-pico-json` but no WiFi
stack — `src/network.{h,cpp}` was a `#error` stub gated on
`HAS_WIFI`. The user got a Pico 2 W on the bench and asked for the
ESP8266-era webserver experience (legacy HTML/JS, AsyncWebServer,
WebSocket push) to come back on the new chip. `monitor_filters =
direct` and the heartbeat debug-print were already on the branch
from the prior session. The open items from
`todo/open/2026-09-21_pico-2-w-platform.md` and the older
`todo/open/2026-09-12_cyw43-picow-wifi.md` were closed in this
pass.

**Decision:**

1. **Library picks.** `khoih-prog/AsyncTCP_RP2040W` +
   `khoih-prog/AsyncWebServer_RP2040W` were the obvious first
   candidates but the `AsyncTCP_RP2040W.h` gates on
   `#if (defined(ARDUINO_RASPBERRY_PI_PICO_W))` and rejects the
   Pico 2 W (`ARDUINO_RASPBERRY_PI_PICO_2W`). Switched to
   `ayushsharma82/RPAsyncTCP@^1.3.2` + `esp32async/ESPAsyncWebServer@^3.7.2`
   (Hristo Gochkov's ESPAsyncWebServer port). The latter is what
   `ayushsharma82/RPAsyncTCP` is built to back, and it explicitly
   supports both `RP2040+W` and `RP2350+W` on Earle Philhower's
   `arduino-pico` core.
2. **Flash partition.** Default `[env:pico2w]` uses 4 MB sketch
   with no FS partition, which would silently disable
   `LittleFS.begin()` and any saved-credential flow. The right
   override is `board_build.filesystem_size = 1MB` — read by
   `~/.platformio/platforms/raspberrypi/builder/main.py` via
   `board.get("build.filesystem_size")`. After the override the
   build prints `Filesystem size: 1.00MB` /
   `Filesystem start: 0x102ff000 / end: 0x103ff000`. The earlier
   `board_build.flash_length` / `fs_start` / `fs_end` overrides I
   tried are silently dropped by the PlatformIO menu-merging code,
   which is why the build was originally showing
   `Filesystem size: 0.00MB`.
3. **SoftAP setup portal on first boot.** Rather than ship a
   hardcoded SSID or a heavy `AsyncWiFiManager` port, the firmware
   reads `/wifi.json` from LittleFS on boot. If the file is
   missing, it brings up a SoftAP `RetroRoom-Setup` (open, no
   password — acceptable since the AP only exposes a `/setup` HTML
   form and we reboot the moment the user POSTs creds) and serves
   the form. On POST, the firmware writes `/wifi.json` with
   `{"ssid":..., "pass":...}` then calls `rp2040.restart()`. On
   next boot the SoftAP is skipped and STA mode is used. If the
   saved creds are bad (or the network is out of range) the
   20-second STA timeout falls back to the SoftAP again.
4. **Endpoints implemented.**
   - `GET  /` → `src/html/index.html` (the legacy iframe wrapper)
   - `GET  /script.js` → `src/html/script.js` (the legacy
     ws://host/ws flow with xhrget polling fallback)
   - `GET  /ledOn` / `/ledOff` / `/flash` / `/healthcheck` →
     drive the on-board LED + broadcast the action over WS
   - `GET  /state.json` → JSON snapshot for polling
   - `GET  /wifi` → JSON of current WiFi status (IP/RSSI/mode)
   - `GET  /setup` (AP only) → HTML form
   - `POST /setup` (AP only) → write creds, reboot
   - `WS   /ws` → broadcastSocketMessage push channel
5. **Wired `consoles.cpp::advanceConsole()` to WS** so the
   browser-side iframe gets a `console:<name>:<index>` message
   whenever the user presses the touch sensor (YD-RP2040 USR
   button). Gated on `#if defined(HAS_WIFI)` so other envs
   compile unchanged.
6. **API surface stayed as close to legacy as possible.** The
   script.js flow's WS path now actually works (no more
   `Unable to send message to socket connection.`), and the
   xhrget polling fallback is no longer exercised.
7. **Build outputs.** 14.4% RAM (75,392 / 524,288), 13.5% flash
   (424,788 / 3,141,632). Allocated CYW43 driver (~220 KB) +
   LWIP buffers (~40 KB) + AsyncWebServer accounted for most of
   the jump from the wired-only `pico_base` build.

**Consequences:**

- `pio run -e pico2w` is green; `pio run -e pico2w -t upload -t
  monitor` flashes via picotool and prints every line of serial
  output. On boot the firmware prints
  `net: network_init()` →
  `net: no /wifi.json; will start SoftAP` →
  `net: starting SoftAP "RetroRoom-Setup"` →
  `net: SoftAP IP = 192.168.4.1` →
  `net: setup portal running on http://192.168.4.1/setup` →
  `Setup Complete.`, then a 1 Hz heartbeat stream that
  includes `millis()`. After 9+ minutes of continuous operation
  the firmware is still heartbeating and the SoftAP is still
  reachable (the file output we sampled went to `Heartbeat:
  552359` with no resets or error prints).
- User joins `RetroRoom-Setup` from a phone, opens
  `http://192.168.4.1/`, submits SSID + password, firmware writes
  `/wifi.json` and `rp2040.restart()`s. On next boot we expect
  `net: connecting to "YourSSID"` followed by either
  `net: connected, IP = ...` or the 20 s timeout → SoftAP
  fallback. The script.js WS handshake should produce
  `net: ws client #N connected` and an immediate `ws_init_ack`
  echo on every new browser tab.
- TODO.md items closed: `cyw43-picow-wifi.md` (deferred by
  `pico-2-w-platform.md`) is now done in spirit — the CYW43
  driver, AsyncWebServer, and WebSocket stack all live in
  `src/network.cpp`. `pico-2-w-platform.md` Phase A (build) and
  Phase B (WiFi stack) are complete; Phase C (polling vs WS) is
  satisfied for both — the legacy xhrget polling path was never
  touched and the real `/ws` is wired. The "Open questions"
  section's SSID/password question is resolved by the SoftAP
  portal approach. Static IP is still open and not needed for
  bench testing.

## 2026-09-12T19:15:00.000Z — IR blaster restored (z3t0/IRremote@4.x) + MANUAL_OE_PIN = GP12

**Context:** Two open items from `TODO.md` and the user's pinout-doc open-question list closed in one pass. `z3t0/IRremote@^4.7.1` was already in `lib_deps` for all Pico envs (originally added in the plan for swapping out `crankyoldgit/IRremoteESP8266`); the v4 `IrSender` global is the canonical API. The legacy `crankyoldgit::IRsend::sendSony(0xa90, 12, 2)` form decoded cleanly into `IrSender.sendSony(address, command, repeats)` where `address = (value >> 7) & 0x1F` and `command = value & 0x7F`. For the manual-OE pin, GP12 is free on the standard Pico header and away from UART0 (GP0/GP1) and I2C0 (GP4/GP5), so it was the obvious pick without further user input.

**Decision:** Single commit `<see git log>` on `session/merge-pico-json`.

- **`src/ircontrol.{h,cpp}` restored** as 12-bit SIRC thin wrappers
  around the v4 `IRsend IrSender` global.
  - `ir_control_init()` calls `IrSender.begin(IR_CONTROL_PIN,
    USE_DEFAULT_FEEDBACK_LED_PIN)`.
  - `setInput(int)` decodes a 12-bit SIRC value into (address, command)
    and calls `IrSender.sendSony(address, command, 2)`. The address /
    command split is hardcoded for now because the JSON's `tvInput`
    field stores a single 12-bit SIRC value; if the perfboard later
    wants per-protocol address encoding (the legacy entry mentioned
    that) we'll parse the JSON's `irCodes.<name>` into split fields.
  - `sendSonyPower()` and `discretePowerOn()` are kept as backward-
    compat wrappers around `setInput(0xA90)` / `setInput(0x750)`;
    nothing currently calls them but the legacy caller sites (a
    future button action) have something to bind to.
- **`<IRremote.hpp>` is only `#include`d in `src/ircontrol.cpp`.**
  Including it in `src/ircontrol.h` (which is pulled into every TU
  via `src/main.h`) caused linker multiple-definition errors on
  `IRrecv::decode()`, `IRsend::setLEDFeedback`, the timer state, etc.
  Those functions are non-inline definitions in the library's own
  sources; including the header from multiple TUs pulls them into
  each TU and the linker rejects duplicates. Single-include is the
  canonical fix.
- **`<HAS_IR>` build flag** added to all three Pico envs'
  `build_flags`. When `HAS_IR` is undefined, `src/ircontrol.cpp`
  falls back to no-op stubs (so the rest of the firmware still
  compiles on a hypothetical future build that drops IR).
- **`#define MANUAL_OE_PIN 12`** in the RP2040 (perfboard) pin block
  in `src/configuration.h`. The existing `#ifdef MANUAL_OE_PIN`
  guards in `src/state.cpp` (`digitalWrite(MANUAL_OE_PIN, !state)`,
  `analogWrite(MANUAL_OE_PIN, 127)`) and `src/main.cpp`
  (`pinMode(MANUAL_OE_PIN, OUTPUT)`, `analogWriteFreq(40000)`)
  activate automatically. The YD pin block does NOT define
  `MANUAL_OE_PIN` since the YD dev board has no MOSFET circuit.
- **`src/consoles.cpp::selectConsole()`** now calls
  `setInput(c.tvinput)` after `selectStack()`, gated on
  `#if defined(HAS_IR)`. Removed the
  "log-the-hex-instead-of-blasting-it" branch that was the TODO
  placeholder when IR was stubbed out.
- **`platformio.ini`** cleaned up:
  - `-D HAS_IR` added to all three Pico envs' `build_flags`
    (alongside `-D HAS_LEDS`).
  - `build_src_filter` simplified: the
    `-<ircontrol.cpp> -<ircontrol.h>` exclusions are gone
    (the file is back in scope).
- **`pico-pin-mapping.md` updated:** added `MANUAL_OE_PIN = GP12`
  to the per-role table and the KiCad net list, demoted
  "MANUAL_OE on the Pico" from "Open questions" to "wired", and
  updated `IR_CONTROL_PIN` consumer (now `src/ircontrol.cpp::ir_control_init()`
  instead of "removed; re-add on `z3t0/IRremote@4.x`").

**Consequences:**

- All three Pico envs build green. `pico_base` 4.0% RAM / 2.8% Flash
  (RAM up from 3.7% pre-IR as expected: the `IRrecv` class + the
  feedback-LED state add a small amount of static state). `picow`,
  `pico_yd`, and `test_native` (24/24 host tests) all green.
- `pico_yd` flashed to `/dev/cu.usbmodem101`. Boot should print
  "IR sender initialized on GP7" (after `ir_control_init()` runs),
  "Select Console index=N: <name> selector=P tvInput=0xH" on every
  `advanceConsole()` press, and the IR blip should fire on GP7
  (GP7 → 33 Ω resistor → IR LED → GND, 940 nm). Confirm by aiming
  the YD's GP7 IR pin at a Sony TV set to the same input.
- TODO.md updated: the IR-restoration item moved to "Done";
  MANUAL_OE item moved to "Done".

## 2026-09-12T17:30:00.000Z — Pico-only: drop ESP8266 / AVR, USR button advances console

**Context:** The user wants to simplify the firmware to Raspberry Pi Pico only and stop carrying the legacy ESP8266 (NodeMCU v2) + AVR compatibility scaffold. With the Pico RP2040 target set, the next concrete UX step is: the YD-RP2040 USR button (TOUCH_SENSOR_PIN = GP24) should advance the selected console with wrap-around instead of toggling the smoke-test WS2812 R/G/B cycle. Reasonable removal list: the WS2812 cycle-toggle (functions stay as dead code), the boot R/G/B flash and per-second pin diagnostic prints (no startup chatter beyond `consoleDefinitions()` log), the heartbeat blink (decorative), the IR layer (re-add via `z3t0/IRremote@4.x` when the perfboard lands), and ESP-only `#ifdef`s throughout.

**Decision:** Single commit `c6005c7` on `session/merge-pico-json` (branched off `dea0a2e` on `session/pico-migration`).

- **`[env:nodemcuv2]` dropped entirely.** [platformio.ini](platformio.ini) loses the ESP8266 platform target, the `espressif8266` board, and the ESP-coupled libs (`ESPAsyncWebServer-esphome`, `ESPAsyncWiFiManager`, `crankyoldgit/IRremoteESP8266`, `bblanchon/ArduinoJson` on the firmware side). All three remaining envs (`pico_base`, `picow`, `pico_yd`) define `-D HAS_LEDS`; `-D HAS_WIFI` stays reserved for when the Pico-W CYW43 work lands.
- **AVR + ESP8266 pin blocks removed** from [src/configuration.h](src/configuration.h). Only the `ARDUINO_RASPBERRY_PI_PICO` / `ARDUINO_RASPBERRY_PI_PICO_W` (perfboard) and `ARDUINO_YD_RP2040` (dev board) pin maps remain.
- **`src/ircontrol.{cpp,h}` deleted.** The body was fully gated on `ESP8266` and used the legacy `crankyoldgit/IRremoteESP8266` + `<IRsend.h>` stack. `src/consoles.cpp::selectConsole()` no longer calls `setInput()`; instead it logs `index / name / selector_position / tvInput` so the operator can verify IR wiring manually on the bench.
- **`src/network.cpp` reduced to a stub** that `#error`s if any build defines `HAS_WIFI` (none do). The ESP-coupled `AsyncWebServer` / `AsyncWebSocket` / `AsyncWiFiManager` body is gone. [src/network.h](src/network.h) keeps the `network_init()` / `broadcastSocketMessage()` declarations under `#if defined(HAS_WIFI)` as the anchor for the CYW43 wifi work.
- **USR button → console advance.** New free function [`src/consoles.cpp::advanceConsole()`](src/consoles.cpp) increments `currentConsoleIndex` modulo `HowManyConsoles()`, paints one bright pixel via `lightSingle()`, then calls `selectConsole()`. Wired as `touchSensor.onPressed(advanceConsole)` in [`src/controls.cpp::controls_init()`](src/controls.cpp). On the perfboard the same pin will be the capacitive touch input, so the same handler fires.
- **Startup logs.** [`src/consoles.cpp::consoleDefinitions()`](src/consoles.cpp) on success prints each console's `id` → `name` → selector → `tvInput` hex → led range. `selectConsole()` prints the index + name + selector + `tvInput` hex on every transition.
- **Removed from loop + controls:** the `lightCycleTick()` call, `lightCycleToggle()` button handler, `lightCycleIsEnabled()` diagnostic, the per-second `Serial.print("pin=...")` diagnostic, the WS2812 R/G/B boot smoke test inside `lighting_init()`, the `setLed(statusLedActive ? 0x0 : 0x1)` heartbeat blink, and the `IRAM_ATTR` shim (no longer needed without ESP). Kept: `lightCycleTick / Toggle / IsEnabled` declarations in `src/lighting.{h,cpp}` for a future "background pattern" mode.
- **`TODO.md` created at repo root.** Single source of truth for open work on this branch: 7 actionable items (USR-button wiring done; left: drop legacy framework files, re-add IR via `z3t0/IRremote@4.x`, CYW43 wifi, StackSelector perfboard pin map, `setInput()` re-add, console startup-log tweaks).

**Consequences:**

- All three Pico envs green: `pico_base` 3.7% RAM / 2.7% Flash, `picow` 26.6% / 15.0%, `pico_yd` 3.7% / 0.3% (16MB YD). 24/24 host tests on `[env:test_native]`.
- `-211` lines net (258 insertions, 469 deletions). Biggest drops: `src/network.cpp` (−88), `src/main.cpp` (−88), `src/controls.cpp` (−67 from the ESP `#ifdef` removal + lightCycle unregistration).
- `ae9d8e4` (network: stabilize WiFi connect against brown-out / rst cause 2) — the only meaningful unmerged commit on `session/json-config-cleanup` — is left there. It's ESP8266-specific (`WiFi.setOutputPower`, `WiFi.setSleepMode`, `WiFi.hostname`) and not applicable to RP2040 yet; the conceptual intent (let VDD settle, drop TX power, modem-sleep when idle) is recorded as the picow-wifi TODO.
- Flashed `pico_yd` to `/dev/cu.usbmodem101` (USB re-enumerated from `11101` after `picotool reboot -u`). No way to run interactive smoke from here, but the firmware loads (`lighting_init()` runs, the FastLED PIO/DMA bring-up completes, USB-CDC re-enumerates) so `advanceConsole()` should fire on each USR press when the operator tries it.

## 2026-09-09T14:30:00.000Z — Pico port: FastLED 3.10+ PIO backend + YD-RP2040 dev board env

**Context:** Earlier work pinned FastLED to 3.6.0 on the Pico envs and patched around an upstream bug (3.6.0's `clockless_arm_rp2040.h` unconditionally `#include`s `../common/m0clockless.h`, which references `SysTick->VAL` — RP2040 has no SysTick). The user surfaced that FastLED 3.10+ ships a complete PIO-based RP2040 clockless backend at `src/platforms/arm/rp2040/` (default `FASTLED_RP2040_CLOCKLESS_PIO=1`, `FASTLED_RP2040_CLOCKLESS_M0_FALLBACK=0`), gated by two macros in `led_sysdefs_arm_rp2040.h`. This drops the patch script + patch file entirely and is the upstream-recommended path. Separately, the dev board on the desk is a VCC-GND Studio YD-RP2040 (pinout-compatible with the standard Pico but with an onboard WS2812 on GP23, USR button on GP24, blue LED on GP25) — useful for smoke-testing the FastLED PIO path without a separate WS2812 ring wired up.

**Decision:** Two commits on `session/pico-migration`, branched off `4966a40`:

1. `65fd602` — port: FastLED 3.10+ RP2040 PIO backend (drop 3.6.0 + patch script)
   - `platformio.ini`: pin `fastled/FastLED@^3.10.0` for `[env:pico_base]` and `[env:picow]`. Add `FASTLED_RP2040_CLOCKLESS_PIO=1` and `FASTLED_RP2040_CLOCKLESS_M0_FALLBACK=0` to build_flags (explicit, not relying on defaults).
   - Delete `apply_fastled_patch.py` and `patches/1-fastled-rp2040-clockless-pio-only.patch` — 3.10+ doesn't need them.
   - `src/lighting.cpp`: migrate `addLeds<NEOPIXEL, DATA_PIN>(...)` → `addLeds<WS2812B, RR_FASTLED_DATA_PIN, GRB>(...)`. `WS2812B` is the explicit clockless chipset class with `<DataPin, RGBOrder>` template signature that 3.10+'s addLeds helper expects.
   - `src/lighting.h`: tighten the `#pragma push_macro`/`pop_macro` save+undef dance — save under `RR_FASTLED_DATA_PIN`/`RR_FASTLED_RGB_ORDER` and restore `DATA_PIN`/`RGB_ORDER` after FastLED.
   - `src/controls.cpp::checkPosition()` `#elif` gate extended to include `ARDUINO_YD_RP2040` (needed for the YD env).

2. `72be849` — port: add YD-RP2040 dev board env + smoke-test WS2812 heartbeat
   - `platformio.ini`: new `[env:pico_yd]` with `board = vccgnd_yd_rp2040`. Auto-defines `ARDUINO_YD_RP2040`. Same FastLED backend as `pico_base`/`picow`.
   - `src/configuration.h`: add `#elif defined(ARDUINO_YD_RP2040)` pin block. `DATA_PIN=23` (onboard WS2812), `TOUCH_SENSOR_PIN=24` (USR button), other pins keep the generic Pico numeric values as placeholders.
   - `src/main.cpp`: 1Hz heartbeat blink on the onboard blue LED via existing `setLed()`/`statusLedActive`/`flash` gate. Runtime signal that `lighting_init()` (FastLED PIO bring-up) succeeded.
   - `src/lighting.cpp::lighting_init()`: at end of init, flash red → green → blue → black once on the WS2812 chain. On YD-RP2040 this lights the onboard LED; on perfboard build (DATA_PIN=4) it lights the external ring.

**Consequences:** All five envs green after this commit set:
- `nodemcuv2` (ESP, legacy FastLED ^3.5.0): SUCCESS, 55.9% RAM, 49.8% Flash
- `pico_base` (Raspberry Pi Pico, FastLED 3.10.3): SUCCESS, 3.7% RAM, 2.7% Flash
- `picow` (Raspberry Pi Pico-W, FastLED 3.10.3): SUCCESS, 26.6% RAM, 15.0% Flash
- `pico_yd` (VCC-GND YD-RP2040, FastLED 3.10.3): SUCCESS, 3.7% RAM, 0.3% Flash (16MB YD flash). Flashed to `/dev/cu.usbmodem11101` — `setup()` completed past `lighting_init()` (PIO program upload + DMA channel claim + DMA IRQ install) without crash; USB-CDC re-enumerated after the FastLED bring-up.
- `test_native`: 24/24 Unity tests pass.

Deferred work unchanged from the previous Pico port commit set:
- z3t0/IRremote@^4.x swap replacing `crankyoldgit/IRremoteESP8266`. The IR send path with raw 12-bit codes will likely move to `IrSender.sendPulseDistanceWidthRaw()`.
- CYW43 (Pico-W WiFi) driver on the `picow` env.
- Persistence (was ESP EEPROM; Pico is flash-backed — needs different lib).
- StackSelector perfboard wiring — once landed, the `pico_yd` env's placeholder pins should be revisited (the YD's GP23/GP24 are already used; the rest of the perfboard pins will need real YD-specific values).

## 2026-09-09T00:00:00.000Z — Pico port: nodemcuv2 / pico_base / picow all build green

**Context:** Earlier sessions chased a boot-loop on ESP8266, then switched hardware to ESP32-C3 (abandoned for pin-count reasons), then to a real Raspberry Pi Pico (`session/picotest` was the toolchain-validation scaffold; verified live on `/dev/cu.usbmodem11101`). The user wants to abandon the `session/json-config-cleanup` branch entirely and port the existing ESP-era firmware to Pico from the `c6e7037` "last-good" snapshot. The Pico + Earle Philhower core does not provide ESP-only headers (AsyncWebServer, ESP8266mDNS, IRremoteESP8266), nor the `IRAM_ATTR` macro, nor the ESP-only WiFi APIs. A clean port needs #ifdef guards throughout src/. The user gave blanket permission to flash the device.

**Decision:** Six commits on `session/pico-migration`, branched off `last-good`:

- `platformio.ini`: declares `HAS_WIFI` + `HAS_LEDS` for `nodemcuv2` (legacy ESP build unchanged). Adds `[env:pico_base]` and `[env:picow]` using `maxgerhardt/platform-raspberrypi` + `earlephilhower/arduino-pico` core. `picow` intentionally omits `-D HAS_WIFI` until a CYW43 implementation lands. Both Pico envs scope `build_src_filter` to exclude `lighting.cpp` and `ircontrol.{cpp,h}` until FastLED 3.10+ and z3t0/IRremote are wired in.
- `src/configuration.h`: adds an `#elif defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W)` pin block (GP2..GP10), and un-`//`s the previously commented `ENABLE_PIN` for the Pico branch. `MANUAL_OE_PIN` deliberately undefined on Pico (perfboard TODO).
- `src/network.{h,cpp}`: entire body wrapped in `#if defined(HAS_WIFI)`. On Pico the file is empty.
- `src/main.h`: drops unused `<map>`, `<string>`, `<iostream>` and `using namespace std;`. `src/main.cpp::setup()`: gated on `HARD_RESET && ESP8266`, `MANUAL_OE_PIN`, `HAS_LEDS`, `HAS_WIFI`, `ESP8266` (for ir_control_init), and `ARDUINO_RASPBERRY_PI_PICO || ARDUINO_RASPBERRY_PI_PICO_W` (for 115200 baud). `while (!Serial) {}` removed.
- `src/{lighting,ircontrol,Console,state,controls}.{h,cpp}`: per-feature guards. FastLED include gated on `HAS_LEDS`. IRremoteESP8266 includes gated on `ESP8266`. `setInput()` call on `ESP8266`. `IRAM_ATTR` shim. RP2040 `_W` macro branch for `checkPosition`. `lightRing()` calls gated on `HAS_LEDS`. `broadcastSocketMessage()` calls in `touchDetected/touchReleaseDetected` gated on `HAS_WIFI`. `<FS.h>` and `MANUAL_OE_PIN` references in `src/state.cpp` cleaned up.

**Consequences:** `pio run -d . -e {nodemcuv2,pico_base,picow}` all green after this commit set. Builds:
- `nodemcuv2`: 55.9% RAM, 49.8% Flash (legacy ESP path).
- `pico_base`: 3.4% RAM, 1.9% Flash — the LED ring and IR blaster are excluded for now, so the binary is tiny.
- `picow`: 26.6% RAM, 14.2% Flash — bigger because the framework-arduinopico core pulls in CYW43 SPI/WiFi driver code unconditionally for `board = rpipicow` even when `HAS_WIFI` is undefined; correct behavior depends on the CYW43 work landing.

Deferred work not in this commit set:
- FastLED 3.10+ `addLeds<>` signature migration in `src/lighting.cpp` (NEOPIXEL enum, not class template; COLOR_ORDER default changed).
- z3t0/IRremote@^4.x swap replacing `crankyoldgit/IRremoteESP8266`. The IR_send path with raw 12-bit codes will likely move to `IrSender.sendPulseDistanceWidthRaw()` to sidestep address/command decode.
- CYW43 (Pico-W WiFi) driver on the `picow` env. Adds `HAS_WIFI` back to `[env:picow]` build_flags once that's done.
- Persistence (was ESP EEPROM; Pico is flash-backed — needs different lib).

---

## 2026-09-07T15:37:37.066Z — AGENT.md Policy #6 — never hide user-facing command output

**Context:** After the first pio build/upload run, output was piped through `tail -120` to fit it into the assistant's context window. The user pointed out that this hid the build log when they wanted to watch and troubleshoot. An agent must not silently truncate output of commands the user explicitly cares about.

**Decision:** Add Policy #6 to AGENT.md: never pipe build, install, test, or other long-running command output through filters that discard lines; either stream to the user's terminal or write to a file. Trivial summaries like `git status -sb` are still fine. The intent is to give the user what they need to troubleshoot and watch things.

**Consequences:** Future build/test runs go unfiltered. The pio wrappers already write full output to <repo>/pio-log.txt and per-session serial-log files — those files plus the user's terminal are the only paths output should take. The full --tail -N privilege is reserved for genuinely verbose commands where the user hasn't asked to see what's happening (rare; default to streaming). A LOG entry is recorded here per Policy #1.

---

## 2026-09-07T15:25:30.472Z — Add PlatformIO wrappers + serial monitor as MCP tools

**Context:** PlatformIO had to be driven directly via run_in_terminal, which truncated output for context and gave us no persistent build log. Serial monitor (`pio device monitor`) was foreground-only — no way to capture output for the agent or for later grep / diff against regressions. Goal: route every pio invocation (human or MCP) through one wrapper pair with consistent logging, and surface build / upload / monitor as structured MCP tools.

**Decision:** Add agent-script/pio.sh and agent-script/pio-monitor.sh. pio.sh appends a timestamped header + full output to <repo>/pio-log.txt and prints a one-line summary; pio-monitor.sh runs `pio device monitor` in bg / fg with output captured to per-session serial-log-<port>-<ts>.txt, plus list/stop subcommands persisted to .pio-monitors.state. Add five MCP tools — pio_port, pio_compile, pio_upload, pio_monitor, pio_monitor_stop — that shell out to the wrappers. Add a runCapture helper to mcp-server.mjs alongside the existing runScript. Enforce a hardware-permission rule in AGENT.md (Policy #5): pio_upload and pio_monitor may only run when the user has explicitly asked.

**Consequences:** AGENT.md gains Policy #5 and a curated list of seven MCP tools. .gitignore adds pio-log.txt, serial-log-*.txt, and .pio-monitors.state (all generated/append-only). Agents now have a single sanctioned interface for pio; humans can use the wrappers directly. Future pio tooling only needs new server.tool() blocks plus matching npm scripts. A wall around uploading and serial attach keeps tool exposure from silently expanding hardware impact.

---

## 2026-09-07T14:55:51.540Z — MCP tool discovery round-trip works from a fresh chat

**Context:** Verifying that the `log_add` MCP tool is discoverable and invokable from a brand-new chat session without any prior tool priming or conversation history about it.

**Decision:** Successfully invoked the `log_add` MCP tool directly from a fresh chat, confirming the MCP tool-discovery round-trip works end-to-end (registration → tool list → invocation → LOG.md write).

**Consequences:** Reuse confirmed — future sessions can call `log_add` without re-priming. Revisit if the tool disappears from the discovery list or if LOG.md writes silently fail.

---

## 2026-09-07T14:48:31.265Z — Expose agent-scripts as MCP stdio server for Copilot Chat

**Context:** log-add.mjs and log-read.mjs were CLI-only and required manual invocation from a terminal. Copilot Chat could not call them directly, so log additions and reads during agent workflows had to fall back to copy-paste or terminal round-trips. Goal: surface these scripts as native Copilot tools via the Model Context Protocol with minimal added surface area.

**Decision:** Add a stdio MCP server (mcp-server.mjs) in agent-script/ that wraps the existing CLIs via @modelcontextprotocol/sdk + zod, spawn them as child processes from each tool handler, and register the server in .vscode/mcp.json. Extend log-add/log-read schema with --kind (decision|note|test) so operational entries (smoke tests, observations) fit alongside decisions without forcing them into the decision frame. Mirror the new flags in the MCP tool input schema.

**Consequences:** agent-script/package.json gains two runtime deps (@modelcontextprotocol/sdk, zod). Future tools only need new server.tool() blocks; the CLI scripts keep working standalone. log-add.mjs becomes kind-aware; existing decision entries remain valid because the default kind is 'decision'. Per the user's preference that 'more in log is better than less', the smoke-test entry previously written was re-classified as a kind=test entry instead of deleted.

---

## 2026-09-07T14:44:09.228Z — Smoke-test MCP wiring post-IDC-enable

**Kind:** test
**Run:** Issue MCP initialize + tools/list + tools/call(log_read, n=1) + tools/call(log_add, no dry-run flag) against the stdio server after enabling it in the VS Code tool picker.

**Result:** initialize returned protocol `2024-11-05` with `tools` capability; tools/list advertised both `log_add` and `log_read` with their input schemas; both tools/call invocations returned content correctly. This entry was the run artifact — the log_add call landed a real entry rather than a dry-run because `--dry-run` was omitted from the test JSON.

**Consequences:** Re-classified post-hoc from a decision to a `kind=test` entry to fit the extended MADR schema. Wording cleaned up to match the test/run/result frame.

---

## 2026-09-07T14:39:24.650Z — Commit partial JSON-rewrite WIP from prior session

**Context:** Uncommitted changes on session/agent-infrastructure refactor console configuration from hardcoded addConsole(...) calls in main.cpp to a JSON config parsed via ArduinoJson. Work was abandoned mid-rewrite. consoleDefinitions_init() in src/consoles.cpp never calls addConsole() and the JSON content is duplicated as a C++ string literal rather than loaded from src/json/base.json. Result: consoles vector is empty at boot, selectStack_init clocks zero times, currentConsole() is UB. TODO.md Phase 1 items 1 and 2 already document this exact state.

**Decision:** Commit the work as-is on session/agent-infrastructure with a WIP prefix and explicit do-not-flash warning in the commit body, rather than discarding or attempting to complete the rewrite. Per AGENT.md policy we do not delete items silently; TODO already names the blockers.

**Consequences:** HEAD on session/agent-infrastructure will not produce a functional binary. Do not flash from this commit until TODO #1 (wire JSON loading or restore hardcoded addConsole calls) and TODO #2 (fix selectStack_init) are resolved. Source of truth for what is broken is now TODO.md, not the diff.

---

## 2026-09-07T14:30:31.977Z — Adopt MADR-format decision log and agent-script tooling

**Context:** Decisions were being made in chat and lost across sessions. No persistent record of why choices were made or what tradeoffs were accepted.

**Decision:** Adopt MADR (Markdown ADR) format for LOG.md, three-file layout (AGENT/TODO/LOG) at repo root, and Node.js scripts in agent-script/ for deterministic log management. Session branches named session/<topic-slug> off main. UTC timestamps. Scripts resolve paths via import.meta.url so cwd does not matter.

**Consequences:** All future significant decisions get logged. Scripts handle formatting consistency. May feel heavy for trivial changes — use judgment. package.json and .nvmrc scoped to agent-script/ to keep PlatformIO root clean.

---


---
