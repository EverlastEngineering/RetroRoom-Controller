#pragma once

// Functional core for RetroRoom console configuration.
// Pure data + parsing + selection math. No Arduino headers, no I/O, no globals.
// Compiles and unit-tests on the host via PlatformIO + Unity.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace retroroom_core {

struct IrCode {
	std::string name;
	std::uint16_t code;
};

struct Console {
	// Display name (e.g. "Nintendo Entertainment System"). May come from
	// consoleNames[id] or fall back to id.
	std::string name;
	// Short id (e.g. "NES").
	std::string id;
	// Resolved IR hex code from the irCodes map.
	std::uint16_t tvinput;
	int selector_position;
	int led_position;
	int led_width;
	// Which physical shelf this console sits on. Optional in the JSON
	// and defaulted to 0 by the parser, so a config that predates the
	// field loads as a single shelf.
	//
	// Not derivable from the LED layout: the shelves are strung as one
	// continuous chain, so pixel order says nothing about where one
	// shelf ends. The browse animation needs it to know that stepping
	// between shelves is a different kind of move -- a step within a
	// shelf crosses a gap, a step between shelves travels the width of a
	// shelf.
	//
	// No initialiser, like the fields above it: under C++11 a default
	// member initialiser would stop Console being an aggregate and break
	// every brace-initialised Console in the tests. The parser always
	// sets it, which is the only place a Console is built from JSON.
	int shelf;
	// Per-console tagline (e.g. "Now you're playing with power!"). Optional
	// in the JSON; default empty string. Consumed by the LCD driver to
	// render line 2 on the 16x2 character display.
	std::string tagline;
};

struct Shelf {
	// Matches the `shelf` value on the consoles that sit here. The join
	// key: a console names a shelf, a shelf names the LEDs it occupies.
	int id = 0;
	// Inclusive LED indices of the shelf's physical extent. The shelf
	// runs wider than the consoles on it -- there is bare string at
	// either end -- and that bare space is where a console's commit
	// animation is allowed to expand into.
	//
	// Inclusive rather than half-open because these are positions on a
	// physical object ("LED 0 through LED 24 are on the top shelf"), not
	// a slice of a buffer.
	int fromLed = 0;
	int toLed = 0;
};

// The roles the LED string can show a pixel as, in the order the
// colours are written in the JSON. Mirrors retroroom_core::LedRole --
// kept as its own enum because lib/ConsoleConfig must not depend on the
// paint library, and the test that keeps the two in step compares them.
enum LedRoleId {
	kRoleStack = 0,
	kRoleLeaving,
	kRoleFill,
	kRoleTravel,
	kRoleProposal,
	kRoleSelected,
	kRoleCount
};

// Everything about how the LED string *feels*, in one struct.
//
// The rule this type exists to enforce: **one number, one home.** Each
// value has exactly one default and one range, both declared here
// beside the comment explaining what the number trades off. The JSON is
// parsed *into* this struct, so a field that is missing is a field that
// takes the default below, and nothing anywhere else states what the
// default is.
//
// That is the whole reason this is a struct and not a block of numbers
// in the JSON file with defaults repeated in C. A flat mirror of the old
// #defines would put every default in two places at once, and the
// failure mode is not subtle: the file says one thing, the code says
// another, and which one the operator sees depends on which path loaded
// the config.
struct LedFeel {
	// ---- the strip itself -------------------------------------------------
	// How many LEDs the string has. The firmware is built for
	// LED_STRING_CAPACITY; this is how many of them are actually fitted.
	//
	// Absent means LED_STRING_CAPACITY -- treat the string as filling the
	// buffer -- which is the old behaviour, so a config that predates the
	// field lays out exactly as it did.
	//
	// ERR LOW. A window that runs past the end is clamped, and the console
	// silently lands in the wrong place. A count larger than the string
	// only means the spare LEDs never light, which is a thing you can
	// see. So when in doubt, under-count.
	int totalLeds;

