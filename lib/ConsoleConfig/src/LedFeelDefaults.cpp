// The default values of every field in LedFeel, and nothing else.
//
// Split out from the parser on purpose: this is pure data with no
// dependencies at all, not even ArduinoJson, so anything that needs to
// know what "the default" is -- the simulator, the host tests -- can
// have it by compiling one file. The parser needs the JSON library; the
// defaults do not, and making the simulator link a parse library to
// learn thirty numbers would be a poor trade.
//
// The *ranges* live in ConsoleConfig.cpp with the parser, because a
// range is only meaningful next to the parse that enforces it.

#include "ConsoleConfig.h"

namespace retroroom_core {

LedFeel defaultLedFeel() {
	LedFeel f;
	f.totalLeds = kLedStripCapacity;

	// The travel. The block's peak is a *cap* on how wide it gets, not
	// a target: the block always ends exactly the width of the console
	// it lands on, because travelEdges() floors the cap at the target's
	// width. A value below the widest window in the cabinet is therefore
	// simply ignored -- which is the safe direction, and worth knowing
	// before anyone lowers it hoping for a slimmer block.
	f.travelMs = 420;
	f.travelPeakWidth = 6;
	f.travelSparkLeds = 2;  // 1 reads as a stray pixel, 2 as an object

	// Brightness, as a percentage of the role's own colour. ABOVE is the
	// resting stack and defaults to 0: a cumulative reading was reported
	// from the bench as "the whole string is lit" rather than as a stack.
	// The dim/fill split is deliberate -- sharing a level between "where
	// the stack ends" and "how far I have got" leaves nothing to read
	// progress from.
	f.abovePct = 0;
	f.selfPct = 100;
	f.dimPct = 22;
	f.fillPct = 45;
	f.blobPct = 100;
	f.browseFromPct = 25;
	f.browseToPct = 45;

	// The knob. Five detents was chosen by feel on the bench: fewer and
	// a step happens by accident, more and the knob stops feeling like it
	// is choosing anything. The fast path is two.
	//
	// fastSpinWindowMs is 0, which DISABLES the escalation, and the
	// reason is worth keeping: it was 1000ms, which sounds generous but
	// is *shorter than a deliberate human detent*. The first detent
	// registered as deliberate, the second tripped the window, and from
	// there on the browse was permanently in fast mode. It has to be
	// comfortably LONGER than the operator's slowest deliberate turn.
	// We do not have a number for that yet, which is why it is off
	// rather than merely retuned; a starting guess would be 2000-3000ms.
	// The latching that compounded this is fixed independently -- the
	// escalation now reflects the gap before each detent, so it drops
	// back the moment they slow down.
	f.detentsPerStep = 5;
	f.fastDetentsPerStep = 2;
	f.fastSpinWindowMs = 0;

	// 250ms after a commit, detents are ignored so an overshoot costs one
	// step rather than two. Deliberately shorter than the travel: once
	// the lockout expires a turn cuts the animation short, which is what
	// a mid-travel detent already did, and covering the whole travel
	// would swallow real input for twice as long.
	f.settleLockoutMs = 250;

	// The progression run. The floor exists because a step *between
	// shelves* is a couple of pixels in index space and a long way round
	// physically, so filling the literal gap would leave the indicator
	// barely moving on exactly the steps hardest to read.
	//
	// The retreat gives an abandoned run back rather than leaving it
	// pointing at a console nobody asked for, one LED at a time from the
	// leading edge. 250ms per LED is also that LED's fade, so the run
	// reels in rather than strobing; 0 for the delay disables it.
	f.fillMinLeds = 3;
	f.fillRetreatDelayMs = 3000;
	f.fillRetreatStepMs = 250;
	f.blobWidth = 3;

	// The preview pulse. 1100ms reads as a slow breath rather than a
	// heartbeat. The dim end wants to stay clearly non-zero or the pulse
	// strobes.
	f.pulseMs = 1100;
	f.pulseMinPct = 30;
	f.pulseMaxPct = 100;

	// The commit, in two halves: the window dissolving outward, then
	// coming back. 400 to dissolve and 200 to rebuild -- the explosion
	// is the one that has to be read as a movement, and the ignite is the
	// one that only has to arrive. They meet at zero brightness, which
	// is the gap between the old console going and the new one arriving.
	f.explodeMs = 400;
	f.igniteMs = 200;

	// The ring. The idle timeout is also what reverts an abandoned
	// browse, and it is *held* while the progression run is still
	// unwinding so it never cuts a retreat short.
	//
	// ringOffDelayMs is separate from ringIdleMs because the two answer
	// different questions. ringIdleMs is "how long after the last turn
	// does the ring give up", and it is also the only delay in the idle
	// path -- by the time it fires the interaction has been over for
	// five seconds. The hand leaving the pad is different: the operator
	// has just *decided* the interaction is over, so a fade starting on
	// that instant reads as the ring reacting to the withdrawal rather
	// than settling. A short grace first, then the fade.
	//
	// ringFadeMs is wall clock, and it is here rather than derived from
	// the main loop because that derivation was the bug: the fade used
	// to be one decrement per loop() iteration, so it took as long as
	// the loop happened to. 300ms is about the floor where a fade still
	// reads as a fade rather than a switch.
	f.ringIdleMs = 5000;
	f.ringFlashMs = 120;  // the strike on a commit. 0 disables
	f.ringOffDelayMs = 300;
	f.ringFadeMs = 300;

	// 100 = the cabinet as it has always looked. The scale it multiplies
	// is the one that was previously a #define in src/lighting.cpp, so
	// the default is not a new number but the one that was already
	// there, renamed. A default lower than that would quietly dim every
	// cabinet on the next boot after an upgrade.
	f.brightnessPct = 100;

	// How often an in-flight frame goes to the wire. A sampling rate, not
	// a step count: every frame is computed from elapsed time, so raising
	// this plays the same animation more smoothly. The floor is how long
	// FastLED.show() takes to clock the strip out plus whatever the rest
	// of loop() needs -- the driver measures that and prints it, so set
	// this from the measurement rather than by guessing.
	f.frameIntervalMs = 8;

	// Colours. Amber for what the operator is being offered, cool blue
	// for the context it is contrasted against: that is the one
	// distinction worth having by eye alone. Values are deliberately
	// conservative -- this strip sits next to a television in a dark
	// room, and a misconfigured colour here is a glare problem.
	//
	// TRAVEL must equal PROPOSAL. The travel's last frame *is* the target
	// window and the frame after it is that window pulsing as a
	// proposal, so any difference is a flash of a different hue at exactly
	// the moment the movement resolves into an answer.
	const int defaults[kRoleCount][3] = {
		{12, 28, 40},  // stack: context, and dark so it never competes
		{20, 34, 48},  // leaving: the console being turned away from
		{16, 40, 56},  // fill: the progression run, cool so it reads as
		{64, 40, 8},   //       progress and not as a console that is on
		{64, 40, 8},   // travel: the proposal in flight -- see above
		{48, 36, 24},  // selected: the resting selection, warmer and calmer
	};
	for (int r = 0; r < kRoleCount; ++r) {
		f.colorR[r] = defaults[r][0];
		f.colorG[r] = defaults[r][1];
		f.colorB[r] = defaults[r][2];
	}
	return f;
}

}  // namespace retroroom_core
