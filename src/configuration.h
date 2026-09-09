#ifndef RRCONFIGURATION_H
#define RRCONFIGURATION_H

// lighting
#define NUM_LEDS 8

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_NANO_EVERY)
	#define ROTARY_PIN_IN1 A2
	#define ROTARY_PIN_IN2 A3
#elif defined(ESP8266)
	#define ARM_PIN D0 // Selector ARM pin,  **HIGH at boot**
	#define ROTARY_PIN_IN1 D1
	#define ROTARY_PIN_IN2 D2
	#define DATA_PIN D3 // connected to FLASH button, boot fails if pulled LOW
	#define TOUCH_SENSOR_PIN D5
	#define ROTARY_SELECTOR_PIN D6
	#define IR_CONTROL_PIN D7
	#define CYCLE_PIN D8 // Selector CYCLE pin, boot fails if pulled HIGH
	#define ENABLE_PIN D9
	#define MANUAL_OE_PIN D10
#elif defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
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