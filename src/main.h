#ifndef MAIN_H
#define MAIN_H

#include <FS.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

// needed for library
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h> //https://github.com/tzapu/WiFiManager

// #include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FastLED.h>

#include "html.h"
#include "status.h"

#endif