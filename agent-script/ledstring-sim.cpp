// Offline simulator for the LED-string browse and selection effects.
//
// WHAT THIS IS FOR
// ----------------
// The animations are hard to iterate on: every retune of a timing or a
// brightness in src/configuration.h otherwise needs a flash, a walk
// around the cabinet, and a squint. This replays the exact same frame
// math the firmware uses and prints each frame as an ASCII strip, so a
// change to configuration.h can be seen immediately.
//
// It is not a mock. Every frame comes from
// retroroom_core::computeStripFrame() -- the same call src/ledstring.cpp
// makes -- with the same numbers src/configuration.h gives the
// firmware. The only thing simulated is the hardware: the console list
// and the passage of time. So if a frame looks wrong here, it looks
// wrong on the strip, and if it looks right here it looks right on the
// strip.
//
// WHAT IS TRUSTED VS. REPRODUCED
// -------------------------------
// Trusted (shared with the firmware, so it cannot drift):
//   - lib/LedStringPaint in full: the detent gate, the blob travel, the
//     preview pulse, the selection settle, and the whole-frame layout.
//   - src/configuration.h: every LEDSTRING_* value below is #included
//     from it, not copied.
//
// Reproduced here (thin, and deliberately obvious if it drifts):
//   - the console list. The firmware reads /consoles.json; this uses
//     example-configurations/example2.json's geometry, which is the
//     widest layout in the repo and so the one most likely to show a
//     mistake in the "above the selection" prefix.
//   - the four-state machine from src/ledstring.cpp: which effect is
//     live, when a browse snaps, and when the selection effect hands
//     back to the resting paint.
//
// BUILD / RUN
// -----------
//   ./agent-script/ledstring-sim.sh              # all scenarios
//   ./agent-script/ledstring-sim.sh browse       # one scenario
//
// Scenarios: browse, fastspin, reverse, select, frames, all.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <LedStringPaint.h>

// The firmware's tunables. configuration.h guards its board-specific
// blocks behind ARDUINO_RASPBERRY_PI_* symbols, none of which are
// defined for a host build, so including it here yields exactly the
// LEDSTRING_* block and nothing else.
#include "configuration.h"

using retroroom_core::collectAboveWindows;
using retroroom_core::computeFillGeometry;
using retroroom_core::computeKeepEnd;
using retroroom_core::computeTravelPath;
using retroroom_core::scaleFillLead;
using retroroom_core::computeStripFrame;
using retroroom_core::resolvePixel;
using retroroom_core::LedRole;
using retroroom_core::RolePalette;
using retroroom_core::LedColor;
using retroroom_core::DetentGate;
using retroroom_core::DetentGateConfig;
using retroroom_core::LedRange;
using retroroom_core::StripEffect;
using retroroom_core::StripFrame;

