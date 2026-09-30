#pragma once

// Functional core for the 8-pixel selector ring on GP20.
//
// Pure state machine: no Arduino headers, no FastLED, no millis() of its
// own, no globals. The shell wrapper in src/lighting.{h,cpp} feeds it
// events and a clock and pushes the returned levels into the live CRGB
// leds[] buffer. Compiles and unit-tests on the host via PlatformIO +
// Unity.
//
// Why this exists
// ---------------
// The ring used to live entirely in src/lighting.cpp as six mutable
// statics -- ringLit, ringFading, ringProximityHold, ringFadeRequested,
// ringHoldUntilMs, ringFlashUntilMs -- with each function partially
// updating them. Three bugs came out of that in one sitting, all of the
// same shape:
//
//   * a namespace-scope constant froze a config value before it was
//     parsed (see the equivalent fix in src/ledstring.cpp),
//   * the fade was fadeToBlackBy(1) per loop(), so its duration was 255
//     loop iterations rather than a time in milliseconds,
//   * and, the one this file exists to make impossible, a re-light
//     mid-fade set ringFading = false and abandoned the fade with the
//     ring stranded partway, permanently.
//
// That last one is the design error rather than a typo. The ring's
// appearance was *accumulated* -- 255 decrements applied into the pixel
// buffer -- so "how bright is the ring" was a function of history, and
// any state that reset the history lost the progress. Here the level is
// a pure function of (mode, elapsed): a fade that gets interrupted and
// restarted cannot strand the ring, because there is nothing to strand.
// A caller that re-lights mid-fade simply changes the mode, and the
// level follows.
//
// The single RingMode enum is the other half of it. Five booleans that
// can disagree is the reason the old file was hard to read; one enum
// cannot.

#include <cstdint>

