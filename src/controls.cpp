#include "controls.h"
#include "Console.h"
#include "main.h"
#include "network.h"
#include "stackselector.h"
#include "consoles.h"
#include <ConsoleConfig.h>  // LedFeel: the strip's feel, parsed from the config
#include "lighting.h"
#include "ledstring.h"
#include <LedStringPaint.h>  // retroroom_core::DetentGate
#include <CabinetMenu.h>      // retroroom_core::MenuState: the config menu
#include "display.h"          // display_showMenu / display_menuClosed
#include <CabinetMenu.h>      // retroroom_core::MenuState: the config menu
#include "display.h"

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
// Last observed proximity state, for edge detection in
// controls_touchTick(). Starts false so a boot with the operator's hand
// already near the knob still registers an approach.
static bool proximityActive = false;

// Browse state for the rotary knob. Owned here rather than in
// src/ledstring.cpp because the *decision* (how many detents a console
// step costs, and where the blob has got to) belongs with the rest of
// the browse cursor, while ledstring.cpp only turns that decision into
// pixels.
//
// browseAnchorIndex is the console the LED blob departs from -- the last
// one the browse snapped onto. It is deliberately not the same thing as
// browsedConsoleIndex (which reports the console being *approached*) or
// currentConsoleIndex (the live one). -1 means the browse has not run
// since boot, so the first detent seeds it.
static retroroom_core::DetentGate browseGate;
static int browseAnchorIndex = -1;

// The config menu's state. Owned here rather than in display.cpp,
// because the thing that decides what the menu is doing is the knob --
// which is this file's business -- and display.cpp only puts the answer
// on the glass.
static retroroom_core::MenuState menuState;

// When a step commits, rotary detents are ignored until this. 0 means
// not armed, and a deadline of 0 is not reachable from a lockout
// duration, so the two cannot be confused. See
// ledFeel.settleLockoutMs for why this exists.
//
// This is a *rotary* lockout. The press is handled by a different
// path and is deliberately not gated: overshooting a detent and then
// committing to it is the mistake being absorbed, and blocking the
// press as well would strand the operator on a console they did not
// ask for.
static uint32_t settleLockoutUntilMs = 0;

namespace {
// The ring's free-running spinner: the only thing that answers a turn
// the browse is not going to act on.
//
// The operator does not need to know the browse is being helped along.
// A knob that visibly turns while the strip refuses to follow reads as
// a dropped input or a broken encoder, which is a worse outcome than
// the one the settle lockout exists to prevent. So the ring moves on
// every turn, and only the browse ignores some of them.
//
// Held in a helper because it now has two call sites -- one here for
// the locked-out case, one below for the ordinary one -- and two copies
// of "which way is this turning" is one more thing to keep in step.
void spinRingFor(int direction) {
	if (direction == -1) {
		ringLEDPrevious();
	} else {
		ringLEDNext();
	}
}

// The browse thresholds, read from ledFeel -- the `led` block of
// /consoles.json -- the same way src/ledstring.cpp assembles its effect
// config out of that same struct.
//
// Handed to the gate on EVERY DETENT rather than configured once, and
// that is the point of the signature. The thresholds are parameters;
// only the accumulated position is state. Holding them in the gate made
// the knob ignore anything that changed ledFeel after boot, which is
// exactly what the config menu does -- setting detentsPerStep to 17
// showed 17 in the menu and left the knob on 5.
//
// This is the third time this session that a value was read out of
// ledFeel too early to see a later change. The other two were a static
// initialiser in src/ledstring.cpp and the setup() ordering of
// controls_init() itself. The pattern is always the same: something
// copies a value out of ledFeel and keeps it. The cure is not to be
// careful about *when* it is read; it is not to keep a copy.
retroroom_core::DetentGateConfig browseGateConfig() {
	return retroroom_core::DetentGateConfig(
		ledFeel.detentsPerStep, ledFeel.fastDetentsPerStep,
		ledFeel.fastSpinWindowMs);
}
}  // namespace

// checkPosition() ISR for the rotary encoder. Defined unconditionally now
// that ESP8266 and AVR are gone.
void checkPosition() {
	encoder->tick(); // just call tick() to check the state.
}

