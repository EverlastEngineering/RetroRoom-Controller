// Pico 2 W (CYW43) AsyncWebServer implementation. Brings up WiFi in
// either STA or SoftAP mode based on whether /wifi.json exists in
// LittleFS, then serves the legacy UI from src/html/ over an
// AsyncWebServer + AsyncWebSocket.
//
// Flow:
//   1. network_init() called from main.cpp::setup() after Serial.begin.
//   2. Try to read /wifi.json from LittleFS.
//   3. If missing or unparseable, bring up SoftAP "RetroRoom-Setup"
//      (open, no password) and serve /setup. User POSTs creds; we
//      write to LittleFS and restart via ESP.restart() equivalent
//      (rp2040.restart() / watchdog_reboot()).
//   4. If creds are present, WiFi.begin(ssid, pass); wait up to 20 s
//      for connection. On failure, fall back to SoftAP.
//   5. Once connected, AsyncWebServer serves /, /script.js, /ledOn,
//      /ledOff, /flash, /healthcheck, /state.json, and /ws. broadcast
//      SocketMessage() pushes events to all WS clients.

#include "network.h"

#if defined(HAS_WIFI)

#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <pico/cyw43_arch.h>
#include <cyw43.h>
#include <functional>
#include <atomic>

#include "html.h"
#include "state.h"
#include "main.h"
#include "display.h"
#include "consoleconfig_store.h"
#if defined(HAS_LEDS)
#include "ledstring.h"
#endif
#include <ConsoleConfig.h>

// ---------- module state ----------

namespace {

constexpr const char* kWifiConfigPath = "/wifi.json";
constexpr const char* kApSsid        = "RetroRoom-Setup";
constexpr const char* kApPass        = "";  // open AP; acceptable since the SoftAP only
                                            // exposes a /setup form and we reboot immediately
                                            // after the user POSTs.
constexpr const char* kHostname      = "RetroRoom";  // DHCP hostname -> RetroRoom.local
                                                       // on mDNS-aware LANs (avoids the
                                                       // CYW43 default "PicoW").
constexpr uint8_t     kStaTimeoutSec = 20;
constexpr uint8_t     kScanCacheMax  = 24; // CYW43 returns at most 24 per scan

struct CachedNet {
    String  ssid;
    int32_t rssi;
    uint8_t auth;
    uint8_t channel;
    bool    open;
};

AsyncWebServer       server(80);
AsyncWebSocket       ws("/ws");
DNSServer            dnsServer;          // Captive-portal DNS: replies to every query
                                         // with the AP's IP so iOS/macOS/Android/Windows
                                         // treat the network as a captive portal.
bool                 wsClientConnected = false;
bool                 networkUp = false;
bool                 inApMode  = false;
unsigned long        dnsSuspendedAt = 0; // millis() when /debug/dns-off was hit;
                                          // network_loop() re-enables after 60 s.

// One-shot CSRF nonce for /factory-reset. The GET handler stores a
// freshly-generated uint32_t here; the POST handler reads + compares
// it to the form's hidden nonce field, then zeroes it on a successful
// match so the same form can't be replayed (browser-back-then-forward,
// browser-resume after navigation, etc.). std::atomic because AsyncWebServer
// can run request handlers from a different context than main loop on
// RPAsyncTCP, and even single-threaded it's a cheap guarantee.
std::atomic<uint32_t> g_factoryResetNonce(0);

// Pending reboot timestamp for /factory-reset. The POST handler sends
// the success page, sets this to millis()+kFactoryResetRebootDelayMs,
// and returns. network_loop() watches the flag and triggers
// rp2040.restart() once the deadline passes -- giving the AsyncTCP
// stack plenty of time to flush the response before the device goes
// down. Calling rp2040.restart() directly from the request handler is
// racy: the TCP send buffer hasn't necessarily drained by the time
// we restart, so the user gets a spinner + timeout instead of the
// "Resetting now!" page.
static constexpr unsigned long kFactoryResetRebootDelayMs = 1500;
volatile unsigned long g_pendingFactoryResetRebootAt = 0;

// Pending reboot timestamp for POST /consoles.json. Mirrors the
// factory-reset pattern (separate flag, same delay constant). The
// POST handler validates the JSON via the functional core, writes it
// (plus two backups) to LittleFS via the shell-side store wrapper,
// sends a short "Saved. Rebooting..." page, and arms this deadline.
// network_loop() fires the restart from main-loop context so the TCP
// send buffer drains cleanly. Separate from
// g_pendingFactoryResetRebootAt so a factory reset happening mid-
// POST (theoretical -- the CSRF nonce would block it, but defense in
// depth) can't clobber this one.
static constexpr unsigned long kConsoleConfigRebootDelayMs = 1500;
volatile unsigned long g_pendingConsoleConfigRebootAt = 0;

// How long the operator-facing outputs get to settle after being
// blanked and before rp2040.restart() actually fires. Both writes
// leave this TU as I2C traffic (the HD44780 needs a few ms to finish
// executing the last few character writes), and a hard reset in the
// same tick can leave the panel showing the old console instead of
// "Rebooting". Costs nothing -- the device is going down regardless,
// and the TCP response drained a full kConsoleConfigRebootDelayMs ago.
static constexpr unsigned long kRebootOutputSettleMs = 250;

// Boot-time scan cache. Populated by network_scan_cache() before the
// AP comes up (the CYW43 cannot scan while a client is associated, so
// this has to run pre-AP). /scan.json serves from this array; no
// in-flight scans ever happen from a request handler.
CachedNet            scanCache[kScanCacheMax];
uint8_t              scanCacheCount = 0;
bool                 scanCacheReady = false;

}  // namespace

// ---------- forward declarations ----------

static void startApPortal();
static void startStaServer();
static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* cli,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);

// Per-loop pump for the DNS server. Only does work while the AP is
// running (inApMode). Call from the main loop after setup().
void network_loop();

// ---------- public API ----------

bool network_isUp() { return networkUp; }
bool network_inStaMode() { return networkUp && !inApMode; }

