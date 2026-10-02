/**
 * RetroRoom firmware main loop. Targets the Raspberry Pi Pico (RP2040)
 * via Earle Philhower's arduino-pico core. Originally written for
 * ESP8266; ESP support was dropped on session/merge-pico-json (see
 * todo/ and LOG.md).
 *
 * Author: Jason Copp
 * Contact: jason@everlastengineering.com
 * License: no public license yet
 */

#include "main.h"

#include "display.h"
#include "serialconfig.h"

// consoleDefinitions() is defined in src/consoles.cpp and reads the embedded
// JSON via the functional core (lib/ConsoleConfig). On boot it prints the
// number of consoles loaded so the user can confirm the JSON parser
// succeeded without having to look at the WS or run any test.

// loop()'s iteration count, reported in the heartbeat as `loop=<n>/s`.
// See state.h for why it is worth having.
uint32_t loopTickCount = 0;

void setup() {
	// Give the host USB-CDC driver a full second to enumerate and attach
	// to /dev/cu.usbmodem* before we touch USB at all. On the RP2350
	// (Pico 2 W) the CDC-ACM buffer discards writes issued before the
	// host opens the port, so a 1 s pre-begin delay is the cheapest way
	// to make sure every subsequent Serial.println() actually lands on
	// the wire. The delay happens before Serial.begin() because the
	// USB stack init can race with the host-side enumeration.
	delay(1000);

	// 115200 on Pico native USB-CDC is conventional. On the RP2350 (Pico 2 W)
	// the CDC-ACM buffer drops writes issued before the host opens the
	// port, so we wait briefly for a host connection before printing
	// anything. Bounded by 3 s so the firmware still boots unattended.
	Serial.begin(115200);
	const unsigned long waitStart = millis();
	while (!Serial && millis() - waitStart < 3000) {
		delay(10);
	}
	pinMode(LED_BUILTIN, OUTPUT);
	// Boot into the flashing state. `flash` is initialised true in
	// state.cpp and the on-board LED blink is then driven from
	// loop()'s flashLedTick() call. We deliberately don't call
	// flashLed() here -- flashLed() toggles `flash`, so calling it
	// once would DISABLE the boot-time flash. The MANUAL_OE_PIN
	// ring side effect is sacrificed (the operator can hit /flash
	// once to engage it if they want the ring OE PWM).
#if defined(HAS_LCD)
	// 16x2 I2C LCD -- brings up the hardware and paints the startup
	// "RetroRoom" / "Loading" screen. Deliberately FIRST, before the
	// network bring-up below: that path blocks for seconds, and this
	// is the only thing telling the operator the cabinet is alive
	// while it happens.
	//
	// The startup -> welcome -> live progression is owned by
	// display_loop() in loop(). Nothing further is needed here, and
	// calling display_init() again would just repeat ~1 s of I2C
	// bring-up and blank the panel.
	display_init();
#endif
#ifdef MANUAL_OE_PIN
	analogWrite(MANUAL_OE_PIN, 127);  // ring OE 50% PWM at boot
#endif
#if defined(HAS_LEDS)
	lighting_init();
#endif
#if defined(HAS_IR)
	ir_control_init();
#endif
	consoleDefinitions();
	// controls_init() comes *after* consoleDefinitions(), and not by
	// accident. It is the only caller of DetentGate::configure(), and
	// that reads ledFeel -- which is what consoleDefinitions() parses
	// out of /consoles.json. Run it earlier and the knob's detent gate
	// is wired to defaultLedFeel() for the rest of the session:
	// detentsPerStep reads 5 no matter what the config says.
	//
	// The symptom is narrow enough to be confusing. Every *runtime*
	// ledFeel read -- ledFeel.settleLockoutMs in the browse, the ring's
	// timings in lighting.cpp, the strip's own config assembled by
	// ledstring_init() -- is already past consoleDefinitions() and
	// therefore correct, so the knob is the one thing that looks
	// ignored. See browseGateConfig() in src/controls.cpp.
	//
	// Nothing in between needs the encoder, and the later attach is
	// the safer side of the trade: no detent can be recorded while the
	// gate is still holding defaults.
	controls_init();
	// Home the console latch. selectStack_init() also issues the
	// initial selectStack() pass, which establishes the known starting
	// position that restoreLastSelectedConsole() below steps relative
	// to -- and the latch shares a power rail with the controller, so
	// it does not hold its position across a power cycle. The two must
	// run in this order.
	selectStack_init();
	// Restore the console that was selected before the last power loss.
	// Must run between consoleDefinitions() -- which populates the list
	// the stored index refers to -- and ledstring_setConsole() below,
	// which lights the strip for currentConsoleIndex. Everything after
	// it (the strip, the LCD's live-line seed, /state.json) then reads
	// the restored index, so the device simply boots into the operator's
	// console rather than being switched to it afterwards.
	restoreLastSelectedConsole();
	// Paint the initial console's window on the second strip now
	// that consoleDefinitions() has populated src/consoles.cpp::consoles.
	// ledstring_init() powers the strip up dark; ledstring_setConsole(0)
	// lights the [ledPosition, ledPosition+ledWidth) range of the first
	// console in the list so the operator sees the feature live before
	// they've turned the dial. Both calls no-op when HAS_LEDS is
	// undefined (boards without the second strip wired).
#if defined(HAS_LEDS)
	ledstring_init();
	ledstring_setConsole(currentConsoleIndex);
#endif
#if defined(HAS_LCD)
	// Arm the backlight-off timer now (no selectConsole() call has
	// happened yet, so without this the backlight would stay on
	// forever). The LCD's phase progression is NOT touched here --
	// loop()'s display_loop() picks up the startup phase on its first
	// tick and takes it from there.
	display_wake();

	// "Nobody has set this cabinet up yet" -- two pages, and only on a
	// cabinet that is still running the config we shipped.
	//
	// Blocking, on purpose. Everything it could be done without
	// blocking is worse here: the pages are a setup flow, they belong
	// before the SoftAP rather than fighting it for the same 16x2
	// panel, and a queue would mean the LCD driver owned an ordering
	// between two subsystems that have no business knowing about each
	// other. A cabinet in this state has nothing configured to lose
	// and no network to break.
	//
	// The cost is honest and worth naming: loop() does not run for
	// two LCD_NOTICE_MS, so the LED does not blink, the knob does not
	// turn, and the radio does not start until these are done. That is
	// the same trade the Loading screen already makes (see
	// LCD_LOADING_MS), and on a cabinet nobody has configured there is
	// nothing for any of those to be doing.
	//
	// This skips the welcome splash entirely -- display_show_status()
	// forces the Live phase, and the handover in display_loop() has
	// not happened yet. Correct here: "RetroRoom / Sit and Play" is a
	// greeting for a cabinet that is ready, and this one is not.
	//
	// Guarded on HAS_LCD rather than left to the display stubs, because
	// on a board with no panel there is nothing to show and the ten
	// seconds would be spent waiting for nobody to look.
	if (!consoleConfigIsUploaded()) {
		display_show_status("Learn How To", "Setup RetroRoom");
		delay(LCD_NOTICE_MS);
		display_show_status("At everlast", "engineering.com");
		delay(LCD_NOTICE_MS);
		// Back to the live view. Same call selectConsole() makes, and
		// guarded on a non-empty list for the reason every other
		// CurrentConsole() caller guards: a cabinet with no config at
		// all has nothing to dereference.
		if (HowManyConsoles() > 0) {
			display_show_console(CurrentConsole().name.c_str(),
			                     CurrentConsole().tagline.c_str());
		}
	}
#endif
#if defined(HAS_WIFI)
	// Network LAST, and that ordering is the whole point.
	//
	// Nothing above this line needs the radio. consoleDefinitions()
	// reads /consoles.json off local flash, and the knob, the strip, the
	// ring and the LCD are all local. What the network buys is the web
	// UI, the /consoles.json upload and the WebSocket -- none of which
	// the operator has before the cabinet is up.
	//
	// It used to be here, before consoleDefinitions(), and the cost was
	// the whole boot: network_scan_cache() is a synchronous
	// WiFi.scanNetworks() (2-4 s) and network_init() then waited up to
	// kStaTimeoutSec -- 20 s -- for a router that might not be there. The
	// cabinet could spend half a minute fetching a config that was
	// already on the chip, while the LCD said "Loading".
	//
	// The scan still runs before the AP, which is the constraint that
	// actually mattered: the CYW43 cannot scan while a client is
	// associated with the SoftAP, so the boot window is the only safe
	// place to populate /scan.json's cache. Moving the whole block here
	// keeps that ordering *within* the block, so it is untouched --
	// network_scan_cache() is still called before network_init().
	network_scan_cache();
	network_init();
#endif
	// The USB-serial configuration channel. After the network, because
	// its auto-entry decision needs to know whether there is a network
	// to reach us on -- and deliberately not before, because a cabinet
	// with valid credentials whose router is merely slow is on its way
	// to being fine.
	serialcmd_init();
	Serial.println("Setup Complete.");
	Serial.flush();
}

