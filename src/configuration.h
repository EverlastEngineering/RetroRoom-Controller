#ifndef RRCONFIGURATION_H
#define RRCONFIGURATION_H

// lighting
#define NUM_RING_LEDS 8

// How long the ring stays lit after a rotary turn before it fades out
// and the browsed cursor reverts to the selected console. Suppressed
// entirely while the capacitive proximity pad reports a hand at the
// knob. 0 disables the timeout (the ring then only goes dark on an
// explicit select).
//
// A #define for now. Several tunables are expected to move into
// /consoles.json later, and this is one of them; the knob is read from
// exactly one place so that move should not need rework.
#ifndef RING_HIGHLIGHT_IDLE_MS
#define RING_HIGHLIGHT_IDLE_MS 5000
#endif

// ---------------------------------------------------------------------------
// LED string (GP21) browse + selection feel
// ---------------------------------------------------------------------------
//
// Every number that shapes how the string strip *feels* lives here, so
// the whole effect can be retuned on the bench without touching
// src/ledstring.cpp or lib/LedStringPaint. src/ledstring.cpp reads these
// once at init and builds the corresponding retroroom_core config
// structs; nothing else hard-codes a timing.
//
// The three behaviours these drive, in the order the operator meets
// them:
//
//   1. BROWSE. A console step no longer takes one detent -- it takes
//      LEDSTRING_DETENTS_PER_STEP of them, with a blob creeping along
//      the strip toward the next console as they accumulate. The final
//      detent snaps the cursor and starts the preview pulse. Spin fast
//      and the requirement drops to LEDSTRING_FAST_DETENTS_PER_STEP.
//
//   2. PREVIEW. The console about to be selected pulses in its
//      JSON-defined window (ledPosition / ledWidth).
//
//   3. SELECT. On commit the whole strip twinkles and collapses in
//      under a second to just the consoles above the selected one.

// Duration of the scripted travel that plays when a browse step
// completes -- the block of light moving off the current console and
// onto the one the operator picked.
#ifndef LEDSTRING_TRAVEL_MS
#define LEDSTRING_TRAVEL_MS 420
#endif

// How wide the travelling block is at the widest point of the travel,
// in LEDs. The block starts as a small spark on the console being left
// and ends exactly the width of the console it lands on; this is the
// peak in between, and it is the whole reason the travel reads as a
// block of light rather than a dot sliding along.
//
// It is a *cap* on how wide the block gets, not a target: the block
// always ends exactly the width of the console it lands on, whatever
// this says, because travelEdges() takes this as a maximum and floors
// it at the target's own width. So setting this too small does not make
// a slimmer block -- it made the block fail to cover wide consoles at
// all, arriving as a sliver against the far end of the window. A wide
// console is safe; a value below the widest window is simply ignored.
#ifndef LEDSTRING_TRAVEL_PEAK_WIDTH
#define LEDSTRING_TRAVEL_PEAK_WIDTH 6
#endif

// How wide the spark is where the block leaves the console behind it.
// Two reads as an object; one reads as a stray pixel.
#ifndef LEDSTRING_TRAVEL_SPARK_LEDS
#define LEDSTRING_TRAVEL_SPARK_LEDS 2
#endif

// Floor on the knob-turn progression indicator's run, in LEDs.
//
// The fill is normalised to the gap between two consoles, which is
// normally the right thing -- the operator's four detents walk across
// whatever space is actually there. But a step *between shelves* is a
// couple of pixels in index space and a long way round physically, and
// filling the literal gap would leave the indicator barely moving on
// exactly the steps where it is hardest to see what is happening. The
// floor keeps the knob feeling the same on every step in the cabinet.
#ifndef LEDSTRING_FILL_MIN_LEDS
#define LEDSTRING_FILL_MIN_LEDS 3
#endif

// Brightness of the knob-turn progression indicator. Deliberately a
// third level, between the consoles already in the stack and the
// selection itself: sharing a level with the stack would make "where
// the stack ends" and "how far I have got" the same fact, leaving
// nothing to read progress from.
#ifndef LEDSTRING_FILL_PCT
#define LEDSTRING_FILL_PCT 45
#endif

// How long the knob must be quiet before the progression run starts
// giving itself back, in ms. 0 disables the retreat entirely.
//
// The run is left pointing at whatever the operator was turning toward
// when they stopped. If that was a console past the one they meant --
// an overshoot, or a spin abandoned half way -- the strip keeps
// pointing at it indefinitely, which reads as the browse still being in
// progress long after the operator has finished.
//
// So after this long with no detent, the run is withdrawn one LED at a
// time from its leading edge, which is what rolling the knob back by
// the overshoot would have looked like, and it finishes on the pulsing
// selection on its own. Turning the knob again cancels it immediately
// and starts a new fill; the retreat never fights the operator.
#ifndef LEDSTRING_FILL_RETREAT_DELAY_MS
#define LEDSTRING_FILL_RETREAT_DELAY_MS 3000
#endif

