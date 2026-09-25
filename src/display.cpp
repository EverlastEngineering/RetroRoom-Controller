// 16x2 I2C LCD driver -- implementation. See display.h for the
// behavior contract.
//
// This TU compiles out cleanly when HAS_LCD is undefined (the inline
// stubs in display.h take over). When HAS_LCD is defined the TU
// pulls in <Wire.h> + <LiquidCrystal_I2C.h> and owns:
//   - the LiquidCrystal_I2C instance (declared extern in display.h)
//   - the welcome -> live transition timer
//   - the scroll timers (one per line)
//   - the backlight-off timer
//   - a backlight-on flag (toggled by display_wake() / the timer)

#include "display.h"

#if defined(HAS_LCD)

#include <string.h>

#include "consoles.h"

// Construct the LCD with the library's default PCF8574-pin mapping
// (P0..P7 = 4,5,6,16,11,12,13,14). The constructor switches on each
// pin number to fill the _lcdToPCF8574[] lookup; passing the wrong
// pin numbers (e.g. LCD_COLS=16, LCD_ROWS=2) makes the lookup fall
// through to the `default:` arm and sets _pcf8574PortsMaping=false,
// which makes lcd.begin() bail out on its safety check -- meaning
// every subsequent LCD call would silently fail. The default pin
// mapping matches the HD44780 + PCF8574 backpack pinout used by the
// enjoyneering library and is what the library tests against; we
// pass LCD_COLS / LCD_ROWS to lcd.begin() instead.
LiquidCrystal_I2C lcd(LCD_I2C_ADDR);

// State machine phases. Boot -> welcome -> live; the welcome phase
// just paints the static "RetroRoom" / "Sit and Play" lines and
// waits LCD_WELCOME_MS before transitioning to live.
enum class DisplayPhase {
	Boot,
	Welcome,
	Live,
};

static DisplayPhase phase = DisplayPhase::Boot;
static uint32_t phaseStartedAtMs = 0;

// The two text lines currently shown on the LCD. Kept as plain
// String/char so the scroll logic can strncmp against them.
static String currentLine1;
static String currentLine2;

// Per-line scroll state. Each line independently:
//   - Holds at the left edge for LCD_SCROLL_PAUSE_MS after a fresh
//     show_console.
//   - Scrolls right by 1 char every LCD_SCROLL_MS, only when the
//     line exceeds LCD_COLS.
//   - When the trailing edge reaches the natural end, snaps back
//     to the left edge and pauses again.
struct LineScroll {
	int offset = 0;
	uint32_t lastTickMs = 0;
	bool needsScroll() const {
		return currentLine.length() > LCD_COLS;
	}
	const String& currentLine;  // bound to currentLine1 / currentLine2 by the caller
};

static LineScroll scroll1{0, 0, currentLine1};
static LineScroll scroll2{0, 0, currentLine2};

// Backlight state. lcd.backlight() / lcd.noBacklight() drive the
// PCF8574 backpack's BL bit.
static bool backlightOn = false;
static uint32_t backlightOffAtMs = 0;

// Probed in display_init(). When false, every public display_*
// function short-circuits -- the firmware stays usable on the bench
// without the perfboard wired up. We can't just rely on lcd.begin()'s
// return value because the HD44780 init sequence keeps doing I2C
// writes via Wire.endTransmission() (each one timing out at ~25 ms
// when NAKed), so on a missing device the whole boot sequence can
// block for 1-2 s before returning -- enough to look like a wedge
// when the device is supposed to come up in <1 s.
static bool lcdPresent = false;

static void lcd_print_padded(const String& s, int offset, char row) {
	// Paint `LCD_COLS` chars from `s` starting at `offset`. When the
	// string is shorter than LCD_COLS+offset, pad with spaces so any
	// previous characters are overwritten cleanly.
	lcd.setCursor(0, row);
	if (offset >= s.length()) {
		// Past the end -- blank line.
		for (int i = 0; i < LCD_COLS; ++i) lcd.print(' ');
		return;
	}
	const char* p = s.c_str() + offset;
	int remaining = s.length() - offset;
	int n = remaining < LCD_COLS ? remaining : LCD_COLS;
	for (int i = 0; i < n; ++i) lcd.print(p[i]);
	for (int i = n; i < LCD_COLS; ++i) lcd.print(' ');
}

static void repaint_line(LineScroll& scroll, int row) {
	// Repaint one row from `scroll.currentLine` at `scroll.offset`.
	// Bound to currentLine1 or currentLine2 by whoever owns the
	// LineScroll instance.
	if (scroll.currentLine.length() <= LCD_COLS) {
		// Short line -- just paint the whole thing at offset 0.
		lcd_print_padded(scroll.currentLine, 0, row);
		return;
	}
	lcd_print_padded(scroll.currentLine, scroll.offset, row);
}

static void enter_phase(DisplayPhase next, uint32_t nowMs) {
	phase = next;
	phaseStartedAtMs = nowMs;
	if (next == DisplayPhase::Welcome) {
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print("RetroRoom");
		lcd.setCursor(0, 1);
		lcd.print("Sit and Play");
	} else if (next == DisplayPhase::Live) {
		lcd.clear();
		// Repaint immediately so the operator sees the current
		// console name without waiting for the next loop() tick.
		repaint_line(scroll1, 0);
		repaint_line(scroll2, 1);
	}
}

