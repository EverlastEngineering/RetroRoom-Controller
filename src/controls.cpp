#include "controls.h"
#include "Console.h"
#include "ircontrol.h"
#include "main.h"
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

int currentConsoleIndex = 0;
std::vector<Console> consoles;

void controls_init() {
	encoder = new RotaryEncoder(ROTARY_PIN_IN2, ROTARY_PIN_IN1,
								RotaryEncoder::LatchMode::TWO03);
	attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_IN2), checkPosition,
					CHANGE);
	attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_IN1), checkPosition,
					CHANGE);

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

	currentConsoleIndex = 0;
}

void addConsole(Console console) { consoles.push_back(console); }

int HowManyConsoles() { return consoles.size(); }
Console CurrentConsole() { return consoles[currentConsoleIndex]; }

void rotarySelectorPressed() {
	// sendSonyPower();
	// Serial.println(SNES);
	Serial.print("Select Console: ");
	Serial.println(CurrentConsole().name.c_str());
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
	Serial.println("Illuminate");
	broadcastSocketMessage("Sensor sensed");
}

void rotaryEncoderTick() {
	static int pos = 0;

	encoder->tick(); // just call tick() to check the state.

	int newPos = encoder->getPosition();
	if (pos != newPos) {
		// Serial.print("Console Index:");
		// Serial.println(currentConsoleIndex);
		// Serial.print("pos:");
		// Serial.print(newPos);
		// Serial.print(" dir:");
		// Serial.println((int)(encoder->getDirection()));
		pos = newPos;

		int direction =
			((int)(encoder->getDirection())); // this "consumes" the last
											  // direction given by the encoder
		// Serial.print(" direction:");
		// Serial.println(direction);
		int num_consoles = (int)HowManyConsoles();
		// Serial.print(" num_consoles:");
		// Serial.println(num_consoles);

		if (direction == -1) {
			// Serial.println("ccw");
			if (currentConsoleIndex == 0) {
				return;
			}
			// Serial.println("subtracting one");
			currentConsoleIndex--;
		} else if (direction == 1) {
			// Serial.println("cw");
			if (currentConsoleIndex == (num_consoles - 1)) {
				return;
			}
			// Serial.println("adding one");
			currentConsoleIndex++;
		}
		// Serial.print(" currentConsoleIndex:");
		// Serial.println(currentConsoleIndex);
		Serial.print("Highlight Console: ");
		Serial.println(CurrentConsole().name.c_str());
	}
}