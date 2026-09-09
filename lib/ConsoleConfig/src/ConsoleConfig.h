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
};

struct LoadResult {
	bool ok = false;
	std::string error;
	std::vector<IrCode> irCodes;
	std::vector<Console> consoles;
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

// Clamp a persisted index into [0, n). Out-of-bounds (negative or >=
// n) collapses to the nearest in-bounds value; n == 0 returns 0. Used by
// the shell when restoring a saved index across reboots and after the
// console list has changed.
int clampIndex(int requested, int n);

}  // namespace retroroom_core