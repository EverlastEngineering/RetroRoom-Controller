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
//   4. If creds are present, WiFi.begin(ssid, pass) and return. The join
//      itself is not waited for: network_loop() polls for it, and when
//      the radio either associates or runs out of time it calls
//      finishStaBringup() -- the rest of what this function used to do
//      after a blocking while-loop. The cabinet is fully live (knob,
//      strip, ring, LCD, menu) throughout the wait.
//   5. Both SoftAP entry points put a two-line notice on the LCD for
//      two seconds, unless `network.showWIFIConnectionFailureMessage`
//      is off. Different wording per case: a saved network that would
//      not join, versus no saved network at all.
//   6. Once connected, AsyncWebServer serves /, /script.js, /ledOn,
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
// For the console list (CurrentConsole() to repaint the panel after the
// WiFi-failure notice) and for the `network` config setting the notice
// is gated on. Explicit rather than inherited through main.h: this
// file uses both, and a transitive include is not a dependency.
#include "consoles.h"
#include "serialconfig.h"
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

// What the two SoftAP entry points say, and the second line of the
// unconfigured one.
//
// The hold is LCD_NOTICE_MS, in display.h, and deliberately not a
// number of its own: these pages and the "not set up yet" pages shown
// at boot are the same kind of thing, and they should last the same
// length of time.
//
// Two messages, not one, because there are two genuinely different
// situations and only one of them is a failure:
//
//   - We had a saved network and could not reach it. The radios work;
//     the router is not answering. The operator's problem is
//     "where did it go", so the line names the check to run.
//   - We have no saved network at all -- a fresh cabinet, or one that
//     was factory-reset. Nothing has failed here; the operator's
//     problem is "how do I set it up", so the second line is the SSID
//     verbatim. An operator told to look for one name who finds
//     another has been sent looking for something that does not exist.
//
// The second line of the unconfigured message is kApSsid itself rather
// than a copy of it, so it cannot drift from the SSID the radio is
// actually advertising. Every line here has to fit LCD_COLS without
// scrolling -- display_show_status() pins both rows and an over-long
// line is simply cut off -- so this is checked by eye, and the SSID
// sitting one character under the limit is the one that will break
// first if the AP is ever renamed.
constexpr const char* kWifiJoinFailedLine1 = "Wifi Join Failed";
constexpr const char* kWifiJoinFailedLine2 = "Check Network!";
constexpr const char* kWifiUnconfiguredLine1 = "WIFI Not Set Up";

// The WiFi bring-up is a two-phase state machine, and the phase is
// what lets the cabinet be usable while it runs.
//
// Phase 1 (network_init) reads the credentials, calls WiFi.begin() and
// returns. Phase 2 (network_loop, the first thing it does) polls the
// status the same condition the old blocking loop used, and on
// either exit -- connected, or out of time -- calls
// finishStaBringup(), which is the rest of what the old function did
// after the loop.
//
// It used to be one function with `while (WiFi.status() !=
// WL_CONNECTED && elapsed < 20 s) delay(500);` in the middle, which
// held the entire cabinet -- knob, strip, ring, LCD, menu -- for up to
// twenty seconds on every boot where the router was slow or absent.
// Ten was typical. That is a long time to be told nothing, and the
// wait bought nothing: the CYW43 associates in the background either
// way, and every line after the loop was reachable from a loop tick.
enum class BringupPhase : uint8_t {
    Idle,        // network_init() has not run, or found no module
    Connecting,  // WiFi.begin() has been called; the join is in flight
    Resolved,    // the bring-up is over, for whatever reason
};
BringupPhase bringupPhase = BringupPhase::Idle;
// millis() when WiFi.begin() was called. The timeout is measured from
// here rather than from "whenever the first loop tick happened to run",
// so a slow boot does not eat into the window the router was given.
unsigned long staConnectStartedAtMs = 0;
// When the last progress dot went out, so the dots are spaced by time
// rather than by loop iteration -- a fast loop would otherwise print
// a dot every tick.
unsigned long staConnectLastDotMs = 0;
// Non-zero while a WiFi notice is on the panel; holds the deadline at
// which the live view comes back. See LCD_NOTICE_MS in display.h.
unsigned long wifiNoticeUntilMs = 0;

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
static void showWifiNotice(const char* line1, const char* line2);
static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* cli,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);

