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

#include "html.h"
#include "state.h"
#include "main.h"

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

static void onStateJson(AsyncWebServerRequest* req) {
	StaticJsonDocument<256> doc;
	doc["index"]   = currentConsoleIndex;
	doc["name"]    = CurrentConsole().name.c_str();
	doc["ledOn"]   = statusLedActive != 0x0;
	doc["flash"]   = flash;
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

static void startStaServer() {
	// Wire routes.
	server.on("/", HTTP_GET, onRoot);
	server.on("/script.js", HTTP_GET, onScriptJs);
	server.on("/ledOn", HTTP_GET, onLedOn);
	server.on("/ledOff", HTTP_GET, onLedOff);
	server.on("/flash", HTTP_GET, onFlash);
	server.on("/healthcheck", HTTP_GET, onHealthcheck);
	server.on("/state.json", HTTP_GET, onStateJson);
	server.on("/wifi", HTTP_GET, onWifiJson);

	// /factory-reset: two-step wipe + reboot. A GET serves a prompt page
	// asking the operator to confirm; a POST does the actual wipe. This
	// prevents the legacy GET-immediately behaviour where a stray
	// browser prefetch, link previewer, or fat-fingered bookmark could
	// silently wipe the saved credentials. After reboot the device
	// boots into SoftAP / captive-portal mode (no creds). Useful when
	// the operator typos a password or wants to move the device to a
	// different wifi network without reflashing.
	server.on("/factory-reset", HTTP_GET, [](AsyncWebServerRequest* req) {
		const char* page =
			"<html><head><meta name=\"viewport\" "
			"content=\"width=device-width,initial-scale=1\"><title>Reset "
			"RetroRoom?</title>"
			"<style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe "
			"UI,sans-serif;max-width:420px;margin:3em auto;padding:0 "
			"1em;color:rgb(34,34,34);line-height:1.4}"
			"h2{margin:0 0 .25em}p{color:rgb(68,68,68)}"
			"button{padding:.7em 1.4em;font-size:1em;border:none;border-radius:"
			"6px;color:white;cursor:pointer;font-weight:600}"
			"button.yes{background:rgb(200,40,40)}button.no{background:rgb(170,"
			"170,170);margin-left:.5em}</style></head><body>"
			"<h2>Factory reset?</h2>"
			"<p>This will erase the saved WiFi credentials and reboot into "
			"the setup portal. The device will need to be reconfigured "
			"before it can join your network again.</p>"
			"<form method=\"POST\" action=\"/factory-reset\">"
			"<button type=\"submit\" class=\"yes\">Yes, erase and reboot</button>"
			"</form>"
			"<form method=\"GET\" action=\"/\" style=\"margin-top:.5em\">"
			"<button type=\"submit\" class=\"no\">Cancel</button>"
			"</form>"
			"</body></html>";
		req->send(200, "text/html", page);
	});
	server.on("/factory-reset", HTTP_POST, [](AsyncWebServerRequest* req) {
		Serial.println("net: /factory-reset POST -- wiping /wifi.json and rebooting");
		if (LittleFS.begin()) {
			LittleFS.remove(kWifiConfigPath);
		}
		req->send(200, "text/html",
		          "<html><body style=\"font-family:sans-serif;text-align:center;margin-top:4em\">"
		          "<h2>Reset.</h2><p>Rebooting into setup mode&hellip;</p>"
		          "</body></html>");
		delay(500);
		rp2040.restart();
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
}

#endif  // HAS_WIFI

