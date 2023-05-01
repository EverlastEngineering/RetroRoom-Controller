#ifndef RR_NETWORK_H
#define RR_NETWORK_H
#include <FS.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>

extern AsyncWebServer server;
extern DNSServer dns;

extern void initializeNetwork();
void routes();
void notFound(AsyncWebServerRequest *request);

#endif