namespace {

// example-configurations/example2.json. The 64-pixel strip, four
// consoles, the widest window (MAME, 15 wide) at the far end.
//
// The strip length is a property of what is physically wired, so
// NUM_SELECTED_CONSOLE_LED_STRING_LEDS lives in configuration.h's
// board-specific block and a host build defines none of those boards.
// Fall back to the Pico block's 64. This is the ONE number in the
// simulator that is not shared with the firmware; the banner says so
// out loud, and if the real strip ever changes length, change it here
// too or the sim stops being a preview of the hardware.
#ifndef NUM_SELECTED_CONSOLE_LED_STRING_LEDS
#define NUM_SELECTED_CONSOLE_LED_STRING_LEDS 64
static const bool kStripLengthFromFirmware = false;
#else
static const bool kStripLengthFromFirmware = true;
#endif

const int kTotalLeds = NUM_SELECTED_CONSOLE_LED_STRING_LEDS;

struct SimConsole {
	const char* name;
	int ledPosition;
	int ledWidth;
	int shelf;
};

// Mirrors example-configurations/example3-two-rows.json: two shelves
// with the TV between them, strung as one continuous chain, so the
// shelf boundary is a two-pixel gap in index space and a long way round
// in the cabinet. That gap is why the console schema carries a `shelf`
// field -- pixel order cannot tell you where one shelf ends.
const SimConsole kConsoles[] = {
	{"NES", 1, 1, 0},  {"SMS", 5, 3, 0},   {"XBOX", 12, 3, 0}, {"MAME", 19, 6, 0},
	{"GEN", 26, 3, 1}, {"PS1", 34, 3, 1}, {"PS2", 41, 3, 1},  {"DC", 49, 5, 1},
};
const int kConsoleCount = static_cast<int>(sizeof(kConsoles) / sizeof(kConsoles[0]));

LedRange windowFor(int idx) {
	if (idx < 0 || idx >= kConsoleCount) {
		return {0, 0};
	}
	return retroroom_core::computeConsoleWindow(kConsoles[idx].ledPosition,
											   kConsoles[idx].ledWidth, kTotalLeds);
}

int keepEndFor(int idx) {
	if (idx < 0 || idx >= kConsoleCount) {
		return 0;
	}
	return computeKeepEnd(kConsoles[idx].ledPosition, kConsoles[idx].ledWidth,
						  kTotalLeds, true);
}

// Upper bound on the "windows above" list, and the scratch it is built
// into. Mirrors kMaxAboveWindows in src/ledstring.cpp; the firmware
// sizes its own off the strip length, which a host build cannot see.
const int kMaxAboveWindows = 8;
LedRange aboveBuffer[kMaxAboveWindows];

// The windows of every console entirely above `idx`, for the resting
// paint. Mirrors collectAboveFor() in src/ledstring.cpp -- same core
// call, same list, so the sim and the firmware agree.
int collectAboveFor(int idx, LedRange* out, int capacity) {
	LedRange all[kConsoleCount];
	int count = 0;
	for (int i = 0; i < kConsoleCount; ++i) {
		if (i != idx) {
			all[count++] = windowFor(i);
		}
	}
	return collectAboveWindows(all, count, windowFor(idx).start, out, capacity);
}

// Fill a frame's resting fields from the console list. Shared by every
// scenario that shows a resting or selection frame, so the sim cannot
// drift from the shell's own assembly.
// Mirrors travelEntryFor() in src/ledstring.cpp: the trailing edge of
// the console being left within a shelf, or the far end of the
// destination shelf when the step crosses between them.
int travelEntryFor(int from, int to) {
	const LedRange leave = windowFor(from);
	const int leaveEnd = leave.start + leave.width;
	if (kConsoles[from].shelf == kConsoles[to].shelf) {
		return leaveEnd;
	}
	int far = windowFor(to).start + windowFor(to).width;
	for (int i = 0; i < kConsoleCount; ++i) {
		if (kConsoles[i].shelf != kConsoles[to].shelf) {
			continue;
		}
		const LedRange w = windowFor(i);
		if (w.start + w.width > far) {
			far = w.start + w.width;
		}
	}
	return far;
}

// Mirrors applyBrowsePath() in src/ledstring.cpp.
void applyBrowsePath(StripFrame& f, int from, int to) {
	computeTravelPath(windowFor(from), travelEntryFor(from, to), windowFor(to),
					  LEDSTRING_TRAVEL_SPARK_LEDS, f);
}

void applyResting(StripFrame& f, int idx) {
	f.from = windowFor(idx);
	f.aboveCount = collectAboveFor(idx, aboveBuffer, kMaxAboveWindows);
	f.aboveWindows = aboveBuffer;
}

int wrapNext(int current, int direction) {
	int next = current + direction;
	while (next < 0) {
		next += kConsoleCount;
	}
	while (next >= kConsoleCount) {
		next -= kConsoleCount;
	}
	return next;
}

// Mirrors buildPalette() in src/ledstring.cpp.
RolePalette buildPalette() {
	RolePalette p;
	p.colors[static_cast<int>(LedRole::OFF)] = {0, 0, 0};
	p.colors[static_cast<int>(LedRole::STACK)] = {LEDSTRING_COLOR_STACK_R,
		LEDSTRING_COLOR_STACK_G, LEDSTRING_COLOR_STACK_B};
	p.colors[static_cast<int>(LedRole::LEAVING)] = {LEDSTRING_COLOR_LEAVING_R,
		LEDSTRING_COLOR_LEAVING_G, LEDSTRING_COLOR_LEAVING_B};
	p.colors[static_cast<int>(LedRole::FILL)] = {LEDSTRING_COLOR_FILL_R,
		LEDSTRING_COLOR_FILL_G, LEDSTRING_COLOR_FILL_B};
	p.colors[static_cast<int>(LedRole::TRAVEL)] = {LEDSTRING_COLOR_TRAVEL_R,
		LEDSTRING_COLOR_TRAVEL_G, LEDSTRING_COLOR_TRAVEL_B};
	p.colors[static_cast<int>(LedRole::PROPOSAL)] = {LEDSTRING_COLOR_PROPOSAL_R,
		LEDSTRING_COLOR_PROPOSAL_G, LEDSTRING_COLOR_PROPOSAL_B};
	p.colors[static_cast<int>(LedRole::SELECTED)] = {LEDSTRING_COLOR_SELECTED_R,
		LEDSTRING_COLOR_SELECTED_G, LEDSTRING_COLOR_SELECTED_B};
	return p;
}

// A hue glyph per role, so the two families are separable at a glance
// in a terminal: lowercase for context, uppercase for the thing being
// offered. `.` is a pixel no console owns.
static char roleGlyph(LedRole r) {
	switch (r) {
	case LedRole::STACK: return 's';
	case LedRole::LEAVING: return 'l';
	case LedRole::FILL: return 'f';
	case LedRole::TRAVEL: return 'T';
	case LedRole::PROPOSAL: return 'P';
	case LedRole::SELECTED: return 'S';
	default: return '.';
	}
}

// Mirrors the assembly in src/ledstring.cpp::baseFrame().
StripFrame baseFrame() {
	StripFrame f;
	f.totalLeds = kTotalLeds;
	f.abovePct = LEDSTRING_ABOVE_PCT;
	f.selfPct = LEDSTRING_SELF_PCT;
	f.fromPct = LEDSTRING_BROWSE_FROM_PCT;
	f.toPct = LEDSTRING_BROWSE_TO_PCT;
	f.blobPct = LEDSTRING_BLOB_PCT;
	f.blobWidth = LEDSTRING_BLOB_WIDTH;
	f.fillPct = LEDSTRING_FILL_PCT;
	f.dimPct = LEDSTRING_ABOVE_PCT;
	f.travelPct = LEDSTRING_SELF_PCT;
	f.minFillLeds = LEDSTRING_FILL_MIN_LEDS;
	f.travelMs = LEDSTRING_TRAVEL_MS;
	f.travelPeakWidth = LEDSTRING_TRAVEL_PEAK_WIDTH;
	f.palette = buildPalette();
	f.pulseMinPct = LEDSTRING_PREVIEW_PULSE_MIN_PCT;
	f.pulseMaxPct = LEDSTRING_PREVIEW_PULSE_MAX_PCT;
	f.pulsePeriodMs = LEDSTRING_PREVIEW_PULSE_MS;
	f.select.totalMs = LEDSTRING_SELECT_EFFECT_MS;
	f.select.twinkleMs =
		(LEDSTRING_SELECT_TWINKLE_MS < LEDSTRING_SELECT_EFFECT_MS)
			? LEDSTRING_SELECT_TWINKLE_MS
			: LEDSTRING_SELECT_EFFECT_MS;
	f.select.staggerMs = LEDSTRING_SELECT_STAGGER_MS;
	f.select.twinkleMin = LEDSTRING_SELECT_TWINKLE_MIN_PCT;
	f.select.twinkleMax = LEDSTRING_SELECT_TWINKLE_MAX_PCT;
	f.select.abovePct = LEDSTRING_ABOVE_PCT;
	f.select.selfPct = LEDSTRING_SELF_PCT;
	return f;
}

// One glyph per pixel. The scale deliberately uses log-ish buckets so
// the difference between "dim" and "very dim" is visible in a
// terminal, which is the whole point -- these percentages are far too
// close together to read numerically.
// Ten glyphs for eleven buckets (0 = dark, then 1..100 in ten steps).
// Dark is '.' rather than ' ' so the whole strip is visible on one
// line -- a space in the middle of a run of them is invisible, which
// hid a correctly-rendering fill until this was noticed.
//
// Exactly ten characters: the bucket index goes to 9, and a nine-
// character ramp silently read past its own end, rendering full
// brightness as garbage.
const char kRamp[] = ".:-=+o*#%@";

void render(const StripFrame& frame, const char* caption) {
	retroroom_core::StripPixel px[kTotalLeds] = {};
	computeStripFrame(frame, px);

	printf("  %-34s |", caption);
	for (int i = 0; i < kTotalLeds; ++i) {
		int p = px[i].level;
		if (p < 0) {
			p = 0;
		}
		if (p > 100) {
			p = 100;
		}
		// Buckets: 0, then ten steps of 10 across 1..100.
		int bucket = (p == 0) ? 0 : ((p - 1) * 9) / 100 + 1;
		putchar(kRamp[bucket]);
	}
	printf("|\n");
}
// What each pixel is *for*, as a role glyph. Brightness alone cannot
// separate two roles at the same level, which is the whole reason the
// colours exist -- so the simulator has to be able to show them.
void renderRoles(const StripFrame& frame, const char* caption) {
	retroroom_core::StripPixel px[kTotalLeds] = {};
	computeStripFrame(frame, px);
	printf("  %-34s |", caption);
	for (int i = 0; i < kTotalLeds; ++i) {
		putchar(roleGlyph(px[i].role));
	}
	printf("|\n");
}

void rule(const char* title) {
	printf("\n%s\n", title);
	for (int i = 0; i < 80; ++i) {
		putchar('-');
	}
	putchar('\n');
}

void banner() {
	printf("LED string simulator -- example3-two-rows.json geometry, %d pixels, %d consoles\n",
		   kTotalLeds, kConsoleCount);
	printf("roles: S=selected P=proposal T=travel f=fill l=leaving s=stack .=off\n");
	if (!kStripLengthFromFirmware) {
		printf("  (strip length is a host fallback: it is a board/wiring value,\n"
			   "   not a tuning knob. It matches src/configuration.h today.)\n");
	}
	printf("%d detents/step, %d when fast (within %dms), blob %dpx, "
		   "pulse %dms, select %dms\n\n",
		   LEDSTRING_DETENTS_PER_STEP, LEDSTRING_FAST_DETENTS_PER_STEP,
		   LEDSTRING_FAST_SPIN_WINDOW_MS, LEDSTRING_BLOB_WIDTH,
		   LEDSTRING_PREVIEW_PULSE_MS, LEDSTRING_SELECT_EFFECT_MS);
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

// One deliberate turn of the knob, detent by detent: the knob-turn
// progression fill walking across the gap, then the scripted travel.
// MAME is index 3, SMS is index 1, so this is a step *backwards* within
// the top shelf.
void scenarioBrowse() {
	rule("BROWSE -- four detents of fill, then the scripted travel");
	banner();

	StripFrame rest = baseFrame();
	rest.effect = StripEffect::RESTING;
	applyResting(rest, 1);
	render(rest, "resting (SMS selected)");

	DetentGate gate;
	gate.configure(DetentGateConfig(LEDSTRING_DETENTS_PER_STEP,
									LEDSTRING_FAST_DETENTS_PER_STEP,
									LEDSTRING_FAST_SPIN_WINDOW_MS));
	const int anchor = 1;
	const int target = 0;
	uint32_t t = 0;

	for (int i = 0; i < LEDSTRING_DETENTS_PER_STEP - 1; ++i) {
		t += 2000;
		const retroroom_core::DetentEvent ev = gate.onDetent(-1, t,
															kConsoleCount, anchor);
		StripFrame f = baseFrame();
		f.effect = StripEffect::FILLING;
		f.from = windowFor(anchor);
		applyBrowsePath(f, anchor, target);
		f.fillLead = scaleFillLead(f.fillAnchor, f.fillLead, f.fillForward,
								   ev.stepPermille);
		char caption[64];
		snprintf(caption, sizeof(caption), "detent %d/%d fill",
				 ev.detents, ev.detentsPerStep);
		render(f, caption);
		char roles[64];
		snprintf(roles, sizeof(roles), "detent %d/%d roles",
				 ev.detents, ev.detentsPerStep);
		renderRoles(f, roles);
	}

	printf("\n  -- travel --\n");
	for (int step = 0; step <= 8; ++step) {
		StripFrame f = baseFrame();
		f.effect = StripEffect::TRAVEL;
		applyBrowsePath(f, anchor, target);
		f.elapsedMs = static_cast<std::uint32_t>(LEDSTRING_TRAVEL_MS * step / 8);
		char caption[64];
		snprintf(caption, sizeof(caption), "travel t=%3ums",
				 f.elapsedMs);
		render(f, caption);
	}
	StripFrame pv = baseFrame();
	pv.effect = StripEffect::PREVIEW;
	pv.from = windowFor(target);
	pv.to = pv.from;
	pv.aboveCount = collectAboveFor(target, aboveBuffer, kMaxAboveWindows);
	pv.aboveWindows = aboveBuffer;
	pv.abovePct = pv.dimPct;
	render(pv, "preview pulses on NES");
}



// The commit: whole strip twinkles, then settles on the pixels above
// the selection. Sampled densely enough to see both phases.
void scenarioSelect(int consoleIdx) {
	rule("SELECT -- commit and settle");
	printf("selecting %s (led %d..%d, keepEnd %d)\n\n", kConsoles[consoleIdx].name,
		   kConsoles[consoleIdx].ledPosition,
		   kConsoles[consoleIdx].ledPosition + kConsoles[consoleIdx].ledWidth - 1,
		   keepEndFor(consoleIdx));

	StripFrame f = baseFrame();
	f.effect = StripEffect::SELECTING;
	applyResting(f, consoleIdx);

	// Sample in a few passes: coarse through the twinkle, finer through
	// the settle, where the interesting ramp is.
	for (int pass = 0; pass < 2; ++pass) {
		const int steps = (pass == 0) ? 6 : 8;
		for (int i = 0; i <= steps; ++i) {
			f.elapsedMs = static_cast<std::uint32_t>(
				(pass == 0)
					? (static_cast<long>(LEDSTRING_SELECT_EFFECT_MS) * i) / (steps * 3)
					: LEDSTRING_SELECT_EFFECT_MS / 3 +
						  static_cast<long>(LEDSTRING_SELECT_EFFECT_MS - LEDSTRING_SELECT_EFFECT_MS / 3) * i / steps);
			char caption[64];
			snprintf(caption, sizeof(caption), "%s t=%4ums",
					 (pass == 0) ? "twinkle" : "settle ", f.elapsedMs);
			render(f, caption);
		}
	}

	StripFrame rest = baseFrame();
	rest.effect = StripEffect::RESTING;
	applyResting(rest, consoleIdx);
	render(rest, "resting");
}

// The resting stack for every console, so the "above the selection"
// prefix can be eyeballed against the whole list at once.
void scenarioFrames() {
	rule("RESTING STACK -- every console in turn");
	for (int i = 0; i < kConsoleCount; ++i) {
		StripFrame f = baseFrame();
		f.effect = StripEffect::RESTING;
		applyResting(f, i);
		char caption[64];
		snprintf(caption, sizeof(caption), "%s (%d above)", kConsoles[i].name,
				 collectAboveFor(i, aboveBuffer, kMaxAboveWindows));
		render(f, caption);
	}
}

// The three readings of "reduce down to only the ones above the selected
// console", for one console. The gaps between segments are the point:
// example2.json puts NES at [1,2), SMS at [7,12), XBOX at [17,22) and
// MAME at [27,42), so [2,7), [12,17) and [22,27) belong to *no*
// console. Whether those stay dark is the whole difference between the
// three, and it is an aesthetic call that is far easier to settle by
// looking than by arguing.
void scenarioAbove() {
	rule("\"ABOVE THE SELECTION\" -- three readings, SMS selected");
	printf("SMS owns leds 7..11, NES owns led 1. The gaps belong to no "
		   "console.\n\n");

	// (a) the consoles above, in their own windows, gaps dark. This is
	// what is on the strip now, and it goes through the real code path.
	{
		StripFrame f = baseFrame();
		f.effect = StripEffect::RESTING;
		applyResting(f, 1);
		render(f, "(a) windows above, gaps dark  [current]");
	}

	// (b) a contiguous prefix from led 0, filling the gaps. This is what
	// used to be on the strip, and is what "starts at led 0" describes.
	printf("%-36s |", "(b) prefix 0..11, gaps filled  [was]");
	for (int p = 0; p < kTotalLeds; ++p) {
		int pct = 0;
		if (p < keepEndFor(1)) {
			pct = LEDSTRING_ABOVE_PCT;
		}
		if (p >= 7 && p < 12) {
			pct = LEDSTRING_SELF_PCT;
		}
		int bucket = (pct == 0) ? 0 : ((pct - 1) * 9) / 100 + 1;
		putchar(kRamp[bucket]);
	}
	printf("|\n");

	// (c) the selection alone -- LEDSTRING_ABOVE_PCT = 0.
	{
		StripFrame f = baseFrame();
		f.effect = StripEffect::RESTING;
		f.abovePct = 0;
		applyResting(f, 1);
		render(f, "(c) selection window only");
	}
}


// The shelf crossing. MAME is the last console on shelf 0 and GEN the
// first on shelf 1; the two are two pixels apart in index space and a
// long way round in the cabinet. The block enters at the far end of the
// destination shelf and sweeps back to GEN, so the light travels the
// width of the cabinet even though the knob went forward one console.
void scenarioShelf() {
	rule("SHELF CROSSING -- MAME (shelf 0) -> GEN (shelf 1)");
	printf("Knob goes RIGHT one console. The light goes LEFT, across the\n"
		   "whole destination shelf, and lands on GEN.\n\n");

	const int from = 3;  // MAME
	const int to = 4;    // GEN

	StripFrame rest = baseFrame();
	rest.effect = StripEffect::RESTING;
	applyResting(rest, from);
	render(rest, "resting (MAME selected)");

	StripFrame f = baseFrame();
	f.effect = StripEffect::FILLING;
	f.from = windowFor(from);
	applyBrowsePath(f, from, to);
	f.minFillLeds = 0;
	render(f, "fill spans the destination shelf");

	for (int step = 0; step <= 10; ++step) {
		StripFrame t = baseFrame();
		t.effect = StripEffect::TRAVEL;
		applyBrowsePath(t, from, to);
		t.elapsedMs = static_cast<std::uint32_t>(LEDSTRING_TRAVEL_MS * step / 10);
		char caption[64];
		snprintf(caption, sizeof(caption), "travel t=%3ums", t.elapsedMs);
		render(t, caption);
		char roles[64];
		snprintf(roles, sizeof(roles), "travel t=%3ums roles", t.elapsedMs);
		renderRoles(t, roles);
	}
}

}  // namespace

int main(int argc, char** argv) {
	const std::string which = (argc > 1) ? argv[1] : "all";
	const bool all = (which == "all");
	if (all || which == "browse") {
		scenarioBrowse();
	}
	if (all || which == "shelf") {
		scenarioShelf();
	}
	if (all || which == "select") {
		scenarioSelect(4);
	}
	if (all || which == "frames") {
		scenarioFrames();
	}
	if (all || which == "above") {
		scenarioAbove();
	}
	if (!all && which != "browse" && which != "shelf" && which != "select" &&
		which != "frames" && which != "above") {
		fprintf(stderr, "unknown scenario '%s'\n", which.c_str());
		fprintf(stderr, "try: browse, shelf, select, frames, above, all\n");
		return 1;
	}
	printf("\n");
	return 0;
}
