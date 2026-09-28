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
// How long the "Loading" screen holds before the panel is handed to
// the welcome screen.
//
// The hold is a floor, not a delay: the CYW43 join / SoftAP bring-up
// and the LittleFS console-config load in setup() block for a couple
// of seconds, and display_loop() is not pumped until setup() returns.
// So in practice the startup phase expires on the very first loop()
// tick after boot -- the Loading screen covers the blocking work for
// free. This constant only governs the case where setup() is fast
// (no WiFi, or a warm FS), so the message doesn't flash past in a
// single frame.
#ifndef LCD_LOADING_MS
#define LCD_LOADING_MS 500
#endif
// How long the welcome screen holds before the live console takes
// over. Started by the startup -> welcome handover, not by display_init().
#ifndef LCD_WELCOME_MS
#define LCD_WELCOME_MS 5000
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

// Bring the LCD hardware up and paint the startup "RetroRoom" /
// "Loading" screen. Called ONCE, at the top of main.cpp setup(),
// before consoleDefinitions() and before the network bring-up -- both
// of which block for seconds, and this is what tells the operator the
// cabinet is alive while they wait.
//
// The startup -> welcome -> live progression is display_loop()'s job.
// display_init() only does hardware bring-up and starts the clock on
// the startup phase.
//
// Idempotent. The bus scan, the address probe and lcd.begin()'s
// HD44780 init sequence (delay(500) + 11 nibble writes) together cost
// close to a second, and re-running lcd.begin() re-issues the init and
// blanks the panel we just painted. A second call is a no-op.
//
// On no-I2C-device-at-this-address (the common bench case when the
// perfboard isn't wired yet) the probe fails, every public display_*
// function no-ops, and the firmware stays usable.
void display_init();

// Push the current console + tagline onto the LCD. Called from
// selectConsole() (which fires on every advance/rewind/click). Also
// resets the backlight-off timer so a user-driven action wakes the
// display.
void display_show_console(const char* name, const char* tagline);

// Paint a fixed, non-scrolling two-line status message and hold it
// until the next display_show_console() / display_show_status() call.
// For terminal states that aren't console selections -- currently the
// "Rebooting" message shown just before a POST /consoles.json resets
// the board.
//
// Contract differences from display_show_console():
//   - Neither line scrolls, even past LCD_COLS. A status is read
//     once, not read in a loop.
//   - The backlight is forced on and re-armed, so a status that
//     lands while the auto-off timer is mid-countdown can't be
//     missed by an operator standing at the cabinet.
//
// Like display_show_console(), this is a no-op when no LCD was
// detected at boot.
void display_show_status(const char* line1, const char* line2);

// Reset the backlight-off timer without changing what's on the
// screen. Called from selectConsole() right after
// display_show_console(). Cheap (just resets a uint32_t timestamp).
void display_wake();

// Pump the LCD state machine. Called from main.cpp loop(). Owns the
// whole startup -> welcome -> live progression: the startup -> welcome
// handover (after LCD_LOADING_MS, and it seeds the live lines from the
// console list that setup() loaded), the welcome -> live transition
// (after LCD_WELCOME_MS), the scroll cadence, and the backlight
// auto-off. All time-based, non-blocking.
void display_loop();

// No-LCD stub mode. When HAS_LCD is undefined these are inline
// no-ops so the call-sites don't need #ifdef guards of their own.
#if !defined(HAS_LCD)
inline void display_init() {}
inline void display_show_console(const char* /*name*/, const char* /*tagline*/) {}
inline void display_show_status(const char* /*line1*/, const char* /*line2*/) {}
inline void display_wake() {}
inline void display_loop() {}
#endif

#endif  // RR_DISPLAY_H