void controls_init() {
	// Clear any accumulated gate state before the encoder can fire, so
	// the first detent after boot starts from a whole step.
	browseGate.reset();
	browseAnchorIndex = -1;

	encoder = new RotaryEncoder(ROTARY_PIN_IN2, ROTARY_PIN_IN1,
								RotaryEncoder::LatchMode::TWO03);
	attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_IN2), checkPosition,
					CHANGE);
	attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_IN1), checkPosition,
					CHANGE);

	// rotary clicker
	rotarySelector.begin();
	rotarySelector.onPressed(rotarySelectorPressed);
	// Long press opens the config menu. onPressedFor() fires once the
	// button has been *held* for the duration, so it cannot be confused
	// with a click -- which matters, because a click is already "commit
	// the console" and a menu that ate commits would make the browse
	// unusable.
	//
	// 900 ms is a guess and should be set by feel. It has to be long
	// enough that a deliberate press-and-turn is never mistaken for a
	// hold, and short enough that opening the menu does not feel like
	// waiting. The dead double-click registration that used to sit here
	// is gone; see sequenceElapsed(), which now does the menu.
	rotarySelector.onPressedFor(900, sequenceElapsed);
	if (rotarySelector.supportsInterrupt()) {
		rotarySelector.enableInterrupt(rotarySelectorISR);
		Serial.println("Button will be used through interrupts");
	}

	// touch sensor -- PROXIMITY ONLY, never a console selection.
	//
	// Capacitive proximity pad on TOUCH_SENSOR_PIN. The electrode is the
	// metal rotary knob, so a hand approaching the knob raises the pin
	// and the ring lights as an affordance cue before the operator
	// touches anything. Approach must NOT move currentConsoleIndex, drive
	// the stack selector, or fire IR -- the ring light is the only effect.
	//
	// No onPressed() callback is registered here, and there deliberately
	// must not be one. EasyButton dispatches _pressed_callback from
	// inside its wasReleased() branch, so onPressed() actually fires on
	// the RELEASE edge (EasyButtonBase.cpp: wasReleased() is
	// `!_current_state && _changed`). For a proximity pad that is the
	// wrong edge -- it would light the ring when the hand leaves instead
	// of when it arrives. Both transitions are detected explicitly in
	// controls_touchTick() instead.
	//
	// No interrupt either, unlike the three real buttons. Gating the
	// read() on an edge means a hand that is already near the knob at
	// power-on and never moves produces no edge at all, so the ring
	// would stay dark. EasyButton's own POLL mode is the default and
	// read() on a GPIO is free next to everything else loop() does.
	touchSensor.begin();

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
	// The config menu owns the click while it is open -- it opens the
	// editor, or commits the one being edited, or leaves on "Go Back".
	// Returning here is the whole point: a click that reached
	// selectConsole() underneath an open menu would commit a console the
	// operator is in the middle of reconfiguring.
	if (menuIsOpen(menuState)) {
		controls_menuClick();
		return;
	}
	// sendSonyPower();
	// Serial.println(SNES);
	// selectConsole() is now a free function in src/consoles.cpp; previously
	// it was a method on the legacy Console class. The legacy class has been
	// removed (now an alias for retroroom_core::Console from lib/ConsoleConfig),
	// and the core type is pure -- no I/O, no Serial, no selectStack.
	//
	// The commit point for a rotary browse: the console the operator has
	// been *shown* becomes the selection, and only now. Everything
	// downstream of currentConsoleIndex follows from this assignment.
	//
	// It is the browse ANCHOR, not browsedConsoleIndex. The anchor is
	// the last console the browse actually snapped onto -- the one that
	// has been lit and pulsing as a candidate. browsedConsoleIndex is
	// the one merely being approached, which after a single detent is
	// already the next console along, so committing to it meant one
	// click could select a console the operator had never been shown.
	//
	// The same line covers the other two cases without special-casing:
	// mid-step the anchor is the console already selected, so a press
	// reverts the browse and changes nothing; and after a snap followed
	// by more detents, it commits the pulsing candidate rather than
	// whichever console the fill had wandered toward.
	int target = browseAnchorIndex;
	if (target < 0) {
		// No browse has run since boot, so there is no anchor to commit
		// and the press is a re-selection of what is already selected.
		target = currentConsoleIndex;
	}
	if (HowManyConsoles() > 0 && target >= 0 && target < HowManyConsoles() &&
		target != currentConsoleIndex) {
		currentConsoleIndex = target;
	}
	selectConsole(CurrentConsole());
}

