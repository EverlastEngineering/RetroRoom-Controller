// ESP-coupled WiFi/AsyncWebServer stack was removed on session/merge-pico-json.
// The whole implementation is gated on HAS_WIFI; no current build defines it,
// so this file compiles to a stub. When the Pico-W CYW43 work lands (TODO.md),
// this becomes the anchor for the CYW43 AsyncWebServer equivalent.
//
// The legacy ESP8266 AsyncWebServer / AsyncWebSocket / AsyncWiFiManager code
// that used to live here is intentionally gone. See TODO.md and LOG.md.

#include "network.h"

#if defined(HAS_WIFI)
#error "HAS_WIFI is not yet wired for the Pico port. See TODO.md / src/network.h."
#endif
