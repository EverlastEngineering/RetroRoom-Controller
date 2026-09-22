# Pico 2 W platform bring-up + WiFi (webserver strategy)

**Status:** open — exploratory (just got the hardware plugged in)
**Branch:** session/pico-2-wireless
**File anchor:** [platformio.ini](../../platformio.ini), [src/network.h](../../src/network.h), [src/network.cpp](../../src/network.cpp), [src/configuration.h](../../src/configuration.h)

## What

Two distinct goals that interlock:

1. **Platform support**: add `[env:pico2w]` to [platformio.ini](../../platformio.ini) so we can build + flash firmware onto the Raspberry Pi **Pico 2 W** (RP2350A, dual-core ARM Cortex-M33, on-board CYW43 WiFi/BLE).
2. **WiFi webserver**: re-introduce the ESP8266-era webserver strategy on the new platform — AsyncWebServer serving the controls page (`/`) + WebSocket (`/ws`) for live `ledOn`/`ledOff`/`flash`/`healthcheck` commands — using the on-board CYW43 stack instead of ESP8266 WiFi.

## Why

The user got a brand-new Pico 2 W on the bench and wants wireless control restored — the same way the legacy `[env:nodemcuv2]` (now deleted on `session/merge-pico-json` commit `c6005c7`) had it. The earlier CYW43 work was deferred (see `todo/open/2026-09-12_cyw43-picow-wifi.md`, opened on the merge-pico-json branch) and the new `pico-2-wireless` branch picks that up.

Note: the original "Pico-W" target (`rpipicow`, RP2040 + CYW43) is a different board from the Pico 2 W (`rpipico2w`, RP2350 + CYW43). The framework supports both; we'll use the existing CYW43 wiring but on a new chip.

## How

### Phase A — Verify the build compiles on Pico 2 W (today)

1. Add a new `[env:pico2w]` block to [platformio.ini](../../platformio.ini):
   - `board = rpipico2w`
   - `lib_deps = fastled/FastLED@^3.10.0`, `mathertel/RotaryEncoder@^1.5.3`, `evert-arias/EasyButton@^2.0.1`, `z3t0/IRremote@^4.7.1`, `bblanchon/ArduinoJson@^7.2.0`, plus **PicoW-specific WiFi libs** (TBD; see Phase B)
   - `-D HAS_LEDS`, `-D HAS_IR`, `-DARDUINO_RASPBERRY_PI_PICO_2W`
   - Same `FASTLED_RP2040_CLOCKLESS_PIO=1` / `FASTLED_RP2040_CLOCKLESS_M0_FALLBACK=0` (the framework auto-routes RP2040 vs RP2350 from the variant)
   - Same `build_src_filter = +<*> -<.git/> -<.pio/>`
2. Confirm `pio run -e pico2w` builds clean on the host. If FastLED or any lib fails on RP2350, fix or pin a different version.
3. Flash the resulting `.uf2` to the Pico 2 W via picotool (`picotool load -u firmware.uf2`) or BOOTSEL drag-drop. Confirm boot via serial monitor.

### Phase B — WiFi stack

This is the work that was deferred from `session/merge-pico-json` (commit `c6005c7` left `src/network.{h,cpp}` as a `#error` stub when `HAS_WIFI` is defined).

1. Pick the WiFi lib(s). Earle Philhower's arduino-pico core includes `cyw43_wrappers.h` (visible in the rpipico2w variant). Candidates:
   - **Earle Philhower's `WebServer` library** — built-in `WiFiWebServer` works directly with `WiFi.status()` / `WiFiClient`.
   - **`ESP Async WebServer` by `me-no-dev`** — historically Arduino-only, has rp2040/pico support but uses the Arduino framework's `WiFi` abstraction. **Doesn't work with Earle Philhower's core** (it's bound to the Arduino `WiFi.h` API that wraps `WiFiS3`/`WiFi101`/etc.). Skip.
   - **`pico-async-webserver`** — a small standalone CYW43-based AsyncWebServer. Less battle-tested but matches our pattern (AsyncWebServer + AsyncWebSocket).
   - **Hand-roll a minimal `WebServer` (no Async)** — fastest path to "page renders + websocket-equivalent polling".

   Recommendation: start with Earle Philhower's `WebServer` + a hand-rolled polling endpoint (`/state.json` + `/cmd?ledOn`). Add async support later if needed.

2. Add `lib_deps = WebServer@^1.0` (or `me-no-dev/ESPAsyncWebServer@^3.x` for the pico variant if available) to `[env:pico2w]`.

3. Add `-D HAS_WIFI` to `[env:pico2w] build_flags`.

4. Rewrite [src/network.cpp](../../src/network.cpp):
   - Replace the `#error` stub with real WiFi init + WebServer handlers.
   - Use the legacy ESP8266-style page (rendered from [src/html/index.html](../../src/html/index.html) + [src/html/script.js](../../src/html/script.js)) for `/`.
   - Implement `/cmd?ledOn`, `/cmd?ledOff`, `/cmd?flash`, `/cmd?healthcheck`.
   - Optional: a `/ws` upgrade endpoint if the lib supports it (for live push).

5. Add a `state.json` endpoint that returns the current `currentConsoleIndex`, the FastLED ring state (first 8 LEDs), and the `flash` boolean so the page can re-render after refresh.

