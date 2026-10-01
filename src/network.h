#ifndef RR_NETWORK_H
#define RR_NETWORK_H

// Network stack — only compiles when HAS_WIFI is defined. On Pico 2 W
// (and any future board with on-board CYW43 WiFi) HAS_WIFI is set in
// platformio.ini's [env:pico2w] build_flags.
//
// On first boot (no /wifi.json in LittleFS) the firmware brings up a
// SoftAP "RetroRoom-Setup" and serves a /setup HTML form. The user
// submits SSID + password over the SoftAP, the firmware writes them to
// LittleFS, restarts into STA mode, and connects.
//
// On subsequent boots the firmware reads /wifi.json from LittleFS and
// joins the saved network in STA mode. While connected, the same web
// UI is served from the legacy src/html/{index.html,script.js} with
// full WebSocket support so script.js's ws://host/ws flow works
// without falling back to xhrget polling.
//
// Endpoints (HTTP):
//   GET  /             -> legacy embedded index.html (iframe wrapper)
//   GET  /script.js    -> legacy embedded script.js
//   GET  /ledOn        -> turn the on-board LED on; sends "ledOn" over WS
//   GET  /ledOff       -> turn the on-board LED off; sends "ledOff" over WS
//   GET  /flash        -> toggle the flash state; sends "flash" over WS
//   GET  /next         -> wrap-around advance (forward); returns JSON {index,name}; broadcasts "console:<name>:<idx>" over WS; 409 if no consoles configured
//   GET  /prev         -> wrap-around rewind (backward); same JSON + WS broadcast shape as /next; 409 if no consoles configured
//   GET  /healthcheck  -> "OK\n"
//   GET  /state.json   -> JSON of current state for polling fallback. Fields:
//                         index, total, name, ledOn, flash, mode ("sta"|"ap"),
//                         uptimeMs (millis() at handler time),
//                         selectedAtUptimeMs (millis() at last /next|/prev decision)
//   GET  /consoles.json-> read-back of the live config file. 200 + raw bytes if present
//                         (byte-identical to what POST /consoles.json wrote),
//                         204 if file missing, 500 if LittleFS mount failed.
//   POST /consoles.json-> accept a console-config JSON body, validate it, save to LittleFS with two backups, reboot (see src/network.cpp for details)
//   GET  /setup        -> SoftAP-only: HTML form to set SSID + password
//   POST /setup        -> SoftAP-only: write creds to LittleFS, reboot
//   GET  /scan.json    -> Cached WiFi scan results (cached at boot; the CYW43
//                         can't scan while a client is associated with the SoftAP).
//                         Available in both AP and STA modes.
//   GET  /factory-reset-> Two-step wipe with a CSRF nonce embedded in the form.
//                         GET serves the confirmation page; POST must echo the
//                         nonce back (auto-resubmitted forms from stale tabs fail
//                         the nonce check). On success: removes /wifi.json and reboots.
//   GET  /wifi         -> JSON of current WiFi status (IP, RSSI, mode)
//   GET  /openapi      -> Swagger UI page. Loads /openapi.yaml same-origin so
//                         "Try it out" works without CORS. See plans/openapi.yaml
//                         for the spec source.
//   GET  /openapi.yaml -> Raw OpenAPI 3.0 spec (24 KB), served byte-for-byte from
//                         html/openapi.yaml
//   WS   /ws           -> broadcastSocketMessage() push channel. Recognized text commands: ledOn, ledOff, flash, next, prev, healthcheck (echo for unknown).
#if defined(HAS_WIFI)

#include <Arduino.h>
#include <string>

// Bring the radio up. Returns as soon as WiFi.begin() has been called --
// it does NOT wait for the join. network_loop() finishes the job: it
// watches for WL_CONNECTED or the timeout and then starts either the
// STA server or the SoftAP, so the cabinet stays responsive to the knob,
// the strip and the LCD while the router is being found.
extern void network_init();
// Run a synchronous wifi scan and cache the results so /scan.json can
// serve them later. MUST be called before network_init() on first boot
// (or before any AP-mode bring-up on reboot) -- the CYW43 can't scan
// while a client is associated with the SoftAP.
extern void network_scan_cache();
// Per-loop pump. Resolves the pending WiFi join (the second half of
// network_init()), restores the LCD after the WiFi-failure notice, and
// feeds the captive-portal DNS server. Cheap; safe to call on every
// iteration of main.cpp::loop().
extern void network_loop();
// Broadcast a string to all connected WebSocket clients. No-op if no
// clients are connected. Safe to call from any context (main loop or
// ISR-adjacent handler) as long as the AsyncWebServer is still alive.
extern void broadcastSocketMessage(const std::string& message);

// True once network_init() has brought up either the STA WiFi
// connection (with an IP) or the SoftAP portal. Used by main.cpp to
// know when to start advertising the IP and by console advance to
// broadcast the change.
extern bool network_isUp();
// True iff the device has joined a wifi network in STA mode (i.e.
// is configured and online). False while in SoftAP / captive-portal
// mode or before network_init() has run. Used by the on-board LED
// state machine to distinguish the "needs wifi config" (fast flash)
// convention from the "online and happy" (slow blink) convention.
extern bool network_inStaMode();
// True when the radio was never started, or was started and could not
// reach a saved network and is now on its own access point.
//
// NOT the same as `!network_inStaMode()`, and the difference matters at
// exactly one moment: the end of setup(), before a join that is merely
// slow has had its chance. A cabinet in that state is not unreachable,
// it is undecided, and treating it as unreachable interrupts a healthy
// device on every boot.
//
// Used by src/serialconfig.cpp to decide whether the serial
// configuration channel should open itself. See the definition for what
// is and is not reachable when this is true.
extern bool network_isUnreachable();
// True iff `network.disable` left the radio off. Distinct from "not
// up": SoftAP mode and disabled mode both report network_isUp()
// false, and conflating them makes a cabinet that is working exactly
// as configured blink and log like a fault. src/state.cpp uses this to
// pick the healthy cadence and the "off" heartbeat mode.
extern bool network_disabled();

#endif // HAS_WIFI
#endif