// network_scan_cache -- synchronous STA-mode scan, must be called
// BEFORE the AP comes up. The CYW43 radio cannot scan while a client
// is associated with the SoftAP, so this is the only safe window.
// Idempotent: re-running replaces the cached results.
//
// On Pico 2 W boot this gets called once before network_init(). If
// the device later joins an AP in STA mode and the operator wants
// the captive portal back, they'd have to power-cycle (we don't
// tear down STA->AP in the current build).
void network_scan_cache() {
	// Need STA-only mode for the scan; cyw43_arch_enable_sta_mode()
	// is what scanNetworks() does internally, but we set it
	// explicitly here so the AP doesn't get briefly brought up
	// before the scan starts.
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	delay(100);  // let the radio settle into STA mode

	scanCacheCount = 0;
	scanCacheReady = false;
	int16_t n = WiFi.scanNetworks(/*async=*/false);
	Serial.print("net: boot scan found ");
	Serial.print(n);
	Serial.println(" networks");

	uint8_t keep = (n > (int16_t)kScanCacheMax) ? kScanCacheMax : (uint8_t)n;
	for (uint8_t i = 0; i < keep; i++) {
		scanCache[i].ssid    = WiFi.SSID(i);
		scanCache[i].rssi    = WiFi.RSSI(i);
		scanCache[i].auth    = WiFi.encryptionType(i);
		scanCache[i].channel = WiFi.channel(i);
		scanCache[i].open    = (scanCache[i].auth == CYW43_AUTH_OPEN);
	}
	scanCacheCount = keep;
	scanCacheReady = true;

	// Free the Earle WiFi class's internal scan-result buffer.
	// (scanCache owns the Strings; scanDelete drops the lib's copies.)
	WiFi.scanDelete();
}

void broadcastSocketMessage(const std::string& message) {
#if !defined(HAS_WIFI)
	(void)message;
#else
	if (!networkUp) return;
	// AsyncWebSocket::textAll takes a const char* + len (preferred) or an
	// Arduino String. We use the C-string overload to avoid the String
	// copy. Our callers (touch: touched, ledOn, ledOff, flash, healthcheck)
	// never include embedded nulls.
	ws.textAll(message.c_str(), message.size());
#endif
}

// ---------- helpers ----------

// Read /wifi.json from LittleFS. Returns true on success and fills ssid/pass.
static bool loadWifiCreds(String& ssid, String& pass) {
	if (!LittleFS.begin()) {
		Serial.println("net: LittleFS mount failed");
		return false;
	}
	if (!LittleFS.exists(kWifiConfigPath)) {
		Serial.println("net: no /wifi.json; will start SoftAP");
		return false;
	}
	File f = LittleFS.open(kWifiConfigPath, "r");
	if (!f) {
		Serial.println("net: open /wifi.json failed");
		return false;
	}
	StaticJsonDocument<256> doc;
	DeserializationError err = deserializeJson(doc, f);
	f.close();
	if (err) {
		Serial.print("net: /wifi.json parse failed: ");
		Serial.println(err.c_str());
		return false;
	}
	const char* s = doc["ssid"] | "";
	const char* p = doc["pass"] | "";
	if (strlen(s) == 0) {
		Serial.println("net: /wifi.json missing ssid");
		return false;
	}
	ssid = s;
	pass = p;
	return true;
}

static void saveWifiCreds(const String& ssid, const String& pass) {
	if (!LittleFS.begin()) {
		Serial.println("net: LittleFS mount failed on save");
		return;
	}
	File f = LittleFS.open(kWifiConfigPath, "w");
	if (!f) {
		Serial.println("net: open /wifi.json for write failed");
		return;
	}
	StaticJsonDocument<256> doc;
	doc["ssid"] = ssid;
	doc["pass"] = pass;
	serializeJson(doc, f);
	f.close();
	Serial.println("net: /wifi.json written");
}

// ---------- SoftAP setup portal ----------

// The /setup page and its companion JS live in src/html/setup.html and
// src/html/setup.js, included via html.h as `const String {...}`. The
// wrappers use `R""""(` / `)""""` to dodge C preprocessor edge cases
// (CSS `2em` parses as a hex float, embedded `)"` sequences break raw
// strings, etc.). See src/html.h for the full rationale.

static void onSetupPost(AsyncWebServerRequest* req) {
	if (!req->hasParam("ssid", true)) {
		req->send(400, "text/plain", "missing ssid");
		return;
	}
	String ssid = req->getParam("ssid", true)->value();
	String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String("");
	ssid.trim();
	if (ssid.length() == 0) {
		req->send(400, "text/plain", "empty ssid");
		return;
	}
	Serial.print("net: /setup POST ssid=\"");
	Serial.print(ssid);
	Serial.println("\"");
	saveWifiCreds(ssid, pass);
	req->send(200, "text/html",
	          "<html><body style=\"font-family:sans-serif;text-align:center;margin-top:4em\">"
	          "<h2>Saved!</h2><p>Rebooting into <code>" + ssid + "</code> &hellip;</p>"
	          "</body></html>");
	// Reboot after a short delay so the response is flushed. Use rp2040
	// watchdog if available; fall back to NVIC reset.
	delay(500);
	rp2040.restart();
}