// How long one LED takes to be given back, in ms. This is also the
// fade: a LED dims across this window rather than being switched off,
// and the next starts as the previous finishes, so the run reels in
// instead of strobing.
//
// It is a duration rather than a LED count on purpose. Retreating a
// whole LED at a time cannot fade, and the snap is what makes a
// countdown look like a fault rather than a release.
//
// This has to stay comfortably shorter than the ring's idle timeout
// divided by the longest run in the cabinet, because that timeout waits
// for the retreat to finish before it gives up (see
// ledstring_fillRetreatInProgress). A retreat that outlasts the ring's
// patience is fine -- the countdown is held, not shortened -- but a
// *fast* retreat is what makes the give-up feel prompt afterwards.
#ifndef LEDSTRING_FILL_RETREAT_STEP_MS
#define LEDSTRING_FILL_RETREAT_STEP_MS 250
#endif

// Detents of the rotary knob required to move the browse cursor one
// console when the operator is turning deliberately.
#ifndef LEDSTRING_DETENTS_PER_STEP
#define LEDSTRING_DETENTS_PER_STEP 5
#endif

// Detents required once they are spinning. Clamped to
// LEDSTRING_DETENTS_PER_STEP at init if set higher.
#ifndef LEDSTRING_FAST_DETENTS_PER_STEP
#define LEDSTRING_FAST_DETENTS_PER_STEP 2
#endif

// Rotary detents are ignored for this long after a step completes, in
// ms. 0 disables it.
//
// Landing on the fifth detent is hard: a hand that overshoots by one
// immediately starts filling toward the *next* console, so a single
// mistimed turn costs two steps and the overshoot looks like the knob
// having a mind of its own. Swallowing the detents that arrive right
// after a commit absorbs the overshoot without making the knob feel
// sticky, because it only bites immediately after a snap -- the exact
// moment the operator is not trying to go anywhere.
//
// Deliberately shorter than LEDSTRING_TRAVEL_MS. The travel is still
// playing when the lockout ends, so turning after it expires cuts the
// animation short rather than queueing behind it. That is the existing
// behaviour for a detent arriving mid-travel and this does not make it
// worse; making the lockout cover the whole travel would swallow real
// input for twice as long.
#ifndef LEDSTRING_BROWSE_SETTLE_LOCKOUT_MS
#define LEDSTRING_BROWSE_SETTLE_LOCKOUT_MS 250
#endif

// A detent arriving within this many ms of the previous one marks the
// spin as fast. **0 disables the escalation entirely**, which is the
// current setting.
//
// DISABLED, and the reason is worth keeping: this was 1000 ms, which
// sounds generous but is *shorter than a deliberate human detent*. On
// the bench the first detent registered as deliberate, the second
// tripped the window, and from there on the browse was permanently in
// fast mode -- so 1/5 then 2/5 then straight to the fast cadence, and
// the slow path could only be reached by turning the knob unnaturally
// slowly. The window has to be comfortably LONGER than the operator's
// slowest deliberate rhythm or it will never be slower than fast.
//
// We do not have a number for that slowest deliberate rhythm yet, which
// is the reason it is off rather than merely retuned. Re-enable it only
// once someone has timed their own deliberate turn; a starting guess
// would be 2000-3000 ms. The latching that compounded this (one fast
// detent locking the whole browse into fast) is fixed independently --
// the escalation now reflects the gap before *this* detent, so it
// drops back to the slow cadence the moment they slow down.
#ifndef LEDSTRING_FAST_SPIN_WINDOW_MS
#define LEDSTRING_FAST_SPIN_WINDOW_MS 0
#endif

// Width in pixels of the travelling blob. Wider reads as more mass
// moving; 1-2 reads as a cursor. The blob clamps into the strip, so a
// value larger than the strip is truncated rather than wrapping.
#ifndef LEDSTRING_BLOB_WIDTH
#define LEDSTRING_BLOB_WIDTH 3
#endif

// How long one cycle of the preview pulse takes. Shorter reads as a
// heartbeat, longer as a slow breath. The glow follows a parabola over
// the cycle, so it peaks mid-cycle and falls away at both ends.
#ifndef LEDSTRING_PREVIEW_PULSE_MS
#define LEDSTRING_PREVIEW_PULSE_MS 1100
#endif

// Brightness of the preview pulse at its dimmest and brightest, as a
// percentage of LEDSTRING_COLOR_*. The dim end should stay clearly
// non-zero or the pulse strobes rather than breathes.
#ifndef LEDSTRING_PREVIEW_PULSE_MIN_PCT
#define LEDSTRING_PREVIEW_PULSE_MIN_PCT 30
#endif
#ifndef LEDSTRING_PREVIEW_PULSE_MAX_PCT
#define LEDSTRING_PREVIEW_PULSE_MAX_PCT 100
#endif