namespace retroroom_core {

// Brightness of the dim base fill, as a fraction of the base colour the
// shell supplies. 255 = the shell's full dim colour, which is DarkBlue.
extern const uint8_t kRingBaseLevel;
// Brightness of the highlighted pixel while holding: white.
extern const uint8_t kRingHoldLevel;
// Brightness of the whole ring during a commit strike: white.
extern const uint8_t kRingFlashLevel;

// What the ring is doing. Exactly one of these at a time, and it is the
// only thing that decides what the ring looks like.
enum class RingMode : uint8_t {
	// Nothing lit.
	DARK,
	// A pixel is lit and holding until idleMs expires. Set by a detent.
	IDLE,
	// A hand is resting on the proximity pad. Holds indefinitely, with
	// the idle deadline pushed out every tick -- an engaged operator
	// should not have the ring expire under them.
	PROXIMITY,
	// The interaction has ended but the ring is still lit, waiting out
	// offDelayMs before it starts to go. Reached only when a hand leaves
	// the pad.
	//
	// A distinct mode rather than a delay bolted onto the fade, because
	// "still lit and settling" and "going dark" are different things and
	// the paint differs: this one is still at full, the one after it is
	// not. The idle timeout does not come through here -- by the time it
	// fires the interaction has already been over for ringIdleMs, so
	// there is nothing left to add grace for.
	OFF_DELAY,
	// The commit strike: the whole ring at full brightness for flashMs.
	// A flash *over* whatever the ring was doing, which is why it is
	// checked before every other mode.
	FLASH,
	// Ramping to black over fadeMs. Entered from anything that ends the
	// interaction.
	FADING,
};

// The timings, copied out of the config's `led` block by the shell. The
// core takes its own struct rather than a LedFeel so it has no
// dependency on the config parser, the same split the strip's frame
// config uses.
struct RingConfig {
	// How long IDLE holds before fading. 0 disables the *timeout* --
	// the ring then stays lit until something explicitly ends the
	// interaction (a commit, or the hand leaving the pad).
	uint32_t idleMs = 5000;
	// The commit strike. 0 disables the strike, so a commit is a
	// force-off with no flash at all.
	uint32_t flashMs = 120;
	// Grace between a hand leaving the pad and the ring starting to
	// fade. Without it the ring snaps off the instant the hand is
	// withdrawn, which reads as the ring reacting to the withdrawal
	// rather than settling after the interaction. 0 goes out at once.
	//
	// The idle timeout deliberately does NOT get this grace: it is
	// already a delay, and a second one on top of it would be
	// indistinguishable from a longer ringIdleMs.
	uint32_t offDelayMs = 300;
	// How long a fade takes, in wall-clock time. Not 255 loop
	// iterations: the whole reason this moved.
	uint32_t fadeMs = 300;
	// How many pixels the ring has. The core needs it to say "the whole
	// ring is lit" without knowing anything about the hardware.
	int pixelCount = 8;
};

// What the ring should look like right now.
//
// Two brightness levels rather than colours: the shell owns the two
// colours (a dim base and a white highlight) and this scales them. That
// keeps colour choices out of the decision logic, the same way the
// strip's palette is supplied by the shell.
struct RingPaint {
	// 0..255 multiplier on the base colour, across every pixel.
	uint8_t baseLevel = 0;
	// 0..255 multiplier on the highlight colour.
	uint8_t highlightLevel = 0;
	// First highlighted pixel. Meaningless when highlightCount is 0.
	int highlightIndex = 0;
	// How many pixels from highlightIndex are highlighted. 0 = none.
	// FLASH uses the whole ring, so this is not always 1.
	int highlightCount = 0;
};

// The ring's entire state. Everything the core needs to answer "what
// now", and nothing else -- in particular no copy of the pixel buffer,
// which is what made the old fade resumable-by-accident.
struct RingState {
	RingMode mode = RingMode::DARK;
	// millis() the current mode was entered. Drives the flash deadline
	// and the fade ramp.
	uint32_t modeStartMs = 0;
	// millis() the current hold expires. Only meaningful in IDLE.
	uint32_t holdUntilMs = 0;
	// The highlighted pixel in IDLE. A free-running spinner, not a
	// console index: the ring's pixel counter and the selected console
	// are deliberately unrelated.
	int pixel = 0;
	// The paint the current fade is ramping down from, snapshotted when
	// the fade began. A strike begins from a fully lit ring and a
	// detent-held ring from a single pixel; both take fadeMs, because
	// the ramp is over this snapshot rather than over a hardcoded 255.
	//
	// This is the field that makes a stranded fade impossible. There is
	// no "how much have I faded so far" counter to lose and no pixel
	// buffer to accumulate into -- the level is recomputed from
	// modeStartMs on every tick, so an interruption costs at most the
	// few milliseconds since the last tick.
	RingPaint fadeFrom;
	// What the shell last put on the wire. An event that has to fade
	// something without knowing the current mode -- a commit with
	// flashMs = 0, say -- fades whatever is actually showing.
	RingPaint lastPaint;
	// The pad's raw reading, and whether an approach has been *accepted*.
	//
	// Two fields rather than one, and the distinction is the fix for a
	// cross-file bug the old code had: a commit cleared the ring's
	// proximity hold but not the pad's last reading, so the two
	// disagreed and the next pad edge re-armed a hold the operator had
	// already ended. Here a commit clears `proximityEngaged` only, and
	// the pad's physical state is remembered separately, so a hand that
	// never leaves cannot resurrect a hold the commit ended -- but a
	// genuine approach after they lift and return still works.
	bool proximityNear = false;
	bool proximityEngaged = false;
	// The fade reached black while the strip was still unwinding an
	// abandoned run, so the "the browse is over" signal is being held
	// back until the retreat finishes.
	//
	// This exists because the ring and the strip clean up on different
	// clocks and this flag is the link between them. The old fade took
	// as long as the main loop did -- seconds -- which meant it happened
	// to outlast the strip's retreat every time, and nothing had to say
	// so. Giving the fade its configured 300ms took that accident away,
	// and the browse was then cleared before the strip had finished
	// taking it apart: the progress indicators vanished in one step
	// instead of being peeled off one at a time. The ring now goes dark
	// on its own schedule and this carries the signal across the gap, so
	// the retreat gets its full run whichever of the two is quicker.
	bool fadeDonePending = false;
};

// The result of advancing the ring one tick.
struct RingUpdate {
	RingPaint paint;
	// True on exactly the tick the fade reaches black. The shell uses
	// it to snap the browse cursor back.
	bool fadeCompleted = false;
};

// ---- events ---------------------------------------------------------------
//
// Each of these decides the mode immediately and is meant to be called
// on the edge that caused it, not polled.

// A rotary detent, `direction` being -1 or +1. Moves the spinner pixel
// and re-arms the hold.
//
// Ignored during a FLASH: the strike owns the ring for its duration, and
// a turn underneath it should neither move the highlight nor re-arm a
// hold that is about to be ended anyway.
void ringDetent(RingState& s, uint32_t nowMs, const RingConfig& cfg,
				int direction);

// A commit. Ends the interaction: clears the proximity hold (a hand may
// still be resting on the pad -- the commit is the operator saying they
// are done) and strikes.
void ringCommit(RingState& s, uint32_t nowMs, const RingConfig& cfg);

// A proximity pad edge. `near` is the pad's current reading, not a
// change; a rising edge accepts the approach, a falling one ends it.
void ringProximity(RingState& s, uint32_t nowMs, bool near);

// ---- the tick -------------------------------------------------------------

// Advance to `nowMs` and say what the ring should look like.
//
// `retreatInProgress` is the strip still unwinding an abandoned browse
// run; it holds the ring lit for as long as it lasts, for the same
// reason the old shell did -- giving up there would clear the strip in
// one step instead of letting it finish.
RingUpdate ringTick(RingState& s, uint32_t nowMs, const RingConfig& cfg,
					bool retreatInProgress);

}  // namespace retroroom_core