static void startApPortal() {
	Serial.print("net: starting SoftAP \"");
	Serial.print(kApSsid);
	Serial.println("\"");
	// WIFI_AP_STA so the radio runs both interfaces at once. The
	// CYW43 can only do a wifi scan from a STA interface; running
	// AP-only means scanNetworks() walks the radio into STA mode
	// internally, but the softAP netif gets torn down in the
	// process and the DHCP server drops. AP_STA mode keeps both
	// interfaces up so /scan.json works without disturbing the
	// captive portal. STA has no SSID configured so it doesn't
	// try to join anything -- it just exists for the scan.
	WiFi.mode(WIFI_AP_STA);
	WiFi.setHostname(kHostname);  // visible to DHCP clients as "RetroRoom"
	// Earle's WiFi.softAP(ssid, password) wrapper does
	//   cyw43_arch_enable_ap_mode(ssid, password, password ? WPA2 : OPEN)
	// with a pointer-null check, NOT strlen(). Passing "" falls into the
	// "set password + apply auth" branch and the radio advertises WPA2
	// with a zero-length key -- macOS/iOS show a lock icon and refuse to
	// pop the captive-portal sheet.
	//
	// WiFi.softAP() has no way to pass nullptr (the C-string parameter
	// is const char*, not optional). So we use WiFi.softAP() to bring
	// up the lwIP netif (otherwise WiFi.softAPIP() returns IP-unset and
	// DHCP never starts), then tear the AP down and re-bring it up via
	// the pico-sdk directly with a real nullptr password. The tear-down
	// + bring-up cycle is the only thing that pushes the auth change to
	// the radio; cyw43_wifi_ap_set_auth() alone only mutates the
	// in-memory state and never calls cyw43_ll_wifi_ap_init() again.
	bool ok = WiFi.softAP(kApSsid, kApPass);
	if (!ok) {
		Serial.println("net: softAP() failed");
		return;
	}
	// Reconfigure as a true open AP. Disable first so the second
	// cyw43_arch_enable_ap_mode() call goes through the
	// (itf_state == 0) branch in cyw43_wifi_set_up() and re-inits the
	// radio with the new auth.
	cyw43_arch_disable_ap_mode();
	cyw43_arch_enable_ap_mode(kApSsid, nullptr, CYW43_AUTH_OPEN);
	inApMode  = true;
	networkUp = true;

	IPAddress ip = WiFi.softAPIP();
	Serial.print("net: SoftAP IP = ");
	Serial.println(ip);  // typically 192.168.4.1

	// Boot-time diagnostic scan was removed: the first scanNetworks()
	// call flips the Earle WiFi class's _wifiHWInitted flag and walks
	// the CYW43 into STA mode; flipping back to AP afterward left
	// the radio in a state where the second scan (from the
	// /scan.json handler) returned zero results. Keep this section
	// empty; the real diagnostic lives in /scan.json itself.
	{
		// intentionally blank -- do not add a boot-time scan here
	}

	// WiFi scan endpoint -- synchronous (the only overload Earle's
	// arduino-pico core exposes is scanNetworks(bool async = false)).
	// scanNetworks() calls cyw43_arch_enable_sta_mode() internally to
	// do the scan, which wipes any existing scan results and tears down
	// the AP netif. The fix is to (1) capture the count + read every
	// /scan.json -- serves the boot-time scan cache. The CYW43 radio
	// cannot scan while a client is associated with the SoftAP, so we
	// do the scan ONCE during boot (before the AP comes up) and serve
	// from the cache for the lifetime of the firmware. /scan.json is
	// safe to call from any request handler regardless of how many
	// clients are connected -- there's no live scan.
	server.on("/scan.json", HTTP_GET, [](AsyncWebServerRequest* req) {
		String out;
		out.reserve(96 + scanCacheCount * 80);
		out += "{\"_count\":";
		out += scanCacheCount;
		out += ",\"networks\":[";
		for (uint8_t i = 0; i < scanCacheCount; i++) {
			if (i) out += ',';
			out += "{\"ssid\":\"";
			out += scanCache[i].ssid;
			out += "\",\"rssi\":";
			out += scanCache[i].rssi;
			out += ",\"open\":";
			out += scanCache[i].open ? "true" : "false";
			out += ",\"auth\":";
			out += scanCache[i].auth;
			out += ",\"channel\":";
			out += scanCache[i].channel;
			out += '}';
		}
		out += "]}";
		req->send(200, "application/json", out);
	});

	// Captive-portal detection is handled by the DNS server below: every
	// query for any hostname resolves to 192.168.4.1, so every browser
	// request that hits "the internet" actually hits us. The HTTP
	// not-found handler below then redirects to /setup.

	// Serve only the setup form on the AP. We register a wildcard
	// catch-all that sends users to /setup so they can't accidentally
	// land on the not-yet-configured legacy UI.
	server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
		req->redirect("/setup");
	});
	// /setup form: serve the static HTML and the companion JS. Both
	// live in src/html/{setup.html,setup.js}, exposed via html_setup_html
	// / html_setup_js from src/html.h.
	server.on("/setup", HTTP_GET, [](AsyncWebServerRequest* req) {
		req->send(200, "text/html", html_setup_html);
	});
	server.on("/setup.js", HTTP_GET, [](AsyncWebServerRequest* req) {
		req->send(200, "application/javascript", html_setup_js);
	});
	server.on("/setup", HTTP_POST, onSetupPost);
	server.onNotFound([](AsyncWebServerRequest* req) {
		// Anything else (including the legacy /script.js, /ledOn, etc.)
		// also redirects to /setup while we're still on the AP.
		req->redirect("/setup");
	});

	// /debug/dns-off and /debug/dns-on let the operator temporarily
	// suspend the captive-portal DNS catch-all so they can navigate
	// to /scan.json (or any other URL) in a regular browser tab while
	// the SoftAP is up. Auto-restart after 60 s so we never leave the
	// device in an unsafe state. Debug-only.
	server.on("/debug/dns-off", HTTP_GET, [](AsyncWebServerRequest* req) {
		dnsSuspendedAt = millis();
		dnsServer.stop();
		Serial.println("net: debug -- dns catch-all SUSPENDED for 60 s");
		req->send(200, "text/plain", "DNS suspended for 60 s. /scan.json reachable directly now.\n");
	});
	server.on("/debug/dns-on", HTTP_GET, [](AsyncWebServerRequest* req) {
		dnsSuspendedAt = 0;
		// Re-enable without flipping WiFi mode (that would clobber
		// the CYW43 state and break subsequent /scan.json calls).
		IPAddress ip = WiFi.softAPIP();
		dnsServer.start(53, "*", ip);
		dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
		Serial.println("net: debug -- dns catch-all RE-ENABLED");
		req->send(200, "text/plain", "DNS re-enabled.\n");
	});

	ws.onEvent(onWsEvent);
	server.addHandler(&ws);
	server.begin();

	// DNS server: replies to every query with the SoftAP IP. This is the
	// bog-standard captive-portal trick that every ESP8266/Pico-W
	// WiFiManager sketch uses -- iOS/macOS/Android/Windows all probe a
	// well-known host, see our IP, and pop the captive-portal sheet.
	dnsServer.start(53, "*", ip);
	dnsServer.setErrorReplyCode(DNSReplyCode::NoError);

	Serial.println("net: setup portal running on http://192.168.4.1/setup");
}

// ---------- STA mode (normal operation) ----------

static void onRoot(AsyncWebServerRequest* req) {
	// index.html is a raw string literal in src/html/index.html wrapped
	// via the R"r(...)r" macro into a C string. The html_index_html String
	// holds the file contents; send it as text/html so the iframe wrapper
	// renders.
	req->send(200, "text/html", html_index_html);
}

static void onScriptJs(AsyncWebServerRequest* req) {
	req->send(200, "application/javascript", html_script_js);
}

static void onLedOn(AsyncWebServerRequest* req) {
	ledOn();
	broadcastSocketMessage("ledOn");
	req->send(200, "text/plain", "OK\n");
}

