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
constexpr uint8_t     kStaTimeoutSec = 20;

AsyncWebServer       server(80);
AsyncWebSocket       ws("/ws");
DNSServer            dnsServer;          // Captive-portal DNS: replies to every query
                                         // with the AP's IP so iOS/macOS/Android/Windows
                                         // treat the network as a captive portal.
bool                 wsClientConnected = false;
bool                 networkUp = false;
bool                 inApMode  = false;

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

static const char kSetupHtml[] PROGMEM = R"(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RetroRoom Setup</title></head>
<body style="font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em">
<h2>RetroRoom WiFi Setup</h2>
<p>Enter your WiFi credentials. The device will reboot and join this network.</p>
<form method="POST" action="/setup">
  <p><label>SSID<br><input name="ssid" required style="width:100%;padding:0.5em"></label></p>
  <p><label>Password (leave blank for open networks)<br><input name="pass" type="password" style="width:100%;padding:0.5em"></label></p>
  <p><button type="submit" style="padding:0.6em 1.2em">Save &amp; Reboot</button></p>
</form>
</body></html>)";

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
	WiFi.mode(WIFI_AP);
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

	// WiFi scan endpoint -- synchronous (the only overload Earle's
	// arduino-pico core exposes is scanNetworks(bool async = false)).
	// scanNetworks() clobbers the current WiFi mode; we restore AP mode
	// after the scan so the captive portal keeps working. Results come
	// back as a JSON array of {ssid, rssi, open, channel}.
	server.on("/scan.json", HTTP_GET, [](AsyncWebServerRequest* req) {
		int16_t n = WiFi.scanNetworks(/*async=*/false);
		WiFi.mode(WIFI_AP);
		String out;
		out.reserve(64 + n * 80);
		out += '[';
		for (int16_t i = 0; i < n; i++) {
			if (i) out += ',';
			out += "{\"ssid\":\"";
			String ss = WiFi.SSID(i);
			ss.replace("\"", "\\\"");
			out += ss;
			out += "\",\"rssi\":";
			out += WiFi.RSSI(i);
			// CYW43_AUTH_OPEN == 0 is the same value the Earle WiFi
			// library's encryptionType() returns for an unencrypted
			// network. The legacy ESP8266 "WIFI_AUTH_OPEN" name doesn't
			// exist in arduino-pico's API.
			out += ",\"open\":";
			out += (WiFi.encryptionType(i) == CYW43_AUTH_OPEN) ? "true" : "false";
			out += ",\"channel\":";
			out += WiFi.channel(i);
			out += "}";
		}
		out += ']';
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
	server.on("/setup", HTTP_GET, [](AsyncWebServerRequest* req) {
		req->send(200, "text/html", kSetupHtml);
	});
	server.on("/setup", HTTP_POST, onSetupPost);
	server.onNotFound([](AsyncWebServerRequest* req) {
		// Anything else (including the legacy /script.js, /ledOn, etc.)
		// also redirects to /setup while we're still on the AP.
		req->redirect("/setup");
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
	flashLed();
	flash = !flash;
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
	doc["ledOn"]   = statusLedActive == 0x0;
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
				// Treat incoming text as a command. Echo back the same
				// message prefixed with "echo: " so the JS console.debug
				// in script.js sees a round-trip.
				std::string msg((const char*)data, len);
				Serial.print("net: ws rx: ");
				Serial.println(msg.c_str());
				std::string reply = std::string("echo: ") + msg;
				cli->text(reply.c_str(), reply.size());
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
	// Per-loop pump for the captive-portal DNS server. Only does work
	// while the AP is up; the dnssServer is a no-op otherwise.
	dnsServer.processNextRequest();
}

#endif  // HAS_WIFI

