#ifndef RRCONFIGURATION_H
#define RRCONFIGURATION_H

// lighting
#define NUM_LEDS 10
#define DATA_PIN D3 // connected to FLASH button, boot fails if pulled LOW

// outputs

// define the output pins for control of the RetroSelector
#define ARM_PIN D0 //HIGH at boot
#define CYCLE_PIN D4 // HIGH at boot, connected to on-board LED, boot fails if pulled LOW
#define ENABLE_PIN D8 //boot fails if pulled HIGH

#define IR_CONTROL_PIN D7

//inputs

// rotary controls
#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_NANO_EVERY)
#define ROTARY_PIN_IN1 A2
#define ROTARY_PIN_IN2 A3
#elif defined(ESP8266)
#define ROTARY_PIN_IN1 D1
#define ROTARY_PIN_IN2 D2
#endif

#define TOUCH_SENSOR_PIN D5
#define ROTARY_SELECTOR_PIN D6

// Define the input # on the televsion

#define COMPOSITE_INPUT 3
#define COMPONENT_INPUT 5
#define SVIDEO_INPUT 4 

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

/** Inputs */
#define Composite "Composite"
#define SVideo "SVideo"
#define Component "Component"
#define SCART "SCART"

#endif