static void onLedOff(AsyncWebServerRequest* req) {
	ledOff();
	broadcastSocketMessage("ledOff");
	req->send(200, "text/plain", "OK\n");
}

static void onFlash(AsyncWebServerRequest* req) {
	// flashLed() already toggles the `flash` flag; the caller doesn't
	// do it again. Broadcast the post-toggle state.
	flashLed();
	broadcastSocketMessage(flash ? "flash on" : "flash off");
	req->send(200, "text/plain", flash ? "flash on\n" : "flash off\n");
}

static void onHealthcheck(AsyncWebServerRequest* req) {
	broadcastSocketMessage("healthcheck");
	req->send(200, "text/plain", "OK\n");
}

// /next + /prev: HTTP-driven console advance / rewind. These are the
// script-friendly counterpart of the USR button (advanceConsole() in
// src/consoles.cpp); the e2e test in agent-script/e2e-consoles-json.sh
// drives them over curl, the embedded UI's script.js can call them
// from any button, and the WebSocket command "next" / "prev" (see
// onWsEvent below) drives the same code path so the UI doesn't have
// to choose between HTTP and WS.
//
// Both endpoints return the current index + console name as JSON so
// the caller can verify the device actually moved (curl --include
// surfaces the JSON; a synchronous test script asserts on it).
// advanceConsole() / rewindConsole() also broadcast a "console:<name>:<idx>"
// WS message in the same call, so any connected UI sees the change
// without having to poll.
static void onConsoleNext(AsyncWebServerRequest* req) {
	// Guard against advancing into an empty vector. CurrentConsole()
	// dereferences operator[] which is UB on size 0; surface 409
	// with a typed error so the harness can distinguish "no config
	// uploaded yet" from a real HTTP failure.
	if (consoles.empty()) {
		req->send(409, "application/json",
		          "{\"error\":\"no consoles configured\"}\n");
		return;
	}
	advanceConsole();
	// 200 OK with the current state -- lets the test script
	// confirm the device actually advanced without a second
	// round-trip to /state.json.
	String out = "{\"index\":" + String(currentConsoleIndex) +
	             ",\"name\":\"" + CurrentConsole().name.c_str() + "\"}\n";
	req->send(200, "application/json", out);
}

static void onConsolePrev(AsyncWebServerRequest* req) {
	if (consoles.empty()) {
		req->send(409, "application/json",
		          "{\"error\":\"no consoles configured\"}\n");
		return;
	}
	rewindConsole();
	String out = "{\"index\":" + String(currentConsoleIndex) +
	             ",\"name\":\"" + CurrentConsole().name.c_str() + "\"}\n";
	req->send(200, "application/json", out);
}

static void onStateJson(AsyncWebServerRequest* req) {
	// Surface enough state that the e2e harness can verify every
	// assertion from one round-trip:
	//   index/name           -- current console
	//   total                -- how many consoles are loaded (lets
	//                            the harness distinguish "no config"
	//                            from "config has one console")
	//   ledOn / flash / mode -- UI status (unchanged)
	//   uptimeMs             -- millis() at the time this handler ran,
	//                            so the harness can compute time deltas
	//   selectedAtUptimeMs   -- millis() at the moment of the most
	//                            recent /next or /prev decision (set
	//                            in advanceConsole()/rewindConsole()
	//                            in src/consoles.cpp). Asserting that
	//                            this value CHANGES across calls is the
	//                            harness's TDD proof that the device
	//                            actually advanced -- not just that the
	//                            HTTP response code was 2xx.
	StaticJsonDocument<384> doc;
	doc["index"]   = currentConsoleIndex;
	doc["total"]   = HowManyConsoles();
	// Empty-vector guard: CurrentConsole() dereferences operator[]
	// on a possibly-empty vector (UB on size 0). When no consoles
	// are configured (factory-fresh device, POST empty.json, etc.)
	// surface name="" and index=0 so the harness can read the state
	// without crashing the request handler. total=0 is the canonical
	// "no consoles" signal the harness asserts on.
	if (!consoles.empty()) {
		doc["name"]    = CurrentConsole().name.c_str();
	} else {
		doc["name"]    = "";
	}
	doc["ledOn"]   = statusLedActive != 0x0;
	doc["flash"]   = flash;
	doc["uptimeMs"]           = (unsigned long)millis();
	doc["selectedAtUptimeMs"] = (unsigned long)currentConsoleSelectedAtMs;
	const char* mode = inApMode ? "ap" : "sta";
	doc["mode"]    = mode;
	String out;
	serializeJson(doc, out);
	req->send(200, "application/json", out);
}

static void onWifiJson(AsyncWebServerRequest* req) {
	StaticJsonDocument<256> doc;
	doc["mode"] = inApMode ? "ap" : "sta";
	if (inApMode) {
		doc["ip"]   = WiFi.softAPIP().toString();
		doc["ssid"] = kApSsid;
	} else {
		doc["ip"]   = WiFi.localIP().toString();
		doc["ssid"] = WiFi.SSID();
		doc["rssi"] = WiFi.RSSI();
	}
	String out;
	serializeJson(doc, out);
	req->send(200, "application/json", out);
}

// /consoles.json GET: serve the live console config back to the
// caller verbatim. Used by the e2e harness to verify the POST
// actually landed on disk (byte-equality, not a JSON re-serialize
// round-trip). Three response shapes:
//   200 + raw bytes   -- file present, served byte-for-byte from
//                          LittleFS (the same bytes POST /consoles.json
//                          wrote via File.print())
//   204 + empty body  -- file missing (factory-fresh device)
//   500 + JSON error  -- LittleFS mount failed; the operator should
//                          investigate (FS corruption, missing
//                          partition, etc.)
//
// Why raw bytes instead of re-serializing through ArduinoJson on the
// read path: ArduinoJson v7's serializeJson is NOT byte-stable with
// the POST input -- it minifies whitespace, escapes non-ASCII via
// \uXXXX, reformats numeric fields, etc. The TDD harness's "POST a
// config and read it back" assertion would flake on every change to
// either formatter. The cleanest fix is to never touch the bytes:
// the POST handler calls f.print(payload) verbatim, the GET handler
// reads them back with loadLiveConsoleConfig() and ships them
// verbatim. Round-trip is trivially byte-identical.
static void onConsolesJsonGet(AsyncWebServerRequest* req) {
	if (!retroroom_store::ensureMounted()) {
		Serial.println("net: GET /consoles.json -- LittleFS mount failed");
		req->send(500, "application/json",
		          "{\"error\":\"fs mount failed\"}\n");
		return;
	}
	std::string fs_json;
	if (!retroroom_store::loadLiveConsoleConfig(fs_json)) {
		// File missing -- 204 with empty body. Distinct from 500 so
		// the harness can assert "fresh device, no config yet" vs
		// "FS broken".
		req->send(204);
		return;
	}
	// Ship raw bytes. Use the len-aware send() overload so we don't
	// have to guarantee a trailing NUL (loadLiveConsoleConfig does
	// not NUL-terminate). The (const uint8_t*, size_t) overload is
	// the byte-oriented form; the (const char*, size_t) overload
	// doesn't exist in this ESPAsyncWebServer build (it treats const
	// char* as NUL-terminated even when len is given).
	req->send(200, "application/json",
	          reinterpret_cast<const uint8_t*>(fs_json.c_str()),
	          fs_json.size());
}

