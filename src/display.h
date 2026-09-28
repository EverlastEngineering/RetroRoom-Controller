#ifndef RR_DISPLAY_H
#define RR_DISPLAY_H

// 16x2 I2C LCD driver for the RetroRoom-Controller firmware.
//
// Wired to the HD44780 + PCF8574 backpack on I2C0 (GP4/GP5) per
// pin-map-chart.md. The display shows:
//   - Welcome: "RetroRoom" / "Sit and Play" for LCD_WELCOME_MS at boot.
//   - Live: console name on line 1, the per-console tagline on line 2.
//   - Long lines (any direction >16 chars) hold for LCD_SCROLL_PAUSE_MS,
//     then scroll at LCD_SCROLL_MS cadence, looping continuously with
//     LCD_LOOP_GAP blanks between repeats. Only the overflowing line moves.
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
// I2C0 lives on GP4/GP5 on the Pico 2 W variant. The pins themselves are
// declared in configuration.h (see pin-map-chart.md); we setSDA/setSCL
// explicitly rather than relying on Wire's board defaults so the code
// survives a future variant swap. The #ifndef guards are a fallback for
// board blocks in configuration.h that don't define them (e.g. the
// YD-RP2040 smoke-test block).
#ifndef LCD_I2C_SDA_PIN
#define LCD_I2C_SDA_PIN 4
#endif
#ifndef LCD_I2C_SCL_PIN
#define LCD_I2C_SCL_PIN 5
#endif
// PCF8574 I2C address, as a raw 7-bit byte rather than the library's
// pcf8574Address enum. Raw is deliberate: it means a rebuild for a
// different backpack is the obvious `-DLCD_I2C_ADDR=0x3F` on the
// command line, instead of requiring the reader to go look up which
// enum constant happens to equal that byte. display_init() scans the
// bus and prints what it finds, so you never have to guess.
//
// 0x27 is the PCF8574 default (A2/A1/A0 all high). 0x3F is the common
// PCF8574A clone.
#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x3F
#endif
#ifndef LCD_COLS
#define LCD_COLS 16
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif
#ifndef LCD_WELCOME_MS
#define LCD_WELCOME_MS 1000
#endif
// How long a freshly-selected line holds at the left edge before the
// marquee starts moving it. After that the line loops continuously.
#ifndef LCD_SCROLL_PAUSE_MS
#define LCD_SCROLL_PAUSE_MS 3000
#endif
#ifndef LCD_SCROLL_MS
#define LCD_SCROLL_MS 1000
#endif
// Blanks inserted between one repeat of a line and the next. Without
// them the tail of the line runs straight into its own head
// ("SystemSuper") and reads as a typo rather than a loop.
#ifndef LCD_LOOP_GAP
#define LCD_LOOP_GAP 3
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
