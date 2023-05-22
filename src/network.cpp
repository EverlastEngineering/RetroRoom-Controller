#include "network.h"
#include "html.h"
#include "status.h"
#include <iostream>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

void initializeNetwork() {
	routes();
	AsyncWiFiManager wifiManager(&server, &dns);
	const char *hostname = "RetroRoom";
	WiFi.hostname(hostname);
	wifiManager.autoConnect(hostname);
	server.begin();
	ws.onEvent(onEvent);
  	server.addHandler(&ws);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
	data[len] = 0;
	Serial.printf("Websocket message received. Data: ");
	Serial.println((char*)data);

	// Responses
	if (messageIs(data, "ledOn")) ledOn();
	else if (messageIs(data, "ledOff")) { flash = false; ledOff(); }
	else if (messageIs(data, "flash")) flash = !flash;
	else if (messageIs(data, "healthcheck")) broadcastSocketMessage("thumpthump");
  }
}

bool messageIs( uint8_t *data, std::string message) {
    if (strcmp((char*)data, message.c_str()) == 0) {
      return true;
    }
	return false;
}

void broadcastSocketMessage(std::string message) {
	Serial.printf("Sending message: ");
	Serial.println(message.c_str());
	ws.textAll(message.c_str());
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

	server.onNotFound(notFound);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}
