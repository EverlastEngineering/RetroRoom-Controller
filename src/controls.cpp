#include "controls.h"
#include "Console.h"
#include "main.h"
#include "network.h"
#include "stackselector.h"
#include "consoles.h"
#include "lighting.h"

// No IRAM_ATTR shim needed anymore -- ESP8266 is gone. RP2040 / Pico does
// not require a special attribute for ISR handlers (the vector system
// handles alignment).

RotaryEncoder *encoder = nullptr;
EasyButton rotarySelector(ROTARY_SELECTOR_PIN);
EasyButton touchSensor(TOUCH_SENSOR_PIN,35,true,false);
// Hardware next/prev console buttons. Mirrors the touchSensor wiring:
// EasyButton handles debounce + (where supported) interrupt-driven
// detection. Both pins are active-low with internal pull-up enabled
// via the EasyButton constructor's debounce_ms / pullup flags. Pin
// numbers come from src/configuration.h (NEXT_CONSOLE_PIN, PREV_CONSOLE_PIN);
// see pin-map-chart.md for the authoritative perfboard layout.
EasyButton nextConsoleButton(NEXT_CONSOLE_PIN);
EasyButton prevConsoleButton(PREV_CONSOLE_PIN);

// Defer flags for the interrupt-driven buttons. The ISR only sets the
// flag; the actual EasyButton::read() (which can invoke the
// _pressed_callback synchronously) runs from loop() so the callback
// fires in non-ISR context. Running it from ISR was the bug that
// starved the CYW43 WiFi driver (and other time-critical ISRs)
// while selectConsole() drove the latch / I2C / PIO state machine
// from inside the interrupt handler -- symptom was a 1-2 s stall on
// every rotary click that propagated randomly to any of the
// loop() handlers because the recovery happened off-loop.
volatile bool hasRotarySelectorInterruptFired = false;
volatile bool hasNextConsoleInterruptFired = false;
volatile bool hasPrevConsoleInterruptFired = false;

bool isTouched = false;
volatile bool hasTouchInterruptFired = false;

// checkPosition() ISR for the rotary encoder. Defined unconditionally now
// that ESP8266 and AVR are gone.
void checkPosition() {
	encoder->tick(); // just call tick() to check the state.
}

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
	// Map the YD-RP2040 USR button (TOUCH_SENSOR_PIN = GP24) to advance
	// currentConsoleIndex with wrap-around. On the perfboard the same pin
	// is the capacitive touch input; the same handler fires.
	//
	// IMPORTANT: register via onPressed() only -- do NOT register onPressedFor.
	// EasyButton's wasReleased() fires _pressed_callback() only when
	// _was_btn_held is false; _was_btn_held is set inside _checkPressedTime()
	// gated on _pressed_for_callback being non-null. Registering an
	// onPressedFor handler (legacy touchReleaseDetected at 100ms) would set
	// _was_btn_held = true on any press longer than that threshold and
	// silently swallow advanceConsole().
	touchSensor.onPressed(advanceConsole);
	if (touchSensor.supportsInterrupt()) {
		attachInterrupt(digitalPinToInterrupt(TOUCH_SENSOR_PIN), touchSensorISR, CHANGE);
		Serial.println("Button will be used through interrupts");
	}

	// next / prev console push-buttons. These are the hardware counterpart
	// to the /next + /prev HTTP endpoints (src/network.cpp::onConsoleNext /
	// onConsolePrev) and the "next" / "prev" WebSocket commands. Same
	// shell functions as the HTTP path -- advanceConsole() / rewindConsole()
	// in src/consoles.cpp -- so the LED ring paint, WS broadcast, and
	// `selectedAtUptimeMs` stamp all happen once whether the trigger is
	// a button press, an HTTP GET, or a WS frame.
	//
	// IMPORTANT: register via onPressed() only -- same caveat as the
	// touch sensor above. Registering onPressedFor would set _was_btn_held
	// = true on any press longer than that threshold and silently swallow
	// advanceConsole() / rewindConsole() on release.
	nextConsoleButton.begin();
	nextConsoleButton.onPressed(nextConsolePressed);
	if (nextConsoleButton.supportsInterrupt()) {
		attachInterrupt(digitalPinToInterrupt(NEXT_CONSOLE_PIN), nextConsoleISR, CHANGE);
		Serial.println("Next-button will be used through interrupts");
	}
	prevConsoleButton.begin();
	prevConsoleButton.onPressed(prevConsolePressed);
	if (prevConsoleButton.supportsInterrupt()) {
		attachInterrupt(digitalPinToInterrupt(PREV_CONSOLE_PIN), prevConsoleISR, CHANGE);
		Serial.println("Prev-button will be used through interrupts");
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
	// Defer: only set the flag. The .read() pump in loop()
	// calls rotarySelector.read() which may invoke the
	// _pressed_callback. Running the callback from ISR would
	// block the CYW43 driver and any other time-critical
	// interrupt for the duration of selectConsole().
	hasRotarySelectorInterruptFired = true;
}

void touchSensorISR() {
	hasTouchInterruptFired = true;
}

void nextConsolePressed() {
	// Forward step. Same shell function as the /next HTTP handler and
	// the "next" WebSocket command -- see the comment in controls_init()
	// for the contract. advanceConsole() logs the new index, paints
	// lightSingle() on the ring, broadcasts "console:<name>:<idx>" over
	// WS, and stamps currentConsoleSelectedAtMs for the e2e harness.
	advanceConsole();
}

void prevConsolePressed() {
	// Backward step. Same shell function as the /prev HTTP handler and
	// the "prev" WebSocket command -- see advanceConsole() for the
	// contract. rewindConsole() mirrors advanceConsole() exactly except
	// the wraparound direction is -1 instead of +1.
	rewindConsole();
}

void nextConsoleISR() {
	// Defer to loop(). Calling EasyButton::read() from ISR was the
	// same CYW43-starvation bug the rotarySelectorISR had -- the
	// _pressed_callback chain (advanceConsole -> selectConsole ->
	// latch / LCD / IR / FastLED) must run with interrupts enabled.
	hasNextConsoleInterruptFired = true;
}

void prevConsoleISR() {
	// Same deferral as nextConsoleISR(); see comment there.
	hasPrevConsoleInterruptFired = true;
}

void touchDetected() {
	// Legacy capacitive-touch handler. Unused on the Pico port; kept as
	// a stub for the perfboard's capacitive-touch input.
	(void)isTouched;
}

void touchReleaseDetected() {
	// See touchDetected() above.
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
			ringLEDPrevious();
			if (currentConsoleIndex == 0) {
				return;
			}
			currentConsoleIndex--;
		} else if (direction == 1) {
			ringLEDNext();
			if (currentConsoleIndex == (num_consoles - 1)) {
				return;
			}
			currentConsoleIndex++;
		}
		// Serial.print(" currentConsoleIndex:");
		// Serial.println(currentConsoleIndex);
		Serial.print("Highlight Console: ");
		Serial.println(CurrentConsole().name.c_str());
	}
}