// Per-loop pump for the DNS server. Only does work while the AP is
// running (inApMode). Call from the main loop after setup().
void network_loop();

// ---------- public API ----------

bool network_isUp() { return networkUp; }
bool network_inStaMode() { return networkUp && !inApMode; }

// True when the radio was never started, or was started and could not
// reach a saved network and is now on its own access point. False
// while a join is still in flight, and false once it has joined.
//
// The distinction from `!network_inStaMode()` is the whole point of
// this function, and the first version got it wrong. At the end of
// setup() a cabinet with perfectly good credentials whose router is
// merely slow has not joined *yet*, so `!network_inStaMode()` is true
// for it -- and asking that question to decide whether to interrupt
// the operator opened the session on a healthy device every single
// boot. "Has not managed to join" and "has not finished trying" are
// different states and only the first one is a problem.
//
// What is true is not quite "unreachable": on the SoftAP someone can
// still reach /setup. What they cannot reach is /consoles.json, which
// is not routed there at all -- onNotFound redirects everything to
// the setup form. That is the question the caller is really asking.
bool network_isUnreachable() { return networkDisabled || inApMode; }

// True when `network.disable` turned the radio off, which is a third
// state rather than a flavour of "not up". It has to be distinguishable
// from SoftAP mode because the two look identical from here -- both
// leave network_isUp() false -- and the LED and the heartbeat both use
// the difference to avoid reporting a fault the operator asked for.
//
// The three above are the whole of "what state is the network in", and
// they are deliberately three questions rather than one enum. Every one
// of them has been wrong at least once, in a different direction: two
// of them because two states looked alike, and one because "not yet"
// and "not going to" are not the same answer.
bool network_disabled() { return networkDisabled; }

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
	// A cabinet with the radio switched off has nothing to scan for,
	// and WiFi.scanNetworks() is a blocking 2-4 s. The check lives
	// here rather than at the call site in main.cpp so that "disabled"
	// is decided in exactly one place -- the two functions below are
	// the only ones that touch the radio, and both ask.
	//
	// main.cpp still calls this unconditionally, and that is
	// deliberate: a call site that had to know about the setting is a
	// call site that can get the ordering wrong.
	if (networkDisabled) {
		Serial.println("net: network disabled by config; skipping scan");
		return;
	}
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
	// A third value alongside "sta" and "ap". "off" is not a flavour
	// of "ap": the radio was never started, so a caller that saw "ap"
	// would go looking for a SoftAP to connect to and find the device
	// had deliberately not put one up.
	doc["mode"]    = networkDisabled ? "off" : (inApMode ? "ap" : "sta");
	String out;
	serializeJson(doc, out);
	req->send(200, "application/json", out);
}

