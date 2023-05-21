
/* FastLED Library */
#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>
#define NUM_LEDS 10
#define DATA_PIN D6 
CRGB leds[NUM_LEDS];

/* Rotary Encoder Library */
#include <Arduino.h>
#include <RotaryEncoder.h>

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_NANO_EVERY)
	#define PIN_IN1 A2
	#define PIN_IN2 A3
#elif defined(ESP8266)
	#define PIN_IN1 D1
	#define PIN_IN2 D2
#endif

RotaryEncoder *encoder = nullptr;

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_NANO_EVERY)
// This interrupt routine will be called on any change of one of the input signals
void checkPosition()
{
  encoder->tick(); // just call tick() to check the state.
}

#elif defined(ESP8266)
/**
 * @brief The interrupt service routine will be called on any change of one of the input signals.
 */
IRAM_ATTR void checkPosition()
{
  encoder->tick(); // just call tick() to check the state.
}

#endif

#include <EasyButton.h>
EasyButton rotary_selector(D3);
EasyButton touch_sensor(D5);

void buttonPressed()
{
  Serial.println("Button pressed");
}



void sequenceEllapsed()
{
  Serial.println("Double click");
}

void rotarySelectorISR()
{
  /*
    When button is being used through external interrupts, 
    parameter INTERRUPT must be passed to read() function
   */
  rotary_selector.read();
}

void touchSensorISR()
{
  /*
    When button is being used through external interrupts, 
    parameter INTERRUPT must be passed to read() function
   */
  touch_sensor.read();
}

/* Our own headers */
#include "status.h"
#include "network.h"
#include "Console.h"

/* HARD_RESET is a hack to completely nuke the onboard PRAM (or is it SRAM or..) that contains the saved wifi settings */
// #define HARD_RESET

/**
 * To Do
 * 
 * - make the list of consoles editable from the web UI <- ooo
 * - make the console class settings save to whatever it is the wifi saves to, so it remembers your console selection
 * 
*/

void sensorSensed()
{
  Serial.println("Sensor sensed");
  broadcastSocketMessage("Sensor sensed");
}


void setup() {
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
	Serial.begin(76800); // native esp8266 speed. Also, upload is possible at 6 times this rate, 460800.
	while (!Serial)
    ;
	pinMode(LED_BUILTIN, OUTPUT);
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
	initializeNetwork();
	Serial.println("Running!");
	flashLed();
	
	encoder = new RotaryEncoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);
	attachInterrupt(digitalPinToInterrupt(PIN_IN1), checkPosition, CHANGE);
  	attachInterrupt(digitalPinToInterrupt(PIN_IN2), checkPosition, CHANGE);

	//rotary clicker
	rotary_selector.begin();
	rotary_selector.onPressed(buttonPressed);
	rotary_selector.onSequence(2, 1500, sequenceEllapsed);
	if (rotary_selector.supportsInterrupt())
	{
		rotary_selector.enableInterrupt(rotarySelectorISR);
		Serial.println("Button will be used through interrupts");
	}

	// touch sensor
	touch_sensor.begin();
	touch_sensor.onPressed(sensorSensed);
	touch_sensor.onSequence(2, 1500, sequenceEllapsed);
	if (touch_sensor.supportsInterrupt())
	{
		touch_sensor.enableInterrupt(touchSensorISR);
		Serial.println("Button will be used through interrupts");
	}

	/**
	 * Console takes:
	 * led_position: the position of the first led on the rgb string for this console
	 * selector_position: the position in the selector system
	 * name: the friendly name of the console )
	*/
	Console consoles[2] = {
		Console(1,5,1,"NES"),
		Console(2,5,2,"SNES")
	};
	Serial.println(consoles[0].name.c_str());
}

void loop() {
	if (flash) {
		flashLed();
	}
	static int pos = 0;

	encoder->tick(); // just call tick() to check the state.

	int newPos = encoder->getPosition();
	if (pos != newPos) {
		Serial.print("pos:");
		Serial.print(newPos);
		Serial.print(" dir:");
		Serial.println((int)(encoder->getDirection()));
		pos = newPos;
	} // if
}
