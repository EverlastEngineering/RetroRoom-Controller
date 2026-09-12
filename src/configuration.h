#ifndef RRCONFIGURATION_H
#define RRCONFIGURATION_H

// lighting
#define NUM_LEDS 8

// ESP8266 (NodeMCU v2) and AVR boards were dropped on session/merge-pico-json.
// Only the Raspberry Pi Pico (RP2040) + Earle Philhower's arduino-pico core
// are supported. The ESP-only `#define MANUAL_OE_PIN` is intentionally gone;
// when a perfboard revision lands, MANUAL_OE_PIN will be added back to the
// Pico block (see TODO.md).

#if defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
	// Bare Raspberry Pi Pico / Pico-W pin map. The Pico-W variant
	// (ARDUINO_RASPBERRY_PI_PICO_W) is currently used the same way as plain Pico
	// since the CYW43 WiFi stack has not been wired in yet (network.{h,cpp} is
	// gated on HAS_WIFI; HAS_WIFI is intentionally NOT set on picow env for now).
	//
	// Override once wiring is known; this is a reasonable starting point that
	// avoids strapping pins (GP25 is the on-board LED, GP0-GP7 are safe on the
	// standard header).
	// MANUAL_OE_PIN deliberately left undefined: a perfboard revision is needed
	// before driving that line; for now, use FastLED.setBrightness(0) for the
	// "off" path.
	#define ROTARY_PIN_IN1    2
	#define ROTARY_PIN_IN2    3
	#define DATA_PIN          4   // FastLED ring on GP4
	#define TOUCH_SENSOR_PIN  5
	#define ROTARY_SELECTOR_PIN 6
	#define IR_CONTROL_PIN    7
	#define ARM_PIN           8
	#define CYCLE_PIN         9
	#define ENABLE_PIN        10  // StackSelector ENABLE on GP10; revise on perfboard
	// MANUAL_OE_PIN intentionally left undefined; see top of branch.
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
	#define ROTARY_PIN_IN1    2
	#define ROTARY_PIN_IN2    3
	#define DATA_PIN          23  // YD-RP2040 onboard WS2812 (PIN_NEOPIXEL)
	#define TOUCH_SENSOR_PIN  24  // YD-RP2040 USR button (PIN_USRKEY)
	#define ROTARY_SELECTOR_PIN 6
	#define IR_CONTROL_PIN    7
	#define ARM_PIN           8
	#define CYCLE_PIN         9
	#define ENABLE_PIN        10
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