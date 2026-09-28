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
LiquidCrystal_I2C lcd((pcf8574Address)LCD_I2C_ADDR);

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

// Milliseconds from `from` to `now`, as a signed quantity.
//
// Both operands are millis() stamps, so a plain uint32_t subtraction
// looks like the obvious thing to write -- and is quietly wrong
// whenever `from` lies in the future, which is exactly how a
// hold-until deadline is expressed. The subtraction wraps to a very
// large positive number, every "has enough time passed?" test then
// reads true, and the hold it was meant to impose silently never
// happens. Signed arithmetic yields a negative number instead, which
// compares correctly against a positive interval.
static int32_t elapsedSince(uint32_t now, uint32_t from) {
	return (int32_t)(now - from);
}

// Per-line scroll state. Each line independently:
//   - Holds at the left edge for LCD_SCROLL_PAUSE_MS after a fresh
//     show_console, and after the welcome screen hands over, so the
//     operator can read the start of the line before it moves.
//   - Then scrolls right by 1 char every LCD_SCROLL_MS, for as long
//     as the line exceeds LCD_COLS.
//   - Loops forever. The window is taken from a stream that repeats
//     the line (see lcd_print_repeating), so the wrap is seamless.
struct LineScroll {
	int offset = 0;
	uint32_t lastTickMs = 0;   // millis() stamp of the last step
	uint32_t holdUntilMs = 0;  // don't step before this stamp
	// True while still inside the initial hold.
	bool holding(uint32_t now) const {
		return elapsedSince(now, holdUntilMs) < 0;
	}
	bool needsScroll() const {
		return scrollable && currentLine.length() > LCD_COLS;
	}
	// One full cycle of the marquee: the string, plus the blanks that
	// separate it from its own next repeat. offset wraps at this.
	int period() const {
		return (int)currentLine.length() + LCD_LOOP_GAP;
	}
	const String& currentLine;  // bound to currentLine1 / currentLine2 by the caller
	// False for a status message (see display_show_status) -- the row
	// is pinned in place instead of marqueeing, however long the text
	// runs. Defaults to true so display_show_console()'s scrolling is
	// unchanged for every caller that doesn't opt out. Declared last
	// because the scroll1 / scroll2 instances below aggregate-init it
	// positionally with {0, 0, 0, currentLineN}.
	bool scrollable = true;
};

static LineScroll scroll1{0, 0, 0, currentLine1};
static LineScroll scroll2{0, 0, 0, currentLine2};

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

// Paint a line that fits the display: the whole string, space-padded
// so the previous contents are overwritten cleanly. No scrolling, no
// repeat.
static void lcd_print_fitted(const String& s, char row) {
	lcd.setCursor(0, row);
	for (int i = 0; i < LCD_COLS; ++i) {
		lcd.print(i < (int)s.length() ? s.charAt(i) : ' ');
	}
}

// Paint one marquee window: LCD_COLS characters taken from a stream
// that repeats `s` end-to-start forever, separated by LCD_LOOP_GAP
// blanks.
//
// This is what makes the loop seamless. The character that exits on
// the left is immediately followed by the first character of the next
// repeat arriving on the right in the same tick, so there is no jump
// and no blank frame at the seam. The gap is what keeps that readable
// -- without it the tail of the line would butt straight against its
// own head ("SystemSuper") and look like a typo.
static void lcd_print_repeating(const String& s, int offset, char row) {
	lcd.setCursor(0, row);
	const int len = (int)s.length();
	if (len == 0) {
		for (int i = 0; i < LCD_COLS; ++i) lcd.print(' ');
		return;
	}
	const int period = len + LCD_LOOP_GAP;
	for (int i = 0; i < LCD_COLS; ++i) {
		const int idx = (offset + i) % period;
		lcd.print(idx < len ? s.charAt(idx) : ' ');
	}
}

static void repaint_line(LineScroll& scroll, int row) {
	// Repaint one row from `scroll.currentLine` at `scroll.offset`.
	// Bound to currentLine1 or currentLine2 by whoever owns the
	// LineScroll instance.
	if (!scroll.needsScroll()) {
		lcd_print_fitted(scroll.currentLine, row);
		return;
	}
	lcd_print_repeating(scroll.currentLine, scroll.offset, row);
}

