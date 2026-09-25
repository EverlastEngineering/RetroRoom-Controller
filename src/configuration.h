#ifndef RRCONFIGURATION_H
#define RRCONFIGURATION_H

// lighting
#define NUM_LEDS 8

// ESP8266 (NodeMCU v2) and AVR boards were dropped on session/merge-pico-json.
// Only the Raspberry Pi Pico (RP2040) + Earle Philhower's arduino-pico core
// are supported. The ESP-only `#define MANUAL_OE_PIN` is intentionally gone;
// when a perfboard revision lands, MANUAL_OE_PIN will be added back to the
// Pico block (see TODO.md).

#if defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_RASPBERRY_PI_PICO_2W)
	// Bare Raspberry Pi Pico / Pico-W / Pico 2 W pin map. All three have
	// the same GP0-GP28 layout at the GPIO level (the Pico 2 W is an RP2350A
	// dual-core ARM Cortex-M33; the original Pico and Pico-W are RP2040).
	// On-board LED differs: GP25 on the Pico / Pico-W; GP64 on the Pico 2 W
	// -- the framework's LED_BUILTIN resolves to the right pin per board.
	// The Pico-W variant (ARDUINO_RASPBERRY_PI_PICO_W) and the Pico 2 W
	// (ARDUINO_RASPBERRY_PI_PICO_2W) have on-board CYW43 WiFi -- see
	// [env:pico2w] in platformio.ini and todo/open/2026-09-21_pico-2-w-platform.md
	// for the CYW43 / webserver bring-up.
	//
	// MANUAL_OE_PIN: drives an external output-enable MOSFET (high = LED
	// ring blanked for power-saving / standby). GP11 sits at the end of
	// the StackSelector cluster (GP8/9/10 + OE on GP11 = contiguous 4-pin
	// functional block). The `#ifdef MANUAL_OE_PIN` guards in src/state.cpp
	// and src/main.cpp activate automatically once this define is set. To
	// temporarily disable, comment the line out -- the firmware falls back
	// to FastLED.setBrightness(0) for the off path.
	//
	// SELECTED_CONSOLE_LED_STRING_DATA / NUM_SELECTED_CONSOLE_LED_STRING_LEDS
	// reserve the second FastLED strip's data pin + length for the future
	// JSON-driven "above-console" LED segment (c.led_position /
	// c.led_width in lib/ConsoleConfig). Defined but not yet referenced by
	// any .cpp -- the second-strip wiring lands in a follow-up commit.
	// Sized for the largest example config (example2.json MAME entry:
	// ledPosition=27 + ledWidth=15 = 42 LEDs, rounded up to 64 for headroom).
	//
	// Pin reshuffle rationale lives at pin-map-chart.md (source of truth).
	// Key choices: I2C0 (GP4/GP5) + I2C1 (GP2/GP3) + SPI0 (GP16..GP19) +
	// ADC (GP26..GP28) blocks are all fully free for future expansion.
	#define MANUAL_OE_PIN     11
	#define NEXT_CONSOLE_PIN  6   // Hardware "next console" push-button (active-low, INPUT_PULLUP). Wired in src/controls.cpp via EasyButton.
	#define PREV_CONSOLE_PIN  7   // Hardware "previous console" push-button (active-low, INPUT_PULLUP). Wired in src/controls.cpp via EasyButton.
	#define ARM_PIN           8
	#define CYCLE_PIN         9
	#define ENABLE_PIN        10  // StackSelector ENABLE on GP10; revise on perfboard
	#define TOUCH_SENSOR_PIN  12  // Capacitive touch; on perfboard connects to rotary encoder ground body for a clean common-ground reference.
	#define ROTARY_SELECTOR_PIN 13
	#define ROTARY_PIN_IN1    14
	#define ROTARY_PIN_IN2    15
	#define DATA_PIN          20  // FastLED ring (above-console selection ring) on GP20
	#define SELECTED_CONSOLE_LED_STRING_DATA 21  // Reserved for the second FastLED strip; wiring lands in a follow-up commit.
	#define IR_CONTROL_PIN    22
	#define NUM_SELECTED_CONSOLE_LED_STRING_LEDS 64
#elif defined(ARDUINO_YD_RP2040)
	// VCC-GND Studio YD-RP2040 (dev board currently on the desk). Distinct
	// from the standard Pico block above because:
	//   - Onboard WS2812 RGB LED is on GP23, not GP4 (no external ring yet)
	//   - User button USR is on GP24 (not exposed by the Earle Philhower
	//     Pico variant; we'd have to wire a button to use TOUCH_SENSOR_PIN
	//     or ROTARY_SELECTOR_PIN on a different pin)
	//   - Onboard blue LED is on GP25 (same as Pico's green LED)
	//
	// This block is selected by setting -DARDUINO_YD_RP2040 in build_flags
	// (the platform-arduino vccgnd_yd_rp2040 board target defines this
	// automatically). Used for smoke-testing FastLED 3.10+ PIO output
	// without a separate WS2812 ring wired up.
	//
	// Other pins (rotary, stackselector, IR) keep the same numeric values
	// as the generic Pico block above -- this is a smoke-test env, not a
	// production pin map. The YD's GP23 and GP24 are physically distinct
	// from anything else; the rest of the GPIO assignments are placeholders
	// that compile but don't connect to anything real until a perfboard
	// revision lands.
	#define NEXT_CONSOLE_PIN  6   // Placeholder; the YD dev board has no physical next/prev buttons.
	#define PREV_CONSOLE_PIN  7
	#define ARM_PIN           8
	#define CYCLE_PIN         9
	#define ENABLE_PIN        10
	#define TOUCH_SENSOR_PIN  24  // YD-RP2040 USR button (PIN_USRKEY)
	#define ROTARY_SELECTOR_PIN 13
	#define ROTARY_PIN_IN1    14
	#define ROTARY_PIN_IN2    15
	#define DATA_PIN          23  // YD-RP2040 onboard WS2812 (PIN_NEOPIXEL)
	#define SELECTED_CONSOLE_LED_STRING_DATA 21  // Placeholder; no second strip wired on the YD.
	#define IR_CONTROL_PIN    22
	#define NUM_SELECTED_CONSOLE_LED_STRING_LEDS 64
#endif

/** Consoles */
#define NES "Nintendo Entertainment System"
#define SNES "Super Nintendo Entertainment System"
#define Gamecube "Nintendo Gamecube"
#define N64 "Nintendo 64"
#define Wii "Nintendo Wii"
#define TurboGrafx16 "TurboGrafx-16"
#define PS1 "Sony PlayStation"
#define PS2 "Sony PlayStation 2"
#define SMS "Sega Master System"
#define Genesis "Sega Genesis"
#define Dreamcast "Sega Dreamcast"
#define Xbox "Microsoft Xbox"

/** VIDEO 1: The Hex Code for the Ir Control For SVideo Input */
#define SVideo 0x030
/** VIDEO 2: The Hex Code for the Ir Control For Front Panel Video Input */
#define FrontPanelComposite 0x830
/** VIDEO 3: The Hex Code for the Ir Control For Video Input */
#define Composite 0x430
/** VIDEO 4: The Hex Code for the Ir Control For Component Input */
#define Component 0xE30
/** The Hex Code for the Ir Control For SCART Input, which comes 
 * into the televsion on component after being converted. */
#define SCART Component

#endif