void display_init() {
	// Wire defaults to GP4/GP5 on the rpipico2w variant, but we set
	// the pins explicitly so the intent is visible at the call site.
	// The rp2040 Wire library doesn't expose a 2-arg begin(SDA, SCL)
	// overload -- only setSDA/setSCL + the 0/1-arg begin().
	Wire.setSDA(LCD_I2C_SDA_PIN);
	Wire.setSCL(LCD_I2C_SCL_PIN);
	Wire.begin();

	// Probe the bus for the LCD before letting lcd.begin() run.
	// Without this guard, lcd.begin() runs the HD44780 init sequence
	// (delay(500) + 11 _send() calls + multiple LCD_CLEAR_DISPLAY
	// writes) even when no PCF8574 is at the address. Each I2C write
	// then waits the full Wire._timeout (25 ms by default) before
	// returning NAK, so the whole sequence can block for 1-2 s and
	// the firmware appears wedged during boot. With the probe guard,
	// bench testing without the perfboard wired boots in milliseconds
	// and the rest of the driver becomes a no-op via the
	// `lcdPresent` flag.
	Wire.beginTransmission(LCD_I2C_ADDR);
	const uint8_t probeResult = Wire.endTransmission();
	if (probeResult != 0) {
		lcdPresent = false;
		return;
	}

	// lcd.begin() now goes through the full HD44780 4-bit init
	// sequence (delay(500) + 11 _send() calls). On a real PCF8574
	// backpack this works; on a missing device it NAKs each write but
	// the Wire._timeout bounds each call so init still completes.
	lcd.begin();
	lcd.backlight();
	backlightOn = true;
	currentLine1 = "RetroRoom";
	currentLine2 = "Sit and Play";
	lcdPresent = true;
	enter_phase(DisplayPhase::Welcome, millis());
}

void display_show_console(const char* name, const char* tagline) {
	// Short-circuit when no LCD was detected at boot. Saves the
	// selectConsole() call-site from needing to know whether the LCD
	// driver is enabled.
	if (!lcdPresent) {
		return;
	}
	// Called from selectConsole() on every advance / rewind. Update
	// the line buffers and reset the scroll offsets so the freshly-
	// selected console starts at the left edge with a full pause.
	currentLine1 = name ? String(name) : String("");
	currentLine2 = tagline ? String(tagline) : String("");
	scroll1.offset = 0;
	scroll1.lastTickMs = millis();
	scroll2.offset = 0;
	scroll2.lastTickMs = millis();
	// Force the live phase so the welcome screen doesn't overwrite
	// the console name when the operator does a /next within the
	// first LCD_WELCOME_MS of boot.
	if (phase != DisplayPhase::Live) {
		enter_phase(DisplayPhase::Live, millis());
	} else {
		repaint_line(scroll1, 0);
		repaint_line(scroll2, 1);
	}
}

void display_wake() {
	// Reset the backlight-off deadline to "now + lcdBacklightOffAfterMs".
	// lcdBacklightOffAfterMs is the shell-side global set by
	// consoleDefinitions(); see src/consoles.cpp. Short-circuit when
	// no LCD was detected so we don't poke lcd.backlight() on a
	// non-existent device.
	if (!lcdPresent) {
		return;
	}
	if (!backlightOn) {
		lcd.backlight();
		backlightOn = true;
	}
	backlightOffAtMs = millis() + lcdBacklightOffAfterMs;
}

static void tick_scroll(LineScroll& scroll, int row) {
	if (scroll.currentLine.length() <= LCD_COLS) {
		return;  // short line; no scrolling
	}
	const uint32_t now = millis();
	if (now - scroll.lastTickMs < LCD_SCROLL_MS) {
		return;
	}
	scroll.lastTickMs = now;
	// After offset reaches (length - LCD_COLS) we've shown the tail;
	// snap back to 0 and pause for LCD_SCROLL_PAUSE_MS.
	if (scroll.offset >= scroll.currentLine.length() - LCD_COLS) {
		scroll.offset = 0;
		// The pause uses LCD_SCROLL_PAUSE_MS by extending the next
		// tick's deadline. Cheap: just bump lastTickMs forward.
		scroll.lastTickMs = now + LCD_SCROLL_PAUSE_MS - LCD_SCROLL_MS;
		repaint_line(scroll, row);
		return;
	}
	++scroll.offset;
	repaint_line(scroll, row);
}

void display_loop() {
	// Short-circuit when no LCD was detected at boot.
	if (!lcdPresent) {
		return;
	}
	const uint32_t now = millis();
	switch (phase) {
		case DisplayPhase::Boot:
			// display_init() should have been called; if not, enter
			// Welcome now. Guarded by `backlightOn` so init's own
			// welcome doesn't double up.
			if (backlightOn) {
				enter_phase(DisplayPhase::Welcome, now);
			}
			break;
		case DisplayPhase::Welcome:
			if (now - phaseStartedAtMs >= LCD_WELCOME_MS) {
				enter_phase(DisplayPhase::Live, now);
			}
			break;
		case DisplayPhase::Live:
			tick_scroll(scroll1, 0);
			tick_scroll(scroll2, 1);
			break;
	}

	// Backlight off when the deadline expires. 0 = never off.
	if (backlightOn && lcdBacklightOffAfterMs > 0 && now >= backlightOffAtMs) {
		lcd.noBacklight();
		backlightOn = false;
	}
}

#else  // !HAS_LCD

// Stub implementations when the LCD driver is excluded from the
// build. The display.h inline stubs handle the call-sites; this TU
// is still required to exist (it's in src/ and src/main.cpp links it).
void display_init() {}
void display_show_console(const char*, const char*) {}
void display_wake() {}
void display_loop() {}

#endif  // HAS_LCD
