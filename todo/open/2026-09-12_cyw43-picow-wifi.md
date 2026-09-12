# CYW43 (Pico-W) WiFi

**Status:** open
**Branch:** session/merge-pico-json
**File anchor:** [src/network.h](../../src/network.h), [src/network.cpp](../../src/network.cpp)
**Related doc:** [pico-pin-mapping.md](../../pico-pin-mapping.md) (no entry yet — add when implementation lands)

## What
Pico-W (RP2040 + CYW43 WiFi) gets a real AsyncWebServer stack like the
legacy ESP8266 env used to have. `src/network.{h,cpp}` is the intended
anchor for this work.

## Why
Right now there's no network connectivity on the Pico port. The web UI
that the legacy ESP8266 firmware exposed (`/` for the controls page,
`/ws` for the WebSocket for live LED on/off) isn't reachable. Once
the CYW43 work lands, the same UI can come back.

## How

### 1. Add the WiFi driver
In `platformio.ini`, under `[env:picow]`:
- Add `cyw43-driver` (or `PicoW-async-webserver` or equivalent) to `lib_deps`.
- Add `-D HAS_WIFI` to `[env:picow] build_flags`.

### 2. Map `network_init()` to CYW43
The current [src/network.cpp](../../src/network.cpp) body has ESP-only AsyncWebServer / AsyncWebSocket / AsyncWiFiManager code that won't link on RP2040. Rewrite it to:
1. Call `cyw43_arch_init()` (or equivalent) on boot.
2. Start the CYW43 in station mode (`cyw43_wifi_join_*`).
3. Join the configured SSID — hardcoded for now (defer WiFiManager / provisioning portal).
4. Bring up an AsyncWebServer on port 80 with routes for `/` (controls page) and `/ws` (WebSocket).

### 3. Move the `lightRing` WebSocket handler
Currently `broadcastSocketMessage("touch: touched")` is wired into
[src/controls.cpp](../../src/controls.cpp) and dead on Pico because
nothing on Pico emits those messages (no network). Once WiFi lands,
the WebSocket consumer in `network.cpp` (`websocketRoutes(uint8_t*)`)
should re-attach to `ledOn` / `ledOff` / `flash` / `healthcheck`
commands.

## Status note
`[env:picow]` currently has a placeholder pin map (DATA_PIN=4 etc.).
The picow board variant in Earle Philhower's core is `rpipicow` so
the same `vccgnd_yd_rp2040`-style auto-detect isn't needed for
hardware; the firmware's just waiting on the CYW43 driver to land.

## Dependencies
- [z3t0/IRremote@^4.7.1](../../platformio.ini) — IR sender is independent
- `lightRing` consumer in [src/lighting.cpp](../../src/lighting.cpp) — unaffected