static void enter_phase(DisplayPhase next, uint32_t nowMs) {
	phase = next;
	phaseStartedAtMs = nowMs;
	if (next == DisplayPhase::Boot) {
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print("RetroRoom");
		lcd.setCursor(0, 1);
		lcd.print("Loading");
	} else if (next == DisplayPhase::Welcome) {
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print("RetroRoom");
		lcd.setCursor(0, 1);
		lcd.print("Sit and Play");
	} else if (next == DisplayPhase::Live) {
		lcd.clear();
		// Arm the same hold a fresh show_console() would, so the
		// console name is readable for LCD_SCROLL_PAUSE_MS after the
		// welcome screen hands over rather than marching off the
		// left edge the instant it appears.
		const uint32_t now = millis();
		scroll1.offset = 0;
		scroll1.lastTickMs = now;
		scroll1.holdUntilMs = now + LCD_SCROLL_PAUSE_MS;
		scroll2.offset = 0;
		scroll2.lastTickMs = now;
		scroll2.holdUntilMs = now + LCD_SCROLL_PAUSE_MS;
		// Repaint immediately so the operator sees the current
		// console name without waiting for the next loop() tick.
		repaint_line(scroll1, 0);
		repaint_line(scroll2, 1);
	}
}

// Print an address as 0xNN without relying on Serial.printf, which the
// RP2040 core's Serial doesn't provide.
static void printI2cAddr(uint8_t addr) {
	Serial.print("0x");
	if (addr < 0x10) {
		Serial.print('0');
	}
	Serial.print((int)addr, HEX);
}

// Walk the 7-bit I2C address space and report every device that ACKs,
// writing up to `maxFound` addresses into `found`. Returns the total
// number of responders (which may exceed maxFound).
//
// Why: a breadboard bring-up with one PCF8574 backpack has exactly one
// failure mode that is invisible. The probe below addresses a single
// hardcoded address and, on a NAK, returns with no output at all -- so
// a blank LCD is indistinguishable from a wiring mistake. The PCF8574
// and PCF8574A live at different addresses (0x20-0x27 vs 0x38-0x3F
// depending on the A2/A1/A0 jumpers), and 0x3F clones are extremely
// common, so the default is wrong often enough to matter.
//
// Time-boxed on purpose. A healthy NAK returns in microseconds, but on
// a miswired bus (SDA held low, no pull-ups) every probe can burn the
// full Wire timeout; at 112 addresses that would stall boot for
// seconds and look exactly like the wedge the probe guard exists to
// prevent. The deadline turns that into one clear "bus stuck" line.
static uint8_t lcd_scanBus(uint8_t* found, uint8_t maxFound, uint32_t budgetMs) {
	const uint32_t startedAt = millis();
	uint8_t count = 0;
	for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
		if (millis() - startedAt > budgetMs) {
			break;
		}
		Wire.beginTransmission(addr);
		if (Wire.endTransmission() == 0) {
			if (count < maxFound) {
				found[count] = addr;
			}
			++count;
		}
	}
	return count;
}

void display_init() {
	// Wire defaults to GP4/GP5 on the rpipico2w variant, but we set
	// the pins explicitly so the intent is visible at the call site.
	// The rp2040 Wire library doesn't expose a 2-arg begin(SDA, SCL)
	// overload -- only setSDA/setSCL + the 0/1-arg begin().
	Wire.setSDA(LCD_I2C_SDA_PIN);
	Wire.setSCL(LCD_I2C_SCL_PIN);
	Wire.begin();

	// Report what's on the bus before deciding whether to drive the
	// LCD. Purely diagnostic -- the probe below still decides whether
	// the driver engages.
	uint8_t busDevices[8];
	const uint8_t busCount = lcd_scanBus(busDevices, sizeof(busDevices), 250);
	Serial.print(F("[display] I2C bus GP"));
	Serial.print((int)LCD_I2C_SDA_PIN);
	Serial.print(F("/GP"));
	Serial.print((int)LCD_I2C_SCL_PIN);
	Serial.print(F(": "));
	if (busCount == 0) {
		Serial.println(F("no devices responded (bus idle, or SDA/SCL "
						 "miswired / missing pull-ups)"));
	} else {
		Serial.print(busCount);
		Serial.print(F(" device(s):"));
		for (uint8_t i = 0; i < busCount && i < sizeof(busDevices); ++i) {
			Serial.print(' ');
			printI2cAddr(busDevices[i]);
		}
		Serial.println();
	}

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
		Serial.print(F("[display] LCD not found at configured "));
		printI2cAddr((uint8_t)LCD_I2C_ADDR);
		Serial.println(F(" -- LCD driver disabled. If a device above "
						 "looks like a PCF8574 backpack, rebuild with "
						 "-DLCD_I2C_ADDR=<that address>."));
		lcdPresent = false;
		return;
	}
	Serial.print(F("[display] LCD found at "));
	printI2cAddr((uint8_t)LCD_I2C_ADDR);
	Serial.println();

	// lcd.begin() now goes through the full HD44780 4-bit init
	// sequence (delay(500) + 11 _send() calls). On a real PCF8574
	// backpack this works; on a missing device it NAKs each write but
	// the Wire._timeout bounds each call so init still completes.
	lcd.begin();
	lcd.backlight();
	backlightOn = true;

	// Seed the live lines from the console that's already selected.
	// selectConsole() fires during consoleDefinitions(), which runs
	// before display_init(), and display_show_console() no-ops until
	// lcdPresent is set -- so without this the LCD hands over from the
	// welcome screen still showing the welcome text. Guarded on the
	// console count because CurrentConsole() indexes without a bounds
	// check. The welcome strings stay as the empty-list fallback.
	if (HowManyConsoles() > 0) {
		const auto& c = CurrentConsole();
		currentLine1 = String(c.name.c_str());
		currentLine2 = String(c.tagline.c_str());
	} else {
		currentLine1 = "RetroRoom";
		currentLine2 = "Configure Req'd";
	}

	lcdPresent = true;
	enter_phase(DisplayPhase::Boot, millis());
}