	// ---- the travel: the block moving between consoles ---------------------
	int travelMs;          // how long the move takes
	int travelPeakWidth;   // cap on the block's width; floored at the target
	int travelSparkLeds;   // width where the block leaves the console

	// ---- brightness, as a percentage of the role's colour -----------------
	int abovePct;   // the resting stack. 0: only the selection is lit
	int selfPct;    // the selected console's own window
	int dimPct;     // context during a browse
	int fillPct;    // the knob-turn progression run
	int blobPct;    // the browse blob
	int browseFromPct;  // the window being left
	int browseToPct;    // the window being approached

	// ---- the knob's feel ---------------------------------------------------
	int detentsPerStep;       // detents for a deliberate console step
	int fastDetentsPerStep;   // ...once spinning. Clamped to the above
	int fastSpinWindowMs;     // gap that marks a spin. 0 disables it
	int settleLockoutMs;      // detents ignored after a commit. 0 disables

	// ---- the progression run ----------------------------------------------
	int fillMinLeds;        // floor on the run, in LEDs
	int fillRetreatDelayMs; // quiet before an abandoned run gives itself back
	int fillRetreatStepMs;  // per LED on the way back. This is also the fade
	int blobWidth;          // blob width in LEDs

	// ---- the preview pulse -------------------------------------------------
	int pulseMs;       // one cycle
	int pulseMinPct;   // dimmest
	int pulseMaxPct;   // brightest

	// ---- the commit --------------------------------------------------------
	int explodeMs;  // the window dissolving outward
	int igniteMs;   // the window coming back
	// The total is the sum of the two, computed in loadLedFeel(). It is
	// not a field: a third number that can disagree with the two it
	// summarises is how the loop and the config drift apart.

	// ---- the ring ----------------------------------------------------------
	int ringIdleMs;   // how long the ring stays lit before giving up
	int ringFlashMs;  // the strike on a commit. 0 disables

	// ---- the strip as a whole ----------------------------------------------
	int frameIntervalMs;  // how often an in-flight frame is pushed

	// One RGB triple per role, in LedRoleId order. Written in the JSON as
	// {"selected": [r, g, b], ...} rather than as twenty-one keys.
	int colorR[kRoleCount];
	int colorG[kRoleCount];
	int colorB[kRoleCount];
};

// The most LEDs the firmware is built to drive. A hardware fact, the
// same category as the data pin, and it lives here rather than in
// src/configuration.h because this is the code that has to *reject* a
// config claiming more -- one number, in the file that enforces it.
//
// Sized generously: the buffer is allocated at this size whatever the
// config says, and the cost is 3 bytes per LED. pin-map-chart.md records
// the length the cabinet actually has.
constexpr int kLedStripCapacity = 512;

// Where the defaults live, for anything that needs them without parsing
// a document: the simulator, and the host tests. Returns the same
// values a config with no `led` block would produce, so there is one
// answer to "what is the default" in the whole codebase.
LedFeel defaultLedFeel();

struct LoadResult {
	bool ok = false;
	std::string error;
	std::vector<IrCode> irCodes;
	std::vector<Console> consoles;
	// Optional top-level `shelves` block: the physical extent of each
	// shelf, keyed by the `shelf` value its consoles carry. Empty when
	// the config does not declare one, and the shell then derives the
	// extents from the consoles' own windows -- which is a shelf that
	// is exactly as wide as the consoles on it, and loses the bare
	// string at either end.
	std::vector<Shelf> shelves;
	// The strip length and how it feels. Populated whether or not the
	// document has a `led` block: a config that says nothing gets the
	// defaults, which is the whole point of loadLedFeel().
	LedFeel feel = defaultLedFeel();
	// Everything the parser had to pull back into range, one line per
	// clamp, for the shell to print at boot. Empty when the config was
	// entirely legal.
	//
	// Accept-and-clamp rather than reject is deliberate: a hand-edited
	// config should not stop a cabinet working, and the alternative to
	// clamping is booting values nobody asked for. A clamp that is
	// invisible is no better than the bug it replaced, hence the list.
	std::vector<std::string> warnings;
	// Top-level LCD config. Default 30000 ms (30 s) when the `lcd` block
	// is absent or when the field is missing. The shell exposes this to
	// the LCD driver via retroroom_store::getLcdBacklightOffAfterMs().
	// Bounds-checked on parse: out-of-range values are accepted but logged
	// (validation shouldn't be a hard error -- operators with weird
	// configs shouldn't have their devices bricked).
	std::uint32_t lcdBacklightOffAfterMs = 30000;
};

