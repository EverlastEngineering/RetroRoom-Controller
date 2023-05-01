
#include <FS.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h> //https://github.com/tzapu/WiFiManager

#include <FastLED.h>

#include "status.h"
#include "webserver.h"

#define NUM_LEDS 10
#define DATA_PIN 2

// #define HARD_RESET

CRGB leds[NUM_LEDS];

void setup() {
	Serial.begin(76800); // native esp8266 speed.
						 // I can upload at 6 times this rate, 460800

#ifdef HARD_RESET // force reset code, set to true to nuke eeprom saved wifi
				  // info
	Serial.println("Resetting");
	delay(1000);
	WiFi.disconnect();
	ESP.eraseConfig();
	delay(1000);
	*((int *)0) = 0; // boom
	return;
#endif // end force reset

	pinMode(LED_BUILTIN, OUTPUT);
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
	AsyncWiFiManager wifiManager(&server, &dns);
	const char *hostname = "RetroRoom";
	WiFi.hostname(hostname);
	wifiManager.autoConnect(hostname);
	setUpRoutes();
	Serial.println("Running!");
	flashLed();
}

void loop() {
	if (flash) {
		flashLed();
	}
}