### Phase C — WebSocket-equivalent

Even if AsyncWebSocket isn't available on RP2350/CYW43, the polling-based state.json endpoint from Phase B is functionally equivalent for the controls use case (turn LEDs on/off, flash, etc.). Defer WebSocket until a real need shows up.

### Phase D — Verify the legacy HTML/JS page renders

1. Confirm [src/html/index.html](../../src/html/index.html) + [src/html/script.js](../../src/html/script.js) render under the new webserver (likely yes — they're plain HTML/JS, no server-side templating).
2. Update the page title from "RetroRoom" (NodeMCU-era) to "RetroRoom v2" so users know it's the Pico port.

## Hardware gotchas (this is a new board)

- The Pico 2 W has an **on-board LED on GP64** (different from GP25 on the Pico). `LED_BUILTIN` resolves to GP64 on rpipico2w; the heartbeat blink will work without code changes.
- **On-board WS2812 is NOT present on the Pico 2 W** (that was a YD-RP2040 special). External WS2812 ring must be wired to DATA_PIN (currently GP4 per [src/configuration.h](../../src/configuration.h)'s RP2040 block; works as-is on the Pico 2 W too).
- CYW43 antenna is on the PCB; no external antenna needed.
- **First-time power-on** may need the user to hold BOOTSEL while plugging in to put it in mass-storage mode for the UF2 drag-drop. After that, `picotool load -u firmware.uf2` over USB-CDC works without BOOTSEL.

## Open questions

1. Do we want the legacy ESP8266 page (`src/html/`) to come back verbatim, or do we want a Pico-port-specific UI? The current page is fine but it references `/ws` for WebSocket push; if WebSocket doesn't work on CYW43 we need a polling fallback.
2. SSID / password — the legacy code used `AsyncWiFiManager` to set up a captive portal. For the Pico port we have no `AsyncWiFiManager` equivalent. Plan: hardcode the SSID/password for now (or store in flash), and let the operator set it via serial monitor later.
3. Static IP vs DHCP? Static IP makes the bench-test workflow easier.

## Dependencies (pre-existing)

- `z3t0/IRremote@^4.7.1` — works on RP2350 unchanged
- `fastled/FastLED@^3.10.0` — has rp2350 backend (uses same ARM cortex-m0+ bitbang / PIO logic)
- `bblanchon/ArduinoJson@^7.2.0` — already a dependency for lib/ConsoleConfig
- `mathertel/RotaryEncoder@^1.5.3` — platform-agnostic
- `evert-arias/EasyButton@^2.0.1` — platform-agnostic

## Related

- [todo/open/2026-09-12_cyw43-picow-wifi.md](./2026-09-12_cyw43-picow-wifi.md) — the older CYW43 TODO that this plan supersedes (it was written before the user had a Pico 2 W on the bench; this file is the concrete realization of that work).
- [src/network.{h,cpp}](../../src/network.{h,cpp}) — the implementation anchor.
- [src/html/index.html](../../src/html/index.html), [src/html/script.js](../../src/html/script.js) — the legacy UI.
- [LOG.md](../../LOG.md) — historical decisions (the ESP8266 webserver is gone as of `c6005c7`, this is the rebuild).

## Gotchas discovered during Phase A

### RP2350 BOOTSEL stays mounted after UF2 write

Unlike the RP2040 (where writing a UF2 auto-ejects the volume and reboots
into the firmware), the RP2350 bootloader keeps the `/Volumes/RP2350`
mass-storage volume mounted until something explicitly exits BOOTSEL:

- A physical button press (BOOTSEL or RESET) on the board
- A USB cable unplug + re-plug
- A 1200-baud reset sequence over USB-CDC from the host (open + close the
  serial port at 1200 bps with `exclusive=True`; some macOS hosts need
  this dance to actually trigger the bootloader)

If `/Volumes/RP2350` appears immediately on power-up, the chip is in
BOOTSEL — flash a UF2 and exit BOOTSEL via one of the methods above to
boot into firmware.

### RP2350 CDC-ACM buffer flush

Earle Philhower's `rpipico2w` board target builds a USB-CDC ACM that
**drops writes issued before the host opens the port**. This means:

- The firmware's `setup()` can print a dozen `Serial.println()` calls
  before the host connects, and none of them will arrive at the
  reader. Looks like a wedge but isn't.
- Once the host is open, subsequent prints flow normally.
- `while (!Serial ... < 3000)` in setup() (with a 3 s ceiling so the
  firmware boots unattended) lets the firmware block on the host
  opening. Anything *after* the wait makes it through.

Capture pattern that works reliably on macOS:
1. Force BOOTSEL via 1200-baud reset.
2. Wait for `/Volumes/RP2350` to mount.
3. `cp` the UF2.
4. Wait for `/dev/cu.usbmodem*` to enumerate (the firmware's CDC-ACM
   instance — happens after the bootloader exits).
5. Open the port immediately and read for ~6 s. The trailing
   `Setup Complete.` makes it through; everything before is lost.

A 1 Hz heartbeat blink on `LED_BUILTIN` (commit `a3408be`) is the
failsafe: if the on-board LED blinks, the firmware ran past
`pinMode(LED_BUILTIN, OUTPUT)` in `setup()`.
