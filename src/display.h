#ifndef RR_DISPLAY_H
#define RR_DISPLAY_H

// 16x2 I2C LCD driver for the RetroRoom-Controller firmware.
//
// Wired to the HD44780 + PCF8574 backpack on I2C0 (GP4/GP5) per
// pin-map-chart.md. The display shows:
//   - Welcome: "RetroRoom" / "Sit and Play" for LCD_WELCOME_MS at boot.
//   - Live: console name on line 1, the per-console tagline on line 2.
//   - Long lines (any direction >16 chars) scroll at LCD_SCROLL_MS cadence
//     after a LCD_SCROLL_PAUSE_MS pause, only the overflowing line.
//   - Backlight auto-off after lcdBacklightOffAfterMs ms (default 30 s)
//     of no selectConsole() activity; restored by display_wake().
//
// When HAS_LCD is undefined (e.g. for test_native or future builds
// without the I2C bus available) every function here is an inline
// no-op so the call-sites in src/consoles.cpp and src/main.cpp don't
// need their own #ifdef guards.

#include <Arduino.h>

#include "configuration.h"

#if defined(HAS_LCD)
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// I2C + LCD geometry. Defaults match the HD44780 + PCF8574 backpack
// commonly sold as a "16x2 i2c board"; see platformio.ini
// [env:pico2w] for the enjoyneering/LiquidCrystal_I2C@^1.4.0 dep.
//
// I2C0 lives on GP4/GP5 by default on the Pico 2 W variant
// (variants/rpipico2w/pins_arduino.h: PIN_WIRE0_SDA=4, PIN_WIRE0_SCL=5)
// but we setSDA/setSCL explicitly so the intent is visible at the
// call-site and the code survives a future variant swap.
#ifndef LCD_I2C_SDA_PIN
#define LCD_I2C_SDA_PIN 4   // I2C0 SDA per pin-map-chart.md
#endif
#ifndef LCD_I2C_SCL_PIN
#define LCD_I2C_SCL_PIN 5   // I2C0 SCL per pin-map-chart.md
#endif
// PCF8574 I2C address. The enjoyneering library uses an enum
// (pcf8574Address) rather than a raw byte; the default A21_A11_A01
// is 0x27, which is the address of the most common backpack variant.
// 0x3F on some clones -- override via build_flags to select a
// different enum value if needed.
#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR PCF8574_ADDR_A21_A11_A01
#endif
#ifndef LCD_COLS
#define LCD_COLS 16
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif
#ifndef LCD_WELCOME_MS
#define LCD_WELCOME_MS 3000
#endif
#ifndef LCD_SCROLL_PAUSE_MS
#define LCD_SCROLL_PAUSE_MS 1000
#endif
#ifndef LCD_SCROLL_MS
#define LCD_SCROLL_MS 250
#endif

// One LiquidCrystal_I2C instance, owned by display.cpp. Defined here
// so the compiler can verify the constructor matches the library
// (mhelm.col/LiquidCrystal_I2C) on every TU that includes display.h.
extern LiquidCrystal_I2C lcd;
#endif  // HAS_LCD

// Boot the LCD. Called once from main.cpp setup() after
// consoleDefinitions(). On no-I2C-device-at-this-address (the common
// bench case when the perfboard isn't wired yet) lcd.init() returns
// silently and the LCD just stays blank -- subsequent calls are
// no-ops, no crash.
void display_init();

// Push the current console + tagline onto the LCD. Called from
// selectConsole() (which fires on every advance/rewind/click). Also
// resets the backlight-off timer so a user-driven action wakes the
// display.
void display_show_console(const char* name, const char* tagline);

// Reset the backlight-off timer without changing what's on the
// screen. Called from selectConsole() right after
// display_show_console(). Cheap (just resets a uint32_t timestamp).
void display_wake();

// Pump the LCD state machine. Called from main.cpp loop(). Handles
// the welcome -> live transition (after LCD_WELCOME_MS), the
// scroll cadence, and the backlight auto-off. All time-based,
// non-blocking.
void display_loop();

// No-LCD stub mode. When HAS_LCD is undefined these are inline
// no-ops so the call-sites don't need #ifdef guards of their own.
#if !defined(HAS_LCD)
inline void display_init() {}
inline void display_show_console(const char* /*name*/, const char* /*tagline*/) {}
inline void display_wake() {}
inline void display_loop() {}
#endif

#endif  // RR_DISPLAY_H
