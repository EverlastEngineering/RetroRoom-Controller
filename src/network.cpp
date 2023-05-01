#include "network.h"
#include "html.h"
#include "status.h"

AsyncWebServer server(80);
DNSServer dns;

void initializeNetwork() {
	routes();
	AsyncWiFiManager wifiManager(&server, &dns);
	const char *hostname = "RetroRoom";
	WiFi.hostname(hostname);
	wifiManager.autoConnect(hostname);
	server.begin();
}

void notFound(AsyncWebServerRequest *request) {
	request->send(404, "text/plain", "Not found");
}

void routes() {
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/html", html_index_html);
	});

	server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "application/javascript", html_script_js);
	});

	server.on("/ledOn", HTTP_GET, [](AsyncWebServerRequest *request) {
		ledOn();
		digitalWrite(LED_BUILTIN, LOW);
		request->send(200, "text/plain", "on");
	});

	server.on("/flash", HTTP_GET, [](AsyncWebServerRequest *request) {
		flash = true;
		request->send(200, "text/plain", "flash");
	});

	server.on("/ledOff", HTTP_GET, [](AsyncWebServerRequest *request) {
		flash = false;
		ledOff();
		request->send(200, "text/plain", "off");
	});

	server.onNotFound(notFound);
}