static void startStaServer() {
	// Wire routes.
	server.on("/", HTTP_GET, onRoot);
	server.on("/script.js", HTTP_GET, onScriptJs);
	// /openapi serves a Swagger UI page that loads /openapi.yaml from
	// the same origin. See plans/openapi.yaml + src/html/openapi.html
	// for the spec source. Same-origin matters -- browsers block the
	// "Try it out" requests when the spec and the API are on different
	// hosts, and the firmware doesn't send CORS headers. The HTML
	// page has a yellow hint banner explaining this.
	server.on("/openapi", HTTP_GET, [](AsyncWebServerRequest* req) {
		req->send(200, "text/html", html_openapi_html);
	});
	server.on("/openapi.yaml", HTTP_GET, [](AsyncWebServerRequest* req) {
		// application/yaml so Swagger UI recognises the content-type.
		// Most swagger-ui builds also accept text/yaml and
		// application/x-yaml; sending the canonical one avoids
		// future drift if Swagger UI tightens its parser.
		req->send(200, "plain/text", html_openapi_yaml);
	});
	server.on("/ledOn", HTTP_GET, onLedOn);
	server.on("/ledOff", HTTP_GET, onLedOff);
	server.on("/flash", HTTP_GET, onFlash);
	server.on("/healthcheck", HTTP_GET, onHealthcheck);
	server.on("/state.json", HTTP_GET, onStateJson);
	server.on("/wifi", HTTP_GET, onWifiJson);
	// Console cycling. GET makes them browser-friendly; the
	// WebSocket command "next" / "prev" hits the same shell
	// functions so the UI's ws-driven path doesn't fork from the
	// HTTP path. See the docstring on onConsoleNext for why both
	// directions return the current index + name in the body.
	server.on("/next", HTTP_GET, onConsoleNext);
	server.on("/prev", HTTP_GET, onConsolePrev);

	// /consoles.json GET: route registration. The handler itself
	// (onConsolesJsonGet) lives in the STA-mode section above so
	// it's a sibling of onStateJson / onConsoleNext / etc. We
	// register it here, alongside the POST registration below.
// /consoles.json POST: accept a JSON body from an external service,
	// validate it via the functional core, save it to LittleFS with two
	// rolling backups, then schedule a reboot so the next boot picks up
	// the new config.
	//
	// Design notes:
	//   - The body is collected via an onBody middleware that fills
	//     request->_tempObject with the raw bytes (then null-terminates
	//     the buffer so we can pass it to ArduinoJson as a C string).
	//     This mirrors the pattern used by AsyncCallbackJsonWebHandler
	//     in this version of ESPAsyncWebServer (see AsyncJson.cpp).
	//     We use the same _tempObject slot but skip the library's
	//     auto-deserialize step so the bytes land on disk verbatim --
	//     re-serializing through ArduinoJson would re-format whitespace
	//     and lose the operator's preferred indentation.
	//   - Content-Length is capped at 8 KB (mirrors the cap in
	//     src/consoleconfig_store.cpp's read path; the example configs
	//     are < 1 KB). Bigger uploads are rejected before we allocate.
	//   - Validation runs BEFORE the FS write. A bad body never
	//     reaches disk; the response carries the typed error string
	//     from the core so the operator can see exactly what was
	//     rejected.
	//   - On success: write, then 200 + tiny "Saved. Rebooting..." page,
	//     then schedule the reboot via g_pendingConsoleConfigRebootAt
	//     (mirrors the /factory-reset pattern in network_loop()).
	//   - On validation failure: 400 + the parser's error string. No
	//     FS write, no reboot.
	//   - On FS failure: 500 + a generic message; the FS may be in a
	//     partial-write state but the boot path falls back through
	//     bak1 -> bak2 -> PROGMEM so the device still boots. The
	//     operator can retry.
	constexpr size_t kMaxBodyBytes = 8 * 1024;
	auto consolesJsonOnBody = [kMaxBodyBytes](AsyncWebServerRequest* req,
	                                          uint8_t* data, size_t len,
	                                          size_t index, size_t total) {
		// Method guard: the same path is registered for both GET and
		// POST. ESPAsyncWebServer fires onBody for any request with a
		// Content-Length header -- including a GET that sent
		// `Content-Length: 0`. Without this guard the GET would write
		// zero bytes into a tempObject, then onRequest would 400 on
		// "empty body" and the GET would 500 instead of returning the
		// live config.
		if (req->method() != HTTP_POST) {
			return;
		}
		// Reject oversized uploads up front so we never allocate a
		// pathologically large buffer. This matches the read-side
		// cap in consoleconfig_store.cpp -- the device can't read
		// more than 8 KB on boot, so accepting more on the way in
		// would silently truncate anyway.
		if (total > kMaxBodyBytes) {
			Serial.print("net: consoles.json body too large (");
			Serial.print((unsigned)total);
			Serial.println(" bytes)");
			req->send(413, "application/json",
			          "{\"error\":\"body exceeds 8 KB cap\"}\n");
			req->abort();
			return;
		}
		// First chunk: allocate the full buffer (calloc gives us a
		// zeroed buffer so the null terminator at offset `total` is
		// already in place for the ArduinoJson C-string parser).
		if (index == 0) {
			if (req->_tempObject != nullptr) {
				// Middleware re-entry on the same request -- shouldn't
				// happen but free defensively so we don't leak.
				free(req->_tempObject);
			}
			req->_tempObject = calloc(total + 1, sizeof(uint8_t));
			if (req->_tempObject == nullptr) {
				Serial.print("net: consoles.json body alloc failed (");
				Serial.print((unsigned)total);
				Serial.println(" bytes)");
				req->send(500, "application/json",
				          "{\"error\":\"body alloc failed\"}\n");
				req->abort();
				return;
			}
		}
		// Subsequent chunks: copy into the pre-allocated buffer.
		// index+len is bounded by total (which we capped above) so
		// no overflow is possible here.
		if (req->_tempObject != nullptr) {
			memcpy((uint8_t*)req->_tempObject + index, data, len);
		}
	};
	auto consolesJsonOnRequest = [](AsyncWebServerRequest* req) {
		// Method guard (defense in depth -- onBody already short-
		// circuits non-POST, but if a future refactor changes the
		// chain order we still want POST-only validation here).
		if (req->method() != HTTP_POST) {
			return;
		}
		// The onBody middleware ran first and stashed the raw body
		// in _tempObject. If it's NULL here, the request either had
		// no body or the middleware aborted it -- either way, we
		// have nothing to validate, so reject.
		if (req->_tempObject == nullptr) {
			req->send(400, "application/json",
			          "{\"error\":\"empty body; expected console-config JSON\"}\n");
			return;
		}
		const char* raw = static_cast<const char*>(req->_tempObject);
		const size_t len = strlen(raw);  // safe -- calloc zeroed offset `total`.

		Serial.print("net: /consoles.json POST body=");
		Serial.print(len);
		Serial.println(" bytes");

		// Step 1: validate via the functional core. Any structural
		// error (missing irCodes, unknown tvInput, bad hex, malformed
		// JSON, empty id) is reported back to the client verbatim
		// from the core's typed error string.
		std::string payload(raw, len);
		retroroom_core::LoadResult parsed;
		if (!retroroom_core::validateConsoleConfigJson(payload, parsed)) {
			Serial.print("net: /consoles.json rejected: ");
			Serial.println(parsed.error.c_str());
			String body_out = String("{\"error\":\"") + parsed.error.c_str() + "\"}\n";
			req->send(400, "application/json", body_out);
			return;
		}
		Serial.print("net: /consoles.json accepted; parsed ");
		Serial.print(parsed.consoles.size());
		Serial.print(" consoles + ");
		Serial.print(parsed.irCodes.size());
		Serial.println(" IR codes");

		// Step 2: write to LittleFS with two rolling backups via the
		// shell-side store wrapper. The wrapper handles rotation
		// policy via the functional core; we just commit whatever
		// the policy produced.
		const retroroom_store::SaveResult saved =
			retroroom_store::saveConsoleConfigWithBackups(payload);
		if (saved == retroroom_store::SaveResult::MountFailed) {
			req->send(500, "application/json",
			          "{\"error\":\"LittleFS mount failed\"}\n");
			return;
		}
		if (saved == retroroom_store::SaveResult::WriteFailed) {
			req->send(500, "application/json",
			          "{\"error\":\"LittleFS write failed (partial state possible); retry the POST\"}\n");
			return;
		}

		// Drop the saved console selection. The stored value is an
		// index into the array the config just replaced, so carrying
		// it across would point at whichever console happens to land
		// at that offset in the new list -- or at nothing, if the new
		// list is shorter. Wiping means the reboot below lands on
		// console 0, which is what an operator uploading a config
		// expects.
		//
		// Best-effort: a failure here is only visible if the device
		// loses power between now and the next selection change, and
		// the boot path clamps an out-of-range value anyway. Not worth
		// failing an otherwise-good upload over.
		if (!retroroom_store::clearLastSelectedConsole()) {
			Serial.println("net: could not clear saved console selection");
		}

		// Step 3: respond, then arm the reboot. Same async-reboot
		// pattern as /factory-reset -- sending the response from the
		// handler, then restarting in network_loop() once the TCP
		// send buffer has drained.
		req->send(200, "application/json",
		          "{\"status\":\"saved\",\"consoles\":" +
		          String((unsigned)parsed.consoles.size()) +
		          ",\"irCodes\":" + String((unsigned)parsed.irCodes.size()) +
		          ",\"message\":\"Rebooting in ~1.5 s\"}\n");

		g_pendingConsoleConfigRebootAt = millis() + kConsoleConfigRebootDelayMs;
		Serial.print("net: /consoles.json reboot scheduled at millis()=");
		Serial.println(g_pendingConsoleConfigRebootAt);
	};
	// GET /consoles.json -- read-back. Same path as the POST below;
	// the POST's onBody middleware has a method guard so a GET with
	// Content-Length: 0 doesn't accidentally write zero bytes into
	// a tempObject. See onConsolesJsonGet above for the response
	// shapes (200 raw bytes / 204 missing / 500 mount failed).
	server.on("/consoles.json", HTTP_GET, onConsolesJsonGet);

	// server.on(uri, method, onRequest) returns a reference to the
	// underlying AsyncCallbackWebHandler so we can chain an onBody
	// middleware onto it. This is the same pattern AsyncJson.cpp
	// uses internally to register its body-collecting middleware.
	AsyncCallbackWebHandler& consoles_h =
		server.on("/consoles.json", HTTP_POST, consolesJsonOnRequest);
	consoles_h.onBody(consolesJsonOnBody);

	// /factory-reset: two-step wipe + reboot with a one-shot CSRF nonce
	// to prevent accidental re-trigger.
//
// Why this exists: a previous two-step version had GET /factory-reset
// serve a confirmation form and POST /factory-reset do the wipe. That
// *should* have been enough, but the operator found a way to trigger
// it accidentally: they had the confirmation page open in a browser
// tab, navigated away, then came back to the network. The browser
// auto-resubmitted the form's POST, wiping the credentials. The
// original goal of the two-step design -- "no stray GET can wipe" --
// was defeated because the browser was smart enough to re-POST.
//
// Fix: the GET response embeds a freshly-generated random nonce in
// the form. The POST handler compares the submitted nonce to the
// last issued one (stored in module-local g_factoryResetNonce, see
// the anonymous namespace above) and only proceeds if they match.
// Auto-resubmitted forms from a stale tab still have the nonce from
// the page they originally rendered, but the nonce on the server
// has been rotated -- so the comparison fails and the POST is
// rejected.
//
// Additional hardening: the GET response is sent with
// `Cache-Control: no-store` so the browser doesn't try to serve the
// form from its disk cache and resurrect the nonce on its own.
	server.on("/factory-reset", HTTP_GET, [](AsyncWebServerRequest* req) {
		// Generate a fresh nonce. micros() + a static salt is enough
		// entropy for a non-security-critical UI confirm; the goal
		// is just to make stale POST submissions invalid, not to
		// thwart a determined attacker.
		const uint32_t nonce = micros() ^ 0xA5A5A5A5;
		g_factoryResetNonce.store(nonce);

		// Inject the nonce into the prompt page (the `__NONCE__`
		// placeholder in src/html/factory-reset.html) and serve it.
		String page = html_factory_reset_html;
		page.replace("__NONCE__", String((unsigned long)nonce, 16));

		AsyncWebServerResponse* resp = req->beginResponse(200, "text/html", page);
		// Block all caching. A cached copy of this page could be
		// re-rendered by the browser after a navigation, in which
		// case the embedded nonce might match a later server-side
		// rotation by accident -- no-store guarantees the page is
		// never served from disk cache.
		resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		resp->addHeader("Pragma", "no-cache");
		req->send(resp);
	});
	server.on("/factory-reset", HTTP_POST, [](AsyncWebServerRequest* req) {
		// Reject any POST that doesn't echo the nonce we last
		// served. Auto-resubmitted forms from a stale tab have a
		// nonce that no longer matches; preflighted requests from
		// bookmarks, link previewers, or anything that doesn't
		// render the GET page first have no nonce at all.
		if (!req->hasParam("nonce", true)) {
			Serial.println("net: /factory-reset POST rejected: missing nonce");
			req->send(400, "text/plain",
			          "factory-reset requires the nonce from the confirmation page. "
			          "Open /factory-reset in a browser and click the red button.\n");
			return;
		}
		const String submitted = req->getParam("nonce", true)->value();
		const uint32_t expected = g_factoryResetNonce.load();
		const uint32_t submittedNum = strtoul(submitted.c_str(), nullptr, 16);
		if (submittedNum != expected) {
			Serial.print("net: /factory-reset POST rejected: nonce mismatch (got 0x");
			Serial.print(submitted);
			Serial.print(", expected 0x");
			Serial.print(String((unsigned long)expected, 16));
			Serial.println(")");
			req->send(400, "text/plain",
			          "factory-reset nonce did not match. The confirmation "
			          "page may have expired; reload /factory-reset and try again.\n");
			return;
		}
		// Rotate the nonce AFTER a successful match so the same form
		// can't be re-submitted by a browser-back-then-forward.
		g_factoryResetNonce.store(0);

		Serial.println("net: /factory-reset POST -- nonce OK, wiping /wifi.json and scheduling reboot");
		if (LittleFS.begin()) {
			LittleFS.remove(kWifiConfigPath);
			// Also drop the saved console selection. It's device state
			// that names a slot in the operator's cabinet, so a device
			// handed to someone else shouldn't boot into the previous
			// owner's last selection. Routed through the store wrapper
			// rather than a raw remove() so the path constant stays in
			// one place.
			retroroom_store::clearLastSelectedConsole();
		}

		// Serve the post-reset confirmation page. The auto-refresh
		// link uses a RELATIVE URL ("/") so it resolves to whatever
		// hostname / IP the operator's browser is currently looking
		// at. After the reboot the device will be on SoftAP
		// (192.168.4.1) and the user's laptop will (a) likely still
		// be associated with the old network, so an absolute
		// "http://192.168.4.1/" link would trigger the captive-
		// portal probe on the *old* network and 404 there, and
		// (b) the browser may have already cached the hostname as
		// "retroroom.local" via mDNS, so the relative "/" is the
		// robust target.
		AsyncWebServerResponse* resp = req->beginResponse(200, "text/html", html_factory_reset_done_html);
		// Same no-store directive as the GET -- we don't want the
		// browser to cache this success page; the next time it
		// loads /factory-reset it must re-render the confirmation
		// form (and get a fresh nonce).
		resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		resp->addHeader("Pragma", "no-cache");
		req->send(resp);

		// Schedule the reboot kFactoryResetRebootDelayMs from now.
		// Do NOT call rp2040.restart() in the handler: req->send()
		// is asynchronous on AsyncWebServer (it queues the response
		// onto the lwIP TCP context) and a 500 ms delay in the
		// handler was racing the send buffer. The user would see a
		// spinner until the browser timed out instead of the
		// success message. network_loop() now handles the reboot from
		// the main loop context, after the response has had time
		// to drain.
		g_pendingFactoryResetRebootAt = millis() + kFactoryResetRebootDelayMs;
		Serial.print("net: /factory-reset reboot scheduled at millis()=");
		Serial.println(g_pendingFactoryResetRebootAt);
	});

	ws.onEvent(onWsEvent);
	server.addHandler(&ws);

	server.begin();
	Serial.println("net: AsyncWebServer running on port 80");
}

