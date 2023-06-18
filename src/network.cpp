#include <iostream>
#include "network.h"
#include "state.h"
#include "html.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

void network_init() {
	routes();
	AsyncWiFiManager wifiManager(&server, &dns);
	const char *hostname = "RetroRoom";
	WiFi.hostname(hostname);
	wifiManager.autoConnect(hostname);
	server.begin();
	ws.onEvent(onEvent);
  	server.addHandler(&ws);
}

void routes() {
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/html", html_index_html);
	});

	server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "application/javascript", html_script_js);
	});

	server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Redirect");
		response->addHeader("Location", "https://everlastengineering.com/favicon.ico");
		request->send(response);
	});

	server.onNotFound(notFound);
}

void websocketRoutes(uint8_t *message) {
	if (messageIs(message, "ledOn")) ledOn();
	else if (messageIs(message, "ledOff")) { flash = false; ledOff(); }
	else if (messageIs(message, "flash")) flash = !flash;
	else if (messageIs(message, "healthcheck")) broadcastSocketMessage("thumpthump");
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
	data[len] = 0;
	Serial.printf("											Websocket message received. Data: ");
	Serial.println((char*)data);
	websocketRoutes(data);
  }
}

bool messageIs( uint8_t *data, std::string message) {
    if (strcmp((char*)data, message.c_str()) == 0) {
      return true;
    }
	return false;
}

void broadcastSocketMessage(std::string message) {
	Serial.printf("											Sending message: ");
	Serial.println(message.c_str());
	ws.textAll(message.c_str());
}

void notFound(AsyncWebServerRequest *request) {
	request->send(404, "text/plain", "Not found");
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("											WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("											WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}
