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

#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <FastLED.h>

#define NUM_LEDS 10
#define DATA_PIN 2

// #define HARD_RESET

const char *PARAM_MESSAGE = "message";

AsyncWebServer server(80);
DNSServer dns;
CRGB leds[NUM_LEDS];
int statusLedActive = 0x0;

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

void setLed(int state)
{
  digitalWrite(LED_BUILTIN, state);
  statusLedActive = state;
}

void ledOff()
{
  setLed(0x1);
}

void ledOn()
{
  setLed(0x0);
}

void flashLed()
{
  ledOn();
  delay(20);
  ledOff();
  delay(50);
}

void setUpRoutes()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        Serial.println("/");
        request->send(200, "text/plain", "Hello, world"); });

  // Send a GET request to <IP>/get?message=<message>
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        Serial.println("/get");
        String message;
        if (request->hasParam(PARAM_MESSAGE)) {
            message = request->getParam(PARAM_MESSAGE)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, GET: " + message); });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        ledOn();
        digitalWrite(LED_BUILTIN, LOW);
        request->send(200, "text/plain", "on"); });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        ledOff();
        request->send(200, "text/plain", "off"); });

  // Send a POST request to <IP>/post with a form field message set to <message>
  server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request)
            {
        Serial.println("/post");
        String message;
        if (request->hasParam(PARAM_MESSAGE, true)) {
            message = request->getParam(PARAM_MESSAGE, true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message); });

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

  flashLed();
  Serial.println("Hello");
  flashLed();
  pinMode(LED_BUILTIN, OUTPUT);
  flashLed();
  Serial.println("FastLED");
  flashLed();
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  flashLed();
  Serial.println("wifiManager");
  flashLed();
  AsyncWiFiManager wifiManager(&server, &dns);
  flashLed();
  Serial.println("connecting");
  flashLed();
  wifiManager.autoConnect("RetroRoom");
  flashLed();
  Serial.println("connected...yeey :)");
  flashLed();
  Serial.println("server:)");
  setUpRoutes();
  flashLed();
  Serial.println("/server handlers set up");
  flashLed();
  server.begin();
  Serial.println("Running!");
}

void loop()
{
}
