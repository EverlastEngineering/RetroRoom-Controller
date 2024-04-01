#include "controls.h"
#include "Console.h"
#include "ircontrol.h"
#include "main.h"
#include "network.h"
#include "stackselector.h"
#include "consoles.h"
#include "lighting.h"

RotaryEncoder *encoder = nullptr;
EasyButton rotarySelector(ROTARY_SELECTOR_PIN);
EasyButton touchSensor(TOUCH_SENSOR_PIN,35,true,false);

bool isTouched = false;
volatile bool hasTouchInterruptFired = false;

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
	touchSensor.onPressedFor(100, touchReleaseDetected);
	if (touchSensor.supportsInterrupt()) {
		attachInterrupt(digitalPinToInterrupt(TOUCH_SENSOR_PIN), touchSensorISR, CHANGE);
		Serial.println("Button will be used through interrupts");
	}
}



void rotarySelectorPressed() {
	// sendSonyPower();
	// Serial.println(SNES);
	// Serial.print("Select Console: ");
	// Serial.println(CurrentConsole().name.c_str());
	// selectStack(CurrentConsole().selector_position);
	CurrentConsole().selectConsole();
}

void sequenceElapsed() { Serial.println("Double click"); }

void rotarySelectorISR() {
	/*
	  Remove this as I don't think this is safe.
	 */
	rotarySelector.read();
}

void IRAM_ATTR touchSensorISR() {
	hasTouchInterruptFired = true;
}

void touchDetected() {
	// Serial.println("Illuminate");
	lightRing(true);
	if (!isTouched) {
		isTouched = true;
		broadcastSocketMessage("touch: touched");
	}
}

void touchReleaseDetected() {
	lightRing(false);
	if (isTouched) {
		broadcastSocketMessage("touch: released");
		isTouched = false;
	}
}


void rotaryEncoderTick() {
	if (hasTouchInterruptFired) {
		touchSensor.read(); 
		hasTouchInterruptFired = false;
	}  
	if (touchSensor.isPressed()) {
		touchDetected();
	}
	else {
		touchReleaseDetected();
	}
	
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
			ringLEDPrevious();
			// Serial.println("ccw");
			if (currentConsoleIndex == 0) {
				return;
			}
			// Serial.println("subtracting one");
			currentConsoleIndex--;
		} else if (direction == 1) {
			ringLEDNext();
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