// The ring gave up -- idle timeout expired, or the hand left the
// proximity pad. Snap the browsed cursor back to the selected console so
// the next detent browses relative to what is actually live, rather than
// being stranded wherever an abandoned spin happened to land.
//
// Only the cursor moves. The ring's own pixel counter
// (currentRingLED in src/lighting.cpp) is a free-running spinner and is
// intentionally left alone -- see the comment in rotaryEncoderTick().
//
// This is the same end-of-browse as a commit, so it routes through
// controls_browseReset(): the LED string drops its blob/pulse and the
// detent gate forgets its accumulated progress, not just the cursor.
void controls_ringFadedOut() {
	if (browsedConsoleIndex == currentConsoleIndex &&
		browseAnchorIndex == currentConsoleIndex) {
		return;
	}
	Serial.print("Ring faded; browse cursor reverted to index ");
	Serial.print(currentConsoleIndex);
	Serial.print(" | strip=");
	Serial.print(ledstring_modeName());
	Serial.print(" retreatPending=");
	Serial.print(ledstring_browseRetreatPending() ? 1 : 0);
	Serial.print(" retreating=");
	Serial.println(ledstring_fillRetreatInProgress() ? 1 : 0);
	controls_browseReset();
}

// Long press: open the config menu. Reached from
// rotarySelector.onPressedFor(), see controls_init() for the duration.
void sequenceElapsed() {
	// A long press while the menu is already open closes it, so the
	// gesture is its own inverse. A second way out would be a way to
	// get stuck if the two ever disagreed about whether it was open.
	if (menuIsOpen(menuState)) {
		menuClose(menuState);
		Serial.println("Menu closed");
		display_menuClosed();
		controls_browseReset();
		return;
	}
	menuOpen(menuState);
	Serial.println("Menu open");
	controls_browseReset();
}

// Paint the menu and let its clock run. Called from main.cpp loop().
//
// menuTick() exists to expire the just-saved value back to a label, so
// this cannot be called only on input: with no detent and no click the
// value would sit on screen forever.
void controls_menuLoop() {
	if (!menuIsOpen(menuState)) {
		return;
	}
	menuTick(menuState, millis());
	const retroroom_core::MenuView v =
		menuView(menuState, CabinetMenu(), millis(), 2, LCD_COLS);
	display_showMenu(v.row[0], v.row[1]);
}


void rotarySelectorISR() {
	// Defer: only set the flag. The .read() pump in loop()
	// calls rotarySelector.read() which may invoke the
	// _pressed_callback. Running the callback from ISR would
	// block the CYW43 driver and any other time-critical
	// interrupt for the duration of selectConsole().
	hasRotarySelectorInterruptFired = true;
}

// The config menu takes the knob away from the browse while it is open.
// A detent scrolls the list rather than moving the console cursor, and a
// click opens the editor rather than committing.
//
// Routed here rather than inside rotaryEncoderTick() so there is one
// place that decides who owns the knob, instead of the browse quietly
// carrying on underneath the menu and both reacting to the same turn.
void controls_menuDetent(int direction) {
	menuDetent(menuState, CabinetMenu(), direction);
}

void controls_menuClick() {
	const retroroom_core::Menu m = CabinetMenu();
	if (menuState.mode == retroroom_core::MenuMode::EDIT) {
		int value = 0;
		if (menuCommit(menuState, millis(), &value) && m.count > 0 &&
			menuState.selected >= 0 && menuState.selected < m.count) {
			applyConfigValue(m.items[menuState.selected].key, value);
		}
		return;
	}
	// The value the editor starts from is the live one, not the default
	// and not a copy: a menu that opened onto a stale number would let
	// the operator save their way back to a value they had already
	// changed.
	int current = 0;
	if (m.count > 0 && menuState.selected >= 0 && menuState.selected < m.count &&
		m.items[menuState.selected].key != nullptr) {
		current = configValueOf(m.items[menuState.selected].key, 0);
	}
	// The prompt is about to ask whether to save, and whether that save
	// needs a restart. Set the flag BEFORE the click, because the click
	// is what opens the prompt and the two cannot be set in the other
	// order.
	menuState.pendingNeedsReboot = configNeedsReboot();
	const bool wasOpen = menuIsOpen(menuState);
	const retroroom_core::MenuAction chosen = menuSelect(menuState, m, current);
	if (wasOpen && !menuIsOpen(menuState)) {
		Serial.println("Menu closed");
		display_menuClosed();
	}
	switch (chosen) {
	case retroroom_core::MenuAction::SAVE: {
		Serial.println("Menu: save");
		const retroroom_store::SaveResult r = configSave();
		Serial.print("Menu: save ");
		Serial.println(r == retroroom_store::SaveResult::Ok ? "ok" : "FAILED");
		if (menuActionNeedsReboot(menuState)) {
			// The change is on flash; the cabinet is not. Say so, and say
			// it long enough to read: on sixteen columns a three-second
			// notice is a blink, and the message exists to be read.
			menuMessage(menuState, millis(), 5000);
		}
		return;
	}
	case retroroom_core::MenuAction::REBOOT: {
		Serial.println("Menu: reboot");
		// The strip has to be dark before the reset, or the operator
		// watches it light up again on the way down.
		ledstring_allOff();
		delay(50);
		Serial.flush();
		rp2040.restart();
		return;
	}
	default:
		return;
	}
}


