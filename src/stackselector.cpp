#include "main.h"
#include "stackselector.h"

int stackDelay = 1;

void selectStack_init() {
	pinMode(ARM_PIN, OUTPUT);
	pinMode(CYCLE_PIN, OUTPUT);
	pinMode(ENABLE_PIN, OUTPUT);
	digitalWrite(ARM_PIN, LOW);
	digitalWrite(CYCLE_PIN, LOW);
	digitalWrite(ENABLE_PIN, LOW);
	selectStack(HowManyConsoles()+1);
}

void selectStack(int position) {
	// Serial.print("Position: ");
	// Serial.println(position);
	// if (position == 1) {
	// 	armStack();
	// 	disarmStack();
	// }
	// else if (position == 2) {
	// 	clockCycle();
	// }
	// else if (position == 3) {
	// 	enableStack();
	// }
	enableStack();
	armStack();
	clockCycle();
	disarmStack();
	for (int i = 0; i < position-1; i++) {
		clockCycle();
	}
	enableStack();
	for (int i = 0; i < HowManyConsoles()-position; i++) {
		clockCycle();
	}
	// Serial.println("Stack Selection Complete");
}

void enableStack() {
	// Serial.println("enableStack");
	digitalWrite(ENABLE_PIN, HIGH);
	delay(stackDelay);
	digitalWrite(ENABLE_PIN, LOW);
	delay(stackDelay);
}

void armStack() {
	// Serial.println("armStack");
	digitalWrite(ARM_PIN, HIGH);
	delay(stackDelay);
}

void disarmStack() {
	// Serial.println("disarmStack");
	digitalWrite(ARM_PIN, LOW);
	delay(stackDelay);
}
void clockCycle() {
	// Serial.println("clockCycle");
	digitalWrite(CYCLE_PIN, HIGH);
	delay(stackDelay);
	digitalWrite(CYCLE_PIN, LOW);
	delay(stackDelay);
}