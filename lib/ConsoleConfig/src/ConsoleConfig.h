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

struct LoadResult {
	bool ok = false;
	std::string error;
	std::vector<IrCode> irCodes;
	std::vector<Console> consoles;
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