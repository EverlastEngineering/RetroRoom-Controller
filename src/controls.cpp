#include "controls.h"
#include "Console.h"
#include "ircontrol.h"
#include "main.h"
#include "network.h"
#include "stackselector.h"
#include "consoles.h"
#include "lighting.h"

// IRAM_ATTR is an ESP-specific macro that maps to an isr-aligned attribute.
// On RP2040 (Earle Philhower core) ISR handlers don't need any attribute --
// the PIO / vector system handles alignment -- so define it to nothing.
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

RotaryEncoder *encoder = nullptr;
EasyButton rotarySelector(ROTARY_SELECTOR_PIN);
EasyButton touchSensor(TOUCH_SENSOR_PIN,35,true,false);

bool isTouched = false;
volatile bool hasTouchInterruptFired = false;

// checkPosition() ISR for the rotary encoder. Must be a real function on every
// board that compiles controls_init(); the conditional definitions below keep
// ESP's IRAM_ATTR optimization without breaking RP2040 / AVR.
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
#elif defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_YD_RP2040)
// RP2040: no attribute needed; the function lives in normal flash and is
// jumpable from the vector table.
void checkPosition() {
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
	// ESP only: the legacy capacitive touch-sensor handler. On Pico envs
	// (including the YD-RP2040 dev board) TOUCH_SENSOR_PIN is repurposed
	// as a regular button input (YD: GP24 USR button) and the touchDetected
	// behavior would paint a white pixel via lightRing -> lightSingle and
	// override the smoke-test WS2812 cycle. Skip on non-ESP.
#if defined(ESP8266)
	touchSensor.onPressed(touchDetected);
#endif
	// Smoke-test toggle: on the YD-RP2040 dev board TOUCH_SENSOR_PIN is GP24
	// (the USR button). Each press flips the red/green/blue cycle on/off so
	// we can confirm the FastLED PIO path is alive without holding a serial
	// monitor open.
	//
	// IMPORTANT: register via onPressed() only -- do NOT register onPressedFor.
	// EasyButton's wasReleased() fires _pressed_callback() only when
	// _was_btn_held is false. _was_btn_held is set inside _checkPressedTime()
	// gated on _pressed_for_callback being non-null; if we don't register
	// onPressedFor at all, _was_btn_held stays false and _pressed_callback
	// fires on every release. Registering onPressedFor(100, ...) would set
	// _was_btn_held = true on any press >100ms and silently swallow the
	// toggle. (Which is exactly the bug we just hit.) So: skip onPressedFor
	// on the YD env, accept the loss of the legacy touchReleaseDetected
	// behavior on this board. touchDetected() is also gated out below on
	// non-ESP envs to keep the WS2812 ring clear of the white-pixel-paint
	// bug from commit a38f4a0.
	touchSensor.onPressed(lightCycleToggle);
	if (touchSensor.supportsInterrupt()) {
		attachInterrupt(digitalPinToInterrupt(TOUCH_SENSOR_PIN), touchSensorISR, CHANGE);
		Serial.println("Button will be used through interrupts");
	}
}



void rotarySelectorPressed() {
	// sendSonyPower();
	// Serial.println(SNES);
	// selectConsole() is now a free function in src/consoles.cpp; previously
	// it was a method on the legacy Console class. The legacy class has been
	// removed (now an alias for retroroom_core::Console from lib/ConsoleConfig),
	// and the core type is pure -- no I/O, no Serial, no selectStack.
	selectConsole(CurrentConsole());
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
#if defined(HAS_LEDS)
	lightRing(true);
#endif
	if (!isTouched) {
		isTouched = true;
#if defined(HAS_WIFI)
		broadcastSocketMessage("touch: touched");
#endif
	}
}

void touchReleaseDetected() {
#if defined(HAS_LEDS)
	lightRing(false);
#endif
	if (isTouched) {
#if defined(HAS_WIFI)
		broadcastSocketMessage("touch: released");
#endif
		isTouched = false;
	}
}


void rotaryEncoderTick() {
	// Note: the touch sensor is now polled from loop() via touchSensor.update()
	// (which fires onPressed / wasReleased handlers correctly under EasyButton's
	// debounce). The previous "isPressed() ? touchDetected() : touchReleaseDetected()"
	// poll here caused touchDetected() to fire on every loop iteration while the
	// USR button on the YD-RP2040 was held, which painted a white pixel via
	// lightSingle() and overrode the smoke-test WS2812 cycle. Removing that poll.
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
#if defined(HAS_LEDS)
			ringLEDPrevious();
#endif
			// Serial.println("ccw");
			if (currentConsoleIndex == 0) {
				return;
			}
			// Serial.println("subtracting one");
			currentConsoleIndex--;
		} else if (direction == 1) {
#if defined(HAS_LEDS)
			ringLEDNext();
#endif
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