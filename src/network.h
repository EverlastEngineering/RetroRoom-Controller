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
//   GET  /healthcheck  -> "OK\n"
//   GET  /state.json   -> JSON of current state for polling fallback
//   GET  /setup        -> SoftAP-only: HTML form to set SSID + password
//   POST /setup        -> SoftAP-only: write creds to LittleFS, reboot
//   GET  /wifi         -> JSON of current WiFi status (IP, RSSI, mode)
//   WS   /ws           -> broadcastSocketMessage() push channel
#if defined(HAS_WIFI)

#include <Arduino.h>
#include <string>

extern void network_init();
// Per-loop pump. Currently just feeds the captive-portal DNS server.
// Cheap; safe to call on every iteration of main.cpp::loop().
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

#endif // HAS_WIFI
#endif