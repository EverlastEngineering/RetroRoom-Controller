#include "main.h"


#define NUM_LEDS 10
#define DATA_PIN 2

// #define HARD_RESET

AsyncWebServer server(80);
DNSServer dns;
CRGB leds[NUM_LEDS];

void notFound(AsyncWebServerRequest *request)
{
	request->send(404, "text/plain", "Not found");
}

void setUpRoutes()
{
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->send(200, "text/html", html_index_html); });

	server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->send(200, "application/javascript", html_script_js); });

	server.on("/ledOn", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
        ledOn();
        digitalWrite(LED_BUILTIN, LOW);
        request->send(200, "text/plain", "on"); });

	server.on("/flash", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
        flash = true;
        request->send(200, "text/plain", "flash"); });

	server.on("/ledOff", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
        flash = false;
        ledOff();
        request->send(200, "text/plain", "off"); });

	server.onNotFound(notFound);
}

void setup()
{
	Serial.begin(76800); // native esp8266 speed.
						 // I can upload at 6 times this rate, 460800

#ifdef HARD_RESET // force reset code, set to true to nuke eeprom saved wifi info
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
	server.begin();
	Serial.println("Running!");
	flashLed();
}

void loop()
{
	if (flash)
	{
		flashLed();
	}
}