// ---------- WebSocket handler ----------

static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* cli,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
	switch (type) {
		case WS_EVT_CONNECT:
			wsClientConnected = true;
			Serial.print("net: ws client #");
			Serial.print(cli->id());
			Serial.println(" connected");
			// Greet the new client with a snapshot of current state.
			cli->text("ws_init_ack");
			break;
		case WS_EVT_DISCONNECT:
			wsClientConnected = false;
			Serial.print("net: ws client #");
			Serial.print(cli->id());
			Serial.println(" disconnected");
			break;
		case WS_EVT_DATA: {
			AwsFrameInfo* info = (AwsFrameInfo*)arg;
			if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
				// Treat incoming text as a command. Mirrors the HTTP
				// /ledOn / /ledOff / /flash / /healthcheck endpoints so
				// the websocket-driven UI (script.js) and the
				// HTTP-driven UI do the same thing.
				std::string msg((const char*)data, len);
				Serial.print("net: ws rx: ");
				Serial.println(msg.c_str());
				if (msg == "ledOn") {
					ledOn();
				} else if (msg == "ledOff") {
					ledOff();
				} else if (msg == "flash") {
					// flashLed() already toggles `flash`. The HTTP
					// /flash handler used to do `flash = !flash`
					// after flashLed(), which double-toggled and
					// made the WS flash path look like a no-op.
					flashLed();
				} else if (msg == "next") {
					// Empty-vector guard: a WS-driven /next on a device
					// with no consoles loaded would otherwise UB inside
					// CurrentConsole() (via the WS broadcast that fires
					// after the advance). Broadcast a noop marker so
					// connected UIs can keep their state in sync without
					// polling.
					if (consoles.empty()) {
						cli->text("console:noop");
						break;
					}
					// Same shell function as HTTP /next. The "console:..."
					// broadcast inside advanceConsole() fans out to all
					// connected clients, so the originating socket sees
					// the change in the same message stream.
					advanceConsole();
				} else if (msg == "prev") {
					if (consoles.empty()) {
						cli->text("console:noop");
						break;
					}
					rewindConsole();
				} else if (msg == "healthcheck") {
					// No hardware side effect, just ack so the client
					// knows we're alive.
				} else {
					// Unknown -- echo so the client sees the round-trip.
					std::string reply = std::string("echo: ") + msg;
					cli->text(reply.c_str(), reply.size());
					break;
				}
				// Broadcast the new state to every connected client so
				// the originating tab and any others stay in sync.
				broadcastSocketMessage(msg);
			}
			break;
		}
		case WS_EVT_ERROR:
		case WS_EVT_PONG:
			break;
	}
}