void loop() {
	// Counted here and reported by the heartbeat, because the loop rate
	// is the sampling rate for every polled input on the cabinet -- most
	// of all the rotary selector, whose presses shorter than one
	// iteration do not happen at all. It cost a register and an
	// increment; the alternative was guessing why a gesture misbehaves.
	++loopTickCount;

	// Track the rotary encoder for console switching.
	rotaryEncoderTick();

	// The rotary selector is POLLED, every tick, unconditionally.
	//
	// Two reasons, and the second is the one that matters.
	//
	// First, the callback chain this read() can invoke drives the
	// latch, the LCD, the IR blaster and FastLED, so it must not run
	// with interrupts blocked. It used to be deferred out of the ISR
	// into a flag for exactly that reason; polling reaches loop()
	// context without the flag.
	//
	// Second, and this is the bug it fixes: in interrupt mode the
	// library reads the pin only when the ISR says it changed, and its
	// read() discards any change arriving within the debounce window
	// of the previous one. With a CHANGE interrupt nothing else will
	// report that transition, so the edge is gone. When the edge lost
	// is the release, the library still believes the knob is held and
	// fires the 900 ms long-press callback with it up -- the config
	// menu, opening itself during a fast double-click. POLL mode
	// re-reads every tick and simply picks the change up on the next
	// one, so nothing is lost. See the long version in
	// src/controls.cpp::controls_init().
	//
	// No update() call alongside it: in POLL mode read() itself drives
	// the onPressedFor held-time check.
	rotarySelector.read();
	if (hasNextConsoleInterruptFired) {
		hasNextConsoleInterruptFired = false;
		nextConsoleButton.read();
	}
	if (hasPrevConsoleInterruptFired) {
		hasPrevConsoleInterruptFired = false;
		prevConsoleButton.read();
	}
	// The capacitive pad is a proximity sensor, not a button, so it has
	// no callback to defer -- controls_touchTick() reads it and drives
	// the ring light off the state edges itself.
	controls_touchTick();

	// The config menu, if it is open. Painted here rather than on input
	// because the just-saved value has to revert to its label on a
	// timer, and with no detent and no click nothing else would move.
	controls_menuLoop();

	// The USB-serial configuration channel. Before the network pump,
	// because a paste arrives faster than the loop iterates and the
	// network is the one thing in here that can be slow.
	serialcmd_loop();

	// Pump the network stack (currently the captive-portal DNS server).
	// No-op when WiFi is not active.
#if defined(HAS_WIFI)
	network_loop();
#endif

	// Pump the on-board LED state machine. Owns LED_BUILTIN + ring
	// OE internally; main.cpp never touches the LED pin directly.
	// No-op when `flash` is false; 1 Hz toggle while it's true.
	flashLedTick();

	// Finish any ring fade that a rotary turn or the proximity pad
	// started, and decide when one should start. The tick that completes
	// a fade is the cue to snap the browsed cursor back to the selected
	// console, so an abandoned spin does not leave it stranded.
	// No-op when HAS_LEDS is undefined.
#if defined(HAS_LEDS)
	if (lighting_loop()) {
		controls_ringFadedOut();
	}
	// Advance whatever the GP21 string is animating -- the preview pulse
	// while a browse is snapped on a console, the twinkle-then-settle
	// after a commit. Returns immediately when the strip is resting, so
	// this costs one comparison in the common case.
	ledstring_loop();
#endif

	// Pump the LCD driver -- welcome -> live transition, scrolling,
	// backlight auto-off. No-op when HAS_LCD is undefined.
#if defined(HAS_LCD)
	display_loop();
#endif

	// Liveness heartbeat over Serial. See heartbeatTick() in
	// src/state.cpp -- without this a stuck radio looks identical
	// to a crashed firmware from the host's perspective.
	heartbeatTick();

	// Pump the debounced LittleFS save of the last-selected console.
	// See consoles_loop() in src/consoles.cpp -- selectConsole()
	// arms a deadline; this loop body is what actually fires the
	// save after the quiet window. Keeps the LittleFS write+sync off
	// the rotary click path (which used to make the dial feel
	// unresponsive on Pico's on-flash FS).
	consoles_loop();

	// On-board LED is driven exclusively from src/state.cpp:
	// ledOn / ledOff / flashLed toggle the `flash` flag and drive
	// the solid state, flashLedTick (above) does the 1 Hz blink
	// while flashing. The lightCycleTick / lightCycleToggle
	// helpers in src/lighting.{h,cpp} are kept around for the
	// perfboard's WS2812-ring smoke test only.
}