LoadResult loadFromJson(const char* json, std::size_t len);
LoadResult loadFromJson(const char* json);

// Selection cursor over a console list. Pure: holds a reference and an index.
class Selection {
  public:
	explicit Selection(const std::vector<Console>& consoles);

	const std::vector<Console>& consoles() const { return consoles_; }
	std::size_t index() const { return index_; }
	std::size_t size() const { return consoles_.size(); }
	bool empty() const { return consoles_.empty(); }

	const Console& current() const;

	// direction: +1 for forward, -1 for backward, 0 is a no-op.
	// wraparound: when true, overflow at either end wraps; when false, returns false.
	// Returns true iff the index actually changed.
	bool rotate(int direction, bool wraparound = false);

  private:
	const std::vector<Console>& consoles_;
	std::size_t index_ = 0;
};

// Pure wraparound math: given a current index, count of items, and a rotation
// direction (+1 forward / -1 backward / 0 no-op), return the next index with
// wraparound at both ends. n == 0 returns 0 (the only sane fallback).
// Tested as a free function so the shell can stay free of the boundary logic
// even if the Selection class isn't used at the call site.
int wraparoundNext(int current, int n, int direction);

// Clamped step: one console in `direction`, or `current` unchanged when
// that would leave [0, n). The policy the cabinet actually wants -- the
// list has physical ends, so turning past one reaches nothing.
//
// The counterpart to wraparoundNext(), and the reason both exist is
// that "what happens at the end" is a product decision, not a detail:
// wrapping is right for a ring of items, stopping is right for a row of
// shelves. Clamps rather than throwing, so a stale index from a
// hand-edited config degrades to "no move" instead of UB.
int stepWithin(int current, int n, int direction);

// "Would stepWithin() actually move?" For callers that need to freeze an
// indication when there is nowhere to go, and must not perform the step
// to find out.
bool canStepWithin(int current, int n, int direction);

// Clamp a persisted index into [0, n). Out-of-bounds (negative or >=
// n) collapses to the nearest in-bounds value; n == 0 returns 0. Used by
// the shell when restoring a saved index across reboots and after the
// console list has changed.
int clampIndex(int requested, int n);

// Validate a console-config JSON string. Thin wrapper over loadFromJson()
// that exists so the network shell has a clearly-named "is this body
// safe to write to LittleFS?" gate; today it just forwards to the parser,
// but if we ever add cross-field invariants (e.g. unique console ids,
// non-overlapping LED ranges) they land here without touching the shell.
// On success: returns true and fills `out` with the parsed consoles/irCodes.
// On failure: returns false and `out.error` carries the reason.
bool validateConsoleConfigJson(const std::string& json, LoadResult& out);

// Pure backup-rotation policy for the LittleFS store. The shell keeps
// three blob slots -- the live config + two rolling backups. Before
// committing a new live config, the shell calls this function to compute
// the new slot values:
//
//     out_live   = newPayload
//     out_backup1 = current_live      (whatever was live becomes the
//                                       most recent backup)
//     out_backup2 = current_backup1   (the older backup is dropped)
//
// `current_live` and `current_backup1` may be empty strings to indicate
// the slot is empty (file missing / new device / wiped FS); an empty
// slot simply propagates downstream. The function never inspects the
// JSON -- it's a pure byte-level rotation so the shell can unit-test
// the policy against an in-memory mock FS.
struct BackupRotation {
	std::string live;
	std::string backup1;
	std::string backup2;
};
BackupRotation rotateBackupBlobs(const std::string& current_live,
                                 const std::string& current_backup1,
                                 const std::string& new_payload);

}  // namespace retroroom_core