// ---------- entry point ----------

void network_init() {
	Serial.println("net: network_init()");

	// Sanity-check the CYW43 module is alive. If WiFi.status() is
	// WL_NO_MODULE the CYW43 firmware didn't load.
	if (WiFi.status() == WL_NO_MODULE) {
		Serial.println("net: CYW43 module not responding!");
		return;
	}

	String ssid, pass;
	bool haveCreds = loadWifiCreds(ssid, pass);

	if (!haveCreds) {
		startApPortal();
		return;
	}

	Serial.print("net: connecting to \"");
	Serial.print(ssid);
	Serial.println("\"");
	WiFi.mode(WIFI_STA);
	// Set the DHCP hostname before WiFi.begin() so the mDNS / router
	// shows "RetroRoom" (not "PicoW") on the LAN. The CYW43 default
	// is "PicoW" on Pico-W variants and similar on Pico 2 W.
	WiFi.setHostname(kHostname);
	WiFi.begin(ssid.c_str(), pass.c_str());

	unsigned long start = millis();
	while (WiFi.status() != WL_CONNECTED && (millis() - start) < kStaTimeoutSec * 1000UL) {
		delay(500);
		Serial.print(".");
	}
	Serial.println();

	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("net: STA connect timed out; falling back to SoftAP");
		startApPortal();
		return;
	}

	inApMode  = false;
	networkUp = true;
	Serial.print("net: connected, IP = ");
	Serial.println(WiFi.localIP());
	Serial.print("net: RSSI = ");
	Serial.print(WiFi.RSSI());
	Serial.println(" dBm");

	// Make sure the captive-portal DNS server isn't running. It's only
	// started by startApPortal() so this is a no-op in the normal STA
	// flow, but defensive against a future STA->AP fallback transition.
	dnsServer.stop();

	startStaServer();
}