// Proximity handling for the capacitive pad. Polled from loop() rather
// than driven by an EasyButton callback or an interrupt -- we need both
// the approach and the departure edge, and the library's callback only
// gives us one. See the comment in controls_init() for the full
// reasoning.
//
// The only effect is the ring light: approach lights it, departure fades
// it out. currentConsoleIndex, the stack selector and IR are untouched.
void controls_touchTick() {
	touchSensor.read();

	const bool near = touchSensor.isPressed();
#if defined(HAS_LEDS)
	// The pad's *reading*, every tick -- not an edge. The ring core does
	// its own edge detection, and it has to: a commit clears the hold
	// without the reading changing, so the two notions of "the hand
	// just arrived" are not the same question. Filtering here as well
	// left two detectors for one fact, which is the shape of the
	// abandoned-browse bug and of the commit/proximity bug before it.
	// Two detectors that currently agree are two detectors that can
	// stop agreeing.
	lightRingSetProximityHold(near);
#endif
	// The serial line is the operator-facing report of the pad, and it
	// only has anything to say when the reading changes -- so this is
	// the one place an edge is still the right thing to detect.
	if (near == proximityActive) {
		return;
	}
	proximityActive = near;
	Serial.print("Proximity ");
	Serial.println(near ? "near -- ring on" : "clear -- ring off");
#if defined(HAS_LEDS)
	lightRingSetProximityHold(near);
#endif
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

		if (direction != -1 && direction != 1) {
			return;
		}

		// The config menu owns the knob while it is open: a turn scrolls
		// it, and nothing below runs. Returning here rather than merely
		// skipping the paint matters -- the gate's position, the blob
		// and the settle lockout would otherwise keep advancing
		// underneath a menu that is plainly not listening, and the first
		// turn after closing it would land the browse somewhere the
		// operator never went.
		if (menuIsOpen(menuState)) {
			controls_menuDetent(direction);
			return;
		}

		if (num_consoles <= 0) {
			return;
		}

		// Swallow the detents that arrive immediately after a step
		// commits. A hand that overshoots the fifth detent by one
		// otherwise starts filling toward the next console, so one
		// mistimed turn costs two steps.
		//
		// The signed difference is what makes this correct across
		// millis()' rollover, which a plain `now < until` would not be.
		// Checked before the gate on purpose: the gate owns position
		// arithmetic, and the whole point here is that a swallowed detent
		// never reaches it. Leaving the gate untouched also means the
		// lockout cannot perturb the fast-spin clock, which measures
		// gaps between detents.
		//
		// The ring still moves. A turn the operator is not allowed to
		// act on is still a turn they made, and the ring is the one
		// indicator that reports turns without committing to them.
		const uint32_t nowMs = millis();
		if (ledFeel.settleLockoutMs > 0 &&
			settleLockoutUntilMs != 0 &&
			(int32_t)(nowMs - settleLockoutUntilMs) < 0) {
			spinRingFor(direction);
			return;
		}

		// Browse only -- this deliberately does NOT touch
		// currentConsoleIndex. Turning the knob previews a console; the
		// rotary click is what commits it. Anything reading the
		// selection (the strip, the LCD, /state.json, the WS broadcast)
		// therefore keeps reporting the live console while the operator
		// spins, and an abandoned spin reverts cleanly.
		//
		// A console step is ledFeel.detentsPerStep detents, not
		// one, so the LED string can show a blob creeping toward the
		// next console as they turn and snap when they commit to it.
		// The gate below owns the threshold arithmetic; see
		// lib/LedStringPaint for why the position is a continuous
		// fraction rather than a detent count.
		//
		// The anchor is the console the blob currently departs from --
		// the last one the browse *snapped* onto, which is not
		// necessarily the selected console. Anchor -1 means the browse
		// has never run since boot; seed it from the live selection the
		// first time the knob turns, so the first blob travels from
		// where the strip is actually painted.
		if (browseAnchorIndex < 0) {
			browseAnchorIndex = currentConsoleIndex;
		}

		// The gate is told the list size and the anchor because it is
		// the thing deciding whether a step completes, and a step off
		// the end of the list does not exist.
		const retroroom_core::DetentEvent ev =
			browseGate.onDetent(direction, nowMs, num_consoles,
								browseAnchorIndex, browseGateConfig());

		if (ev.frozen) {
			// Off the end of the list. The cabinet has physical ends and
			// turning past one reaches nothing, so every indication
			// freezes: the LED string keeps whatever it was showing, the
			// ring's spinner does not advance, and the browsed cursor
			// does not move. A knob that visibly keeps turning while
			// nothing goes anywhere reads as a fault rather than as an
			// end.
			//
			// Note this does override the ring's free-running spinner,
			// which normally answers "which way am I turning" without
			// reference to the list. The browse takes priority over it
			// here because the ring and the strip are both telling the
			// operator the same thing, and them disagreeing is worse
			// than neither moving.
			Serial.print("Browse at end of list (");
			Serial.print(browsedConsoleIndex);
			Serial.println(") -- frozen");
			return;
		}

		// Ring pixel only. The free-running spinner is the ring's own
		// position counter and is deliberately independent of both the
		// browsed and the selected console -- it answers "which way am
		// I turning", not "which console am I on". Moved *after* the
		// freeze check so a frozen knob does not spin the ring either:
		// at the end of the list the strip stops, and a ring still
		// spinning would say the list continues.
		spinRingFor(direction);

		// The console the operator is heading toward, and the one the
		// blob is departing. targetDirection is the sign of the
		// *position*, not of the detent, so turning back mid-transit
		// keeps aiming at the same side while the blob walks back.
		const int targetIndex = retroroom_core::stepWithin(
			browseAnchorIndex, num_consoles, ev.targetDirection);

		if (!ev.advanced) {
			// Still travelling. Move the blob and leave the cursor where
			// it is -- the console being approached is what the cursor
			// reports, but the anchor (and therefore what a commit
			// would mean) has not moved yet.
			browsedConsoleIndex = targetIndex;
#if defined(HAS_LEDS)
			ledstring_browseProgress(browseAnchorIndex, targetIndex,
									 ev.fractionPermille, ev.stepPermille);
#endif
			Serial.print("Browse ");
			Serial.print(ev.detents);
			Serial.print("/");
			Serial.print(ev.detentsPerStep);
			Serial.print(ev.fastMode ? " (fast) -> " : " -> ");
			Serial.println(consoles[targetIndex].name.c_str());
			return;
		}

		// The step completed: the animation lands and the cursor
		// follows. Reaching here means the step was legal -- the gate
		// freezes at either end of the list, so it cannot have wrapped.
#if defined(HAS_LEDS)
		// The travel needs both ends: the console the block leaves and
		// the one it lands on.
		ledstring_browseSnap(browseAnchorIndex, targetIndex);
#endif
		// Arm the settle lockout from the same timestamp the gate used,
		// so the window starts when the step completed rather than a few
		// microseconds later.
		if (ledFeel.settleLockoutMs > 0) {
			settleLockoutUntilMs =
				nowMs + (uint32_t)ledFeel.settleLockoutMs;
		}
		browseAnchorIndex = targetIndex;
		browsedConsoleIndex = targetIndex;
		Serial.print("Browse snapped -> ");
		Serial.println(consoles[targetIndex].name.c_str());
	}
}

void controls_browseReset() {
	// End the browse and put the strip back. Called from
	// selectConsole(), so it covers every commit path -- see the
	// declaration in controls.h for why it lives there rather than at
	// each caller.
	browseGate.reset();
	// The settle lockout belongs to a browse, so ending the browse ends
	// it. Left armed it would swallow the first detent after a commit,
	// which is a real turn the operator meant to make.
	settleLockoutUntilMs = 0;
	browsedConsoleIndex = currentConsoleIndex;
	browseAnchorIndex = currentConsoleIndex;
#if defined(HAS_LEDS)
	ledstring_browseClear();
#endif
}