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

		String page;
		page.reserve(900);
		page += "<!DOCTYPE html><html><head>";
		page += "<meta name=\"viewport\" "
		        "content=\"width=device-width,initial-scale=1\">";
		page += "<title>Reset RetroRoom?</title>";
		page += "<style>body{font-family:-apple-system,BlinkMacSystemFont,"
		        "Segoe UI,sans-serif;max-width:420px;margin:3em auto;"
		        "padding:0 1em;color:rgb(34,34,34);line-height:1.4}";
		page += "h2{margin:0 0 .25em}p{color:rgb(68,68,68)}";
		page += "button{padding:.7em 1.4em;font-size:1em;border:none;"
		        "border-radius:6px;color:white;cursor:pointer;font-weight:600}";
		page += "button.yes{background:rgb(200,40,40)}button.no{background:"
		        "rgb(170,170,170);margin-left:.5em}</style>";
		page += "</head><body>";
		page += "<h2>Factory reset?</h2>";
		page += "<p>This will erase the saved WiFi credentials and reboot "
		        "into the setup portal. The device will need to be "
		        "reconfigured before it can join your network again.</p>";
		page += "<form method=\"POST\" action=\"/factory-reset\">";
		page += "<input type=\"hidden\" name=\"nonce\" value=\"";
		page += String((unsigned long)nonce, 16);
		page += "\">";
		page += "<button type=\"submit\" class=\"yes\">Yes, erase and reboot</button>";
		page += "</form>";
		page += "<form method=\"GET\" action=\"/\" style=\"margin-top:.5em\">";
		page += "<button type=\"submit\" class=\"no\">Cancel</button>";
		page += "</form>";
		page += "</body></html>";

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
		}

		// Build a "resetting now" page that includes a link back to
		// the device's eventual SoftAP root once the reboot lands.
		// The link points to the well-known captive-portal IP
		// (192.168.4.1) which the device will be serving on once it
		// comes back up in SoftAP mode -- the user's laptop will
		// likely still be on the old network at this point so a
		// relative "/" would 404. After the device reboots and
		// starts its SoftAP, the operator reconnects to
		// "RetroRoom-Setup" and the link works.
		//
		// The page also includes a small auto-refresh meta tag so
		// the browser will attempt to reconnect on its own once the
		// AP comes back -- helps the operator who walks away and
		// comes back to find the page already showing the new state.
		String page;
		page.reserve(700);
		page += "<!DOCTYPE html><html><head>";
		page += "<meta name=\"viewport\" "
		        "content=\"width=device-width,initial-scale=1\">";
		page += "<meta http-equiv=\"refresh\" content=\"10;url=http://192.168.4.1/\">";
		page += "<title>Resetting&hellip;</title>";
		page += "<style>body{font-family:-apple-system,BlinkMacSystemFont,"
		        "Segoe UI,sans-serif;max-width:420px;margin:3em auto;"
		        "padding:0 1em;color:rgb(34,34,34);line-height:1.4;text-align:center}";
		page += "h2{margin:0 0 .25em}p{color:rgb(68,68,68)}";
		page += "a{color:rgb(10,132,255);font-weight:600;text-decoration:none}";
		page += ".spinner{display:inline-block;width:1.2em;height:1.2em;"
		        "border:.18em solid rgb(136,136,136);border-top-color:transparent;"
		        "border-radius:50%;animation:spin .9s linear infinite;"
		        "vertical-align:middle;margin-right:.5em}";
		page += "@keyframes spin{to{transform:rotate(360deg)}}";
		page += "</style></head><body>";
		page += "<h2>Resetting now!</h2>";
		page += "<p><span class=\"spinner\"></span>WiFi credentials erased. "
		        "The device is rebooting into setup mode&hellip;</p>";
		page += "<p>When the on-board LED blinks steadily, reconnect your "
		        "computer to <code>RetroRoom-Setup</code> and open "
		        "<a href=\"http://192.168.4.1/\">http://192.168.4.1/</a> "
		        "to configure WiFi again.</p>";
		page += "<p style=\"font-size:.85em;color:rgb(136,136,136);margin-top:2em\">"
		        "This page will refresh automatically in 10 seconds.</p>";
		page += "</body></html>";

		AsyncWebServerResponse* resp = req->beginResponse(200, "text/html", page);
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

	// Pending /factory-reset reboot. The POST handler sent the
	// "Resetting now!" page, then scheduled this; we trigger the
	// restart from the main loop so the TCP send buffer has time
	// to drain before we go down. Doing the restart in the request
	// handler was racy -- the user saw a spinner until the browser
	// timed out instead of the success message.
	if (g_pendingFactoryResetRebootAt &&
	    (long)(millis() - g_pendingFactoryResetRebootAt) >= 0) {
		g_pendingFactoryResetRebootAt = 0;
		Serial.println("net: factory-reset reboot firing now");
		rp2040.restart();
	}
}

#endif  // HAS_WIFI