// Blank the operator-facing outputs so a pending reboot reads as a
// deliberate shutdown rather than the cabinet freezing mid-frame:
//   - the LCD shows a fixed "Rebooting" on line 1
//   - the second FastLED strip (GP21, SELECTED_CONSOLE_LED_STRING_DATA)
//     goes fully dark
//
// Main-loop context only -- never call this from a request handler.
// The PIO state machine and the I2C bus are both shared with whatever
// else the loop is driving, and this is always followed by a hard
// reset, so there's nothing to gain from running it any earlier in
// the reboot window.
//
// Each output degrades to a no-op on a build that doesn't have it:
// display.h supplies inline stubs without HAS_LCD, and ledstring.h
// declares nothing without HAS_LEDS.
static void showRebootingState() {
	display_show_status("Rebooting", "");
#if defined(HAS_LEDS)
	ledstring_allOff();
#endif
	Serial.println("net: outputs blanked for pending reboot");
	// Let the frame actually reach the panel and the wire before the
	// reset truncates it. See kRebootOutputSettleMs.
	delay(kRebootOutputSettleMs);
}

void network_loop() {
	// Auto-restart the DNS catch-all if the debug suspension has
	// expired. The operator hit /debug/dns-off, the 60 s window is up,
	// we put things back the way they were. Only does work while the
	// AP is up. Don't flip WiFi mode -- that would clobber the CYW43
	// state and break subsequent /scan.json calls.
	if (dnsSuspendedAt && (millis() - dnsSuspendedAt) >= 60000UL) {
		IPAddress ip = WiFi.softAPIP();
		dnsServer.start(53, "*", ip);
		dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
		dnsSuspendedAt = 0;
		Serial.println("net: dns catch-all auto-restarted after 60 s");
	}
	dnsServer.processNextRequest();

	// Pending /factory-reset reboot. The POST handler sent the
	// "Resetting now!" page, then scheduled this; we trigger the
	// restart from the main loop so the TCP send buffer has time
	// to drain before we go down. Doing the restart in the request
	// handler was racy -- the user saw a spinner until the browser
	// timed out instead of the success message.
	//
	// The `if (const unsigned long deadline = g_pendingFactoryResetRebootAt)`
	// form gives a compiler-checked non-zero guard (the variable only
	// exists in scope when the flag is non-zero), preventing the
	// `(long)(millis() - 0)` fire-on-zero edge case.
	if (const unsigned long deadline = g_pendingFactoryResetRebootAt) {
		if ((long)(millis() - deadline) >= 0) {
			g_pendingFactoryResetRebootAt = 0;
			Serial.println("net: factory-reset reboot firing now");
			rp2040.restart();
		}
	}

	// Pending /consoles.json reboot. Same shape as the factory-reset
	// branch above: the POST handler saved the new config + sent the
	// response + armed this deadline; we restart from main-loop
	// context once the TCP send buffer has drained. Separate flag so
	// a race with /factory-reset (theoretical -- /factory-reset has
	// its own CSRF nonce gate) can't lose this one.
	//
	// showRebootingState() is called here rather than in the POST
	// handler: the response has had its full 1.5 s window to drain by
	// now, so painting at the fire site means the "Rebooting" message
	// and the dark strip are up for exactly as long as the device is
	// still running, not for a window that mostly overlaps the TCP
	// grace period.
	if (const unsigned long deadline = g_pendingConsoleConfigRebootAt) {
		if ((long)(millis() - deadline) >= 0) {
			g_pendingConsoleConfigRebootAt = 0;
			Serial.println("net: /consoles.json reboot firing now");
			showRebootingState();
			rp2040.restart();
		}
	}
}

#endif  // HAS_WIFI