// Total length of the selection effect. The requirement is that it
// finishes in under a second; the per-pixel stagger is subtracted from
// the ramp rather than added to the total, so raising it cannot push
// the effect past this budget.
#ifndef LEDSTRING_SELECT_EFFECT_MS
#define LEDSTRING_SELECT_EFFECT_MS 900
#endif

// Portion of the selection effect spent twinkling the whole strip
// before it starts collapsing. Clamped to LEDSTRING_SELECT_EFFECT_MS.
#ifndef LEDSTRING_SELECT_TWINKLE_MS
#define LEDSTRING_SELECT_TWINKLE_MS 350
#endif

// Per-pixel delay on the collapse ramp, so the strip settles as a
// ripple rather than snapping in one frame. The cost is taken out of
// the ramp length, so the total stays inside LEDSTRING_SELECT_EFFECT_MS.
#ifndef LEDSTRING_SELECT_STAGGER_MS
#define LEDSTRING_SELECT_STAGGER_MS 6
#endif

// Dimmest and brightest samples of the twinkle, as a percentage of
// LEDSTRING_COLOR_*. The twinkle deliberately dips near zero so the
// sparkle has contrast.
#ifndef LEDSTRING_SELECT_TWINKLE_MIN_PCT
#define LEDSTRING_SELECT_TWINKLE_MIN_PCT 10
#endif
#ifndef LEDSTRING_SELECT_TWINKLE_MAX_PCT
#define LEDSTRING_SELECT_TWINKLE_MAX_PCT 100
#endif

// What the strip settles to once everything is over.
//
//   ABOVE -- the consoles *above* the selected one, dimly lit. **0 by
//            default**: the resting strip shows the selection and
//            nothing else. A cumulative reading (select the third
//            console and the first two stay lit) is available by
//            raising this, but it was reported from the bench as
//            reading as "the whole string is lit" rather than as a
//            stack, so it is not the default.
//
//            Note this is the *resting* state only. While a browse is
//            under way the consoles behind the operator's turn stay
//            visible at LEDSTRING_DIM_PCT, because that is what the
//            animation travels over.
//
//   SELF  -- the selected console's own window, at full. Nothing
//            paints over it; the selection is the brightest thing on
//            the strip.
#ifndef LEDSTRING_ABOVE_PCT
#define LEDSTRING_ABOVE_PCT 0
#endif
#ifndef LEDSTRING_SELF_PCT
#define LEDSTRING_SELF_PCT 100
#endif

// Brightness of everything the strip is showing *around* the thing the
// operator is currently looking at: the consoles behind them during a
// browse, and the consoles above the proposal while it pulses. Distinct
// from LEDSTRING_ABOVE_PCT because that one is a resting-state choice
// and this one is part of the animation -- the proposal has to have
// something to be compared against.
#ifndef LEDSTRING_DIM_PCT
#define LEDSTRING_DIM_PCT 22
#endif

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
//
// The strip can be showing several things at once, and brightness alone
// cannot separate them: two things at 45% are indistinguishable from
// two things at 45%. Each role the strip can be showing therefore gets
// its own colour, and the LEDSTRING_*_PCT values above scale that
// colour's intensity rather than picking a shade.
//
// The defaults are a warm amber for everything the operator is being
// *offered* and a cool blue for the context they are choosing against.
// That is the one distinction worth having by eye alone: which lights
// are the thing you are about to turn on, and which are already-on
// state. Tune the two groups independently -- a cabinet that reads as
// one colour in a dark room may want the context group much dimmer
// rather than a different hue.
//
// Conservative values throughout: this strip sits next to a television
// in a dark room, and a misconfigured colour here is a glare problem
// rather than an aesthetic one.
#ifndef LEDSTRING_COLOR_SELECTED_R
#define LEDSTRING_COLOR_SELECTED_R 48
#endif
#ifndef LEDSTRING_COLOR_SELECTED_G
#define LEDSTRING_COLOR_SELECTED_G 36
#endif
#ifndef LEDSTRING_COLOR_SELECTED_B
#define LEDSTRING_COLOR_SELECTED_B 24
#endif

// The console a click would select, pulsing. The amber of the
// selection: it is the same promise, made before you have committed.
#ifndef LEDSTRING_COLOR_PROPOSAL_R
#define LEDSTRING_COLOR_PROPOSAL_R 64
#endif
#ifndef LEDSTRING_COLOR_PROPOSAL_G
#define LEDSTRING_COLOR_PROPOSAL_G 40
#endif
#ifndef LEDSTRING_COLOR_PROPOSAL_B
#define LEDSTRING_COLOR_PROPOSAL_B 8
#endif