// Shared body of display_show_console() and display_show_status():
// load the two line buffers and rewind both marquee windows to the
// left edge, holding there for LCD_SCROLL_PAUSE_MS. `scrollable` is
// false for a status message, which pins both rows in place instead
// of letting the marquee take them over.
//
// Callers must have already checked lcdPresent.
static void show_lines(const char* line1, const char* line2, bool scrollable) {
	currentLine1 = line1 ? String(line1) : String("");
	currentLine2 = line2 ? String(line2) : String("");
	const uint32_t now = millis();
	scroll1.offset = 0;
	scroll1.lastTickMs = now;
	scroll1.holdUntilMs = now + LCD_SCROLL_PAUSE_MS;
	scroll1.scrollable = scrollable;
	scroll2.offset = 0;
	scroll2.lastTickMs = now;
	scroll2.holdUntilMs = now + LCD_SCROLL_PAUSE_MS;
	scroll2.scrollable = scrollable;
	// Force the live phase so the welcome screen doesn't overwrite
	// the message when the caller fires within the first
	// LCD_WELCOME_MS of boot. enter_phase() repaints both rows and
	// rewinds the offsets itself, but leaves `scrollable` alone --
	// which is what we want, since we set it above.
	if (phase != DisplayPhase::Live) {
		enter_phase(DisplayPhase::Live, now);
		return;
	}
	repaint_line(scroll1, 0);
	repaint_line(scroll2, 1);
}

void display_show_console(const char* name, const char* tagline) {
	// Short-circuit when no LCD was detected at boot. Saves the
	// selectConsole() call-site from needing to know whether the LCD
	// driver is enabled.
	if (!lcdPresent) {
		return;
	}
	// Called from selectConsole() on every advance / rewind. The freshly-
	// selected console starts at the left edge, held there for
	// LCD_SCROLL_PAUSE_MS so the operator gets a beat to read the
	// start of the line before the marquee takes over. After that it
	// loops continuously.
	show_lines(name, tagline, /*scrollable=*/true);
}

void display_show_status(const char* line1, const char* line2) {
	// Short-circuit when no LCD was detected at boot, same as
	// display_show_console().
	if (!lcdPresent) {
		return;
	}
	show_lines(line1, line2, /*scrollable=*/false);
	// Force the backlight on and re-arm the auto-off deadline. The
	// caller is on a path that ends in a reset, so a display that had
	// been idle long enough to blank itself would otherwise show a
	// dark panel for the fraction of a second the message is up.
	if (!backlightOn) {
		lcd.backlight();
		backlightOn = true;
	}
	backlightOffAtMs = millis() + lcdBacklightOffAfterMs;
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
	if (!scroll.needsScroll()) {
		return;  // short line; nothing to scroll
	}
	const uint32_t now = millis();
	// Still inside the initial hold? Don't move. Checked with signed
	// arithmetic because holdUntilMs is legitimately in the future.
	if (scroll.holding(now)) {
		return;
	}
	if (elapsedSince(now, scroll.lastTickMs) < (int32_t)LCD_SCROLL_MS) {
		return;
	}
	scroll.lastTickMs = now;
	// One step forward, wrapping at the end of a full cycle. The wrap
	// is invisible: lcd_print_repeating() renders the window from the
	// repeating stream, so the character that reappears on the right
	// is the one that just left on the left. No snap-back, no pause.
	scroll.offset = (scroll.offset + 1) % scroll.period();
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
void display_show_status(const char*, const char*) {}
void display_wake() {}
void display_loop() {}

#endif  // HAS_LCD
