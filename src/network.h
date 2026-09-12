#ifndef RR_NETWORK_H
#define RR_NETWORK_H

// The whole network stack is WiFi-coupled (AsyncWebServer, AsyncWebSocket,
// CYW43 driver, etc.) and currently does nothing on any env (ESP8266 is
// gone; the Pico-W CYW43 work is tracked in TODO.md). On any env without
// HAS_WIFI the network code compiles to nothing so the rest of the firmware
// can build on pico_base. When the Pico-W WiFi lands, this is the intended
// anchor:
//   1. Add cyw43-driver (or similar) to lib_deps for [env:picow].
//   2. Add -D HAS_WIFI to [env:picow] build_flags.
//   3. Implement network_init() here using the CYW43 API.
//
// The ESP-coupled includes (FS.h, ESP8266WiFi.h, ESP8266mDNS.h,
// ESPAsyncWebServer.h, ESPAsyncWiFiManager.h) are gone; once HAS_WIFI is
// enabled for the Pico-W board, AsyncWebServer + a CYW43 driver go back
// in their place. See TODO.md for the full plan.
#if defined(HAS_WIFI)

#include <WiFi.h>

extern void network_init();
extern void broadcastSocketMessage(const std::string& message);

#endif // HAS_WIFI
#endif