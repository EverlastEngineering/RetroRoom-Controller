#ifndef RR_WEBSERVER_H
#define RR_WEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>

extern AsyncWebServer server;
extern DNSServer dns;

extern void notFound(AsyncWebServerRequest *request);
extern void setUpRoutes();

#endif