static void onWifiJson(AsyncWebServerRequest* req) {
	StaticJsonDocument<256> doc;
	doc["mode"] = networkDisabled ? "off" : (inApMode ? "ap" : "sta");
	if (networkDisabled) {
		// No IP and no SSID, because there is no interface to have
		// either. Reporting the SoftAP SSID here would be a lie on a
		// device that is not running one.
		doc["ip"]   = "";
		doc["ssid"] = "";
	} else if (inApMode) {
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

		Serial.println("net: /factory-reset POST -- nonce OK, wiping all state and scheduling reboot");
		// Every file the cabinet keeps, gone -- live config, both
		// backups, the saved selection, and the credentials. Routed
		// through the store's one wipe so this and the serial RESET
		// cannot drift into disagreeing about what a factory reset
		// is; see wipeEverything() for why the backups have to go too.
		if (!retroroom_store::wipeEverything()) {
			Serial.println("net: factory-reset -- LittleFS unavailable or a file survived");
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

// The tail of the bring-up, run from network_loop() once the join has
// either succeeded or run out of time. Everything it does is what the
// second half of the old network_init() did, unchanged -- the split
// moved *when* this runs, not what it decides.
static void finishStaBringup() {
	bringupPhase = BringupPhase::Resolved;

	// The same condition the blocking loop exited on, asked once more
	// from main-loop context. A radio can associate between the last
	// poll and this call, and preferring the success path means a
	// cabinet that made it in time serves the UI rather than silently
	// dropping to an access point.
	if (WiFi.status() == WL_CONNECTED) {
		inApMode  = false;
		networkUp = true;
		Serial.print("net: joined after ");
		Serial.print(millis() - staConnectStartedAtMs);
		Serial.print(" ms, IP = ");
		Serial.println(WiFi.localIP());
		Serial.print("net: RSSI = ");
		Serial.print(WiFi.RSSI());
		Serial.println(" dBm");

		// Make sure the captive-portal DNS server isn't running. It's
		// only started by startApPortal() so this is a no-op in the
		// normal STA flow, but defensive against a future STA->AP
		// fallback transition.
		dnsServer.stop();

		startStaServer();
		return;
	}

	Serial.println("net: STA connect timed out; falling back to SoftAP");
	showWifiNotice(kWifiJoinFailedLine1, kWifiJoinFailedLine2);
	startApPortal();
	// The web API is not reachable from here -- onNotFound redirects
	// everything on the AP to /setup, so /consoles.json is not even
	// routed. If the operator is going to fix this, the cable is what
	// they have, so open the session rather than waiting for an `i`
	// they have no reason to know about.
	//
	// Deliberately not done at setup() time. A cabinet with good
	// credentials whose router takes fifteen seconds is *on its way* to
	// being fine, and opening a session on it would be a guess; by the
	// time this runs, the guess has been settled.
	serialcmd_maybeAutoEnter();
}

void network_init() {
	Serial.println("net: network_init()");

	// The radio stays off. Checked before anything else here, and
	// before WiFi.status() -- asking the CYW43 anything is a radio
	// access, and the whole point of the setting is that there is
	// none.
	//
	// No SoftAP either. Falling back to one would leave the cabinet
	// advertising a network nobody asked for, holding a DHCP lease and
	// answering a captive portal, which is the thing being turned off.
	if (networkDisabled) {
		Serial.println("net: network disabled by config; radio left off");
		// Resolved, not a phase of its own. Nothing in network_loop()
		// does anything differently for a cabinet that never started
		// the radio than for one that finished starting it, and a
		// value nothing branches on is a value that will quietly go
		// stale. "Is the radio disabled" is answered by
		// network_disabled(), which reads the setting rather than
		// reconstructing it from here.
		bringupPhase = BringupPhase::Resolved;
		return;
	}

	// Sanity-check the CYW43 module is alive. If WiFi.status() is
	// WL_NO_MODULE the CYW43 firmware didn't load.
	if (WiFi.status() == WL_NO_MODULE) {
		Serial.println("net: CYW43 module not responding!");
		bringupPhase = BringupPhase::Idle;
		return;
	}

	String ssid, pass;
	bool haveCreds = loadWifiCreds(ssid, pass);

	// No credentials is not a slow case, it is a decision, and the
	// decision needs no waiting: the SoftAP comes up now and the
	// cabinet carries on being a cabinet.
	//
	// It does still get said out loud. Without this the cabinet is on
	// its own access point with nothing on the panel to say so, and
	// from the front of the cabinet that is the same picture as a
	// cabinet that is switched off -- which is the confusion this
	// notice exists to remove, and a fresh device is the case that
	// needs it most.
	if (!haveCreds) {
		bringupPhase = BringupPhase::Resolved;
		showWifiNotice(kWifiUnconfiguredLine1, kApSsid);
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

	// Hand the rest to the main loop. WiFi.begin() has already put the
	// CYW43 into associating; from here the radio does its work on its
	// own schedule and every line after the old blocking loop was
	// reachable from a loop tick. What this buys is that the knob, the
	// strip, the ring, the LCD and the menu are all live while the
	// router is being found.
	staConnectStartedAtMs = millis();
	staConnectLastDotMs  = staConnectStartedAtMs;
	bringupPhase         = BringupPhase::Connecting;
	Serial.println("net: joining in the background; the cabinet is live while we wait");
}

// Put a two-line WiFi notice on the panel for LCD_NOTICE_MS, unless
// the operator has turned that off. Called from both SoftAP entry
// points, which are in different functions and have genuinely
// different things to say -- see the constants above.
//
// It used to be called from the timeout path only, and a factory-reset
// cabinet therefore said nothing at all while sitting on its own access
// point. That is the exact confusion the setting was added to remove:
// from the front of the cabinet, "on a network nobody can name" and
// "switched off" are the same picture.
//
// Gated on `network.showWIFIConnectionFailureMessage` rather than
// hard-wired, because a cabinet on a bench that is permanently
// unconfigured would otherwise say this at every single boot, and a
// message that is always there stops being read. Default is on.
//
// The restore is a deadline rather than a delay(): the cabinet is
// usable again the moment the SoftAP is up, and blocking here would
// re-introduce the very stall this whole change is about. network_loop
// puts the live view back when the deadline passes.
//
// display_show_status() is an inline no-op on a build without an LCD
// (display.h), so this needs no HAS_LCD of its own -- which is right,
// because the *decision* to fall back to SoftAP has nothing to do with
// whether there is a panel to tell anyone about it on.
static void showWifiNotice(const char* line1, const char* line2) {
	if (!showWifiConnectionFailureMessage) {
		Serial.println("net: WiFi notice suppressed by config");
		return;
	}
	// Logged even when the panel took it, and deliberately so. "I didn't
	// see the message" is a question about two different things -- the
	// code decided to show it, or the code decided to show it and the
	// panel never did -- and the answer is a line either way. Whether
	// the LCD was actually found at boot is reported separately by
	// display_init(), so between the two there is no case this leaves
	// unexplained.
	Serial.print("net: notice \"");
	Serial.print(line1);
	Serial.print("\" / \"");
	Serial.print(line2);
	Serial.println("\"");
	display_show_status(line1, line2);
	wifiNoticeUntilMs = millis() + LCD_NOTICE_MS;
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
	// The other half of network_init(). First thing in the loop, and
	// the only thing here that touches WiFi.status().
	//
	// This is the old blocking loop's exit condition, inverted: the
	// `while` used to keep going while the radio had *not* associated
	// and time was left, and the body slept 500 ms per pass. Here the
	// loop keeps going while the radio has *not* associated and time is
	// left -- the same predicate, evaluated without the sleep -- and
	// hands the decision to finishStaBringup() the moment it is false.
	//
	// Nothing else in the loop has to know this exists, and nothing
	// before it in loop() is gated on it: the LCD, the strip, the ring
	// and the menu all run whether the radio has found a router yet or
	// not, which is the entire point.
	if (bringupPhase == BringupPhase::Connecting) {
		if (WiFi.status() == WL_CONNECTED) {
			Serial.println();
			finishStaBringup();
		} else if ((unsigned long)(millis() - staConnectStartedAtMs) >=
		           (unsigned long)kStaTimeoutSec * 1000UL) {
			Serial.println();
			finishStaBringup();
		} else if ((unsigned long)(millis() - staConnectLastDotMs) >= 2000UL) {
			// The old loop printed a dot every 500 ms; this is every
			// two seconds, because the loop body is now every few
			// milliseconds and a dot per tick would be a dot-fest.
			staConnectLastDotMs = millis();
			Serial.print(".");
			Serial.flush();
		}
	}

	// The WiFi notice has had its two seconds. Put the live view
	// back.
	//
	// display_show_status() holds whatever it was given until the next
	// display_show_console(), so something has to undo it or the
	// cabinet sits on a message about a network problem that was
	// resolved -- or, more to the point, on one that is still true
	// (the cabinet is on its own AP) but which the operator has long
	// since read. Repainting from CurrentConsole() is the same call
	// selectConsole() makes, so the panel lands on the same thing it
	// would have shown had the notice never happened.
	//
	// Guarded on a non-empty console list: with no config uploaded,
	// CurrentConsole() dereferences operator[] on a zero-size vector,
	// and a boot with neither WiFi nor a console config is a real
	// state for a factory-fresh device.
	if (wifiNoticeUntilMs && (long)(millis() - wifiNoticeUntilMs) >= 0) {
		wifiNoticeUntilMs = 0;
		if (!consoles.empty()) {
			display_show_console(CurrentConsole().name.c_str(),
			                     CurrentConsole().tagline.c_str());
		}
	}

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

