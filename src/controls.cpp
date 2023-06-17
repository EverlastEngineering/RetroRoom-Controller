#include "controls.h"
#include "ircontrol.h"
#include "network.h"

RotaryEncoder *encoder = nullptr;
EasyButton rotarySelector(ROTARY_SELECTOR_PIN);
EasyButton touchSensor(TOUCH_SENSOR_PIN);


#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_NANO_EVERY)
// This interrupt routine will be called on any change of one of the input
// signals
void checkPosition() {
	encoder->tick(); // just call tick() to check the state.
}
#elif defined(ESP8266)
// @brief The interrupt service routine will be called on any change of one of
// the input signals.
IRAM_ATTR void checkPosition() {
	encoder->tick(); // just call tick() to check the state.
}
#endif

void controls_init() {
	encoder =
		new RotaryEncoder(ROTARY_PIN_IN1, ROTARY_PIN_IN2, RotaryEncoder::LatchMode::TWO03);
	attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_IN1), checkPosition, CHANGE);
	attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_IN2), checkPosition, CHANGE);

	// rotary clicker
	rotarySelector.begin();
	rotarySelector.onPressed(rotarySelectorPressed);
	// rotary_selector.onSequence(2, 1500, sequenceElapsed); // double click
	if (rotarySelector.supportsInterrupt()) {
		rotarySelector.enableInterrupt(rotarySelectorISR);
		Serial.println("Button will be used through interrupts");
	}

	// touch sensor
	touchSensor.begin();
	touchSensor.onPressed(touchDetected);
	if (touchSensor.supportsInterrupt()) {
		touchSensor.enableInterrupt(touchSensorISR);
		Serial.println("Button will be used through interrupts");
	}
}

void rotarySelectorPressed() {
	sendSonyPower();
	Serial.println(SNES);
}

void sequenceElapsed() { Serial.println("Double click"); }

void rotarySelectorISR() {
	/*
	  When button is being used through external interrupts,
	  parameter INTERRUPT must be passed to read() function
	 */
	rotarySelector.read();
}

void touchSensorISR() {
	/*
	  When button is being used through external interrupts,
	  parameter INTERRUPT must be passed to read() function
	 */
	touchSensor.read();
}

void touchDetected() {
	Serial.println("Sensor sensed");
	broadcastSocketMessage("Sensor sensed");
}

void rotaryEncoderTick() {
	static int pos = 0;

	encoder->tick(); // just call tick() to check the state.

	int newPos = encoder->getPosition();
	if (pos != newPos) {
		Serial.print("pos:");
		Serial.print(newPos);
		Serial.print(" dir:");
		Serial.println((int)(encoder->getDirection()));
		pos = newPos;
	}
}