// The block of light moving between consoles. The brightest thing on
// the strip while it moves, because it is the only thing that is
// moving and the eye goes to motion first.
#ifndef LEDSTRING_COLOR_TRAVEL_R
#define LEDSTRING_COLOR_TRAVEL_R 80
#endif
#ifndef LEDSTRING_COLOR_TRAVEL_G
#define LEDSTRING_COLOR_TRAVEL_G 60
#endif
#ifndef LEDSTRING_COLOR_TRAVEL_B
#define LEDSTRING_COLOR_TRAVEL_B 24
#endif

// The knob-turn progression indicator. Cool, so it never reads as a
// console that is on.
#ifndef LEDSTRING_COLOR_FILL_R
#define LEDSTRING_COLOR_FILL_R 16
#endif
#ifndef LEDSTRING_COLOR_FILL_G
#define LEDSTRING_COLOR_FILL_G 40
#endif
#ifndef LEDSTRING_COLOR_FILL_B
#define LEDSTRING_COLOR_FILL_B 56
#endif

// Context: the consoles above the selection at rest, and the console
// being turned away from during a browse. The same cool blue as the
// fill but darker, so the two never compete.
#ifndef LEDSTRING_COLOR_STACK_R
#define LEDSTRING_COLOR_STACK_R 12
#endif
#ifndef LEDSTRING_COLOR_STACK_G
#define LEDSTRING_COLOR_STACK_G 28
#endif
#ifndef LEDSTRING_COLOR_STACK_B
#define LEDSTRING_COLOR_STACK_B 40
#endif

#ifndef LEDSTRING_COLOR_LEAVING_R
#define LEDSTRING_COLOR_LEAVING_R 20
#endif
#ifndef LEDSTRING_COLOR_LEAVING_G
#define LEDSTRING_COLOR_LEAVING_G 34
#endif
#ifndef LEDSTRING_COLOR_LEAVING_B
#define LEDSTRING_COLOR_LEAVING_B 48
#endif

// Brightness percentages for the browse blob and for the two console
// windows it travels between. The windows are dim so the blob is
// unmistakably the brightest thing on the strip while it moves.
#ifndef LEDSTRING_BLOB_PCT
#define LEDSTRING_BLOB_PCT 100
#endif
#ifndef LEDSTRING_BROWSE_FROM_PCT
#define LEDSTRING_BROWSE_FROM_PCT 25
#endif
#ifndef LEDSTRING_BROWSE_TO_PCT
#define LEDSTRING_BROWSE_TO_PCT 45
#endif

// How often an in-flight frame is pushed to the wire while an
// animation is running.
//
// This is a *sampling rate*, not a step count: every frame is computed
// from the animation's elapsed time, so raising it produces the same
// animation played more smoothly rather than the same animation played
// faster. Halve it and the motion is twice as finely sampled; the
// duration and the easing are untouched.
//
// The ceiling is how long FastLED.show() takes to clock 64 WS2812B
// pixels out, plus whatever the rest of loop() needs. That is a
// property of the board and the strip, not something to guess at, so
// the driver measures it: when an animation finishes it prints the
// frame count, the achieved mean frame period, and the worst single
// frame. Read that line on the bench and set this number from it --
// if the worst frame is close to this interval, you are the limit, and
// lowering the number further only starves the rest of the loop.
#ifndef LEDSTRING_FRAME_INTERVAL_MS
#define LEDSTRING_FRAME_INTERVAL_MS 8
#endif


// ESP8266 (NodeMCU v2) and AVR boards were dropped on session/merge-pico-json.
// Only the Raspberry Pi Pico (RP2040) + Earle Philhower's arduino-pico core
// are supported. The ESP-only `#define MANUAL_OE_PIN` is intentionally gone;
// when a perfboard revision lands, MANUAL_OE_PIN will be added back to the
// Pico block (see todo/deferred/ and todo/open/ for the tracked work).

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
	#define LED_RING_DATA_PIN          20  // FastLED ring 
	#define SELECTED_CONSOLE_LED_STRING_DATA 21  // FastLED strip
	#define IR_CONTROL_PIN    22
	#define NUM_SELECTED_CONSOLE_LED_STRING_LEDS 64
	// I2C0 (SDA/SCL) for the 16x2 HD44780 + PCF8574 backpack. Defined here
	// rather than in display.h so that pin-map-chart.md really is the only
	// place pins are declared -- the chart asserts every pin in the table
	// has a matching #define in this file.
	#define LCD_I2C_SDA_PIN 4
	#define LCD_I2C_SCL_PIN 5
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
	#define LED_RING_DATA_PIN 23  // YD-RP2040 onboard WS2812 (PIN_NEOPIXEL)
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