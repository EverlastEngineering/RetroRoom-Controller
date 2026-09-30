#pragma once

// Functional core for the long-press config menu.
//
// Pure state machine and renderer: no Arduino, no LiquidCrystal, no
// millis() of its own, no globals. The shell feeds it detents and
// clicks and prints the two rows it returns.
//
// Why this is a lib and not a screenful of code in src/display.cpp:
// the interesting properties here are all things a test can check and
// a person cannot see. Which row is highlighted after the selection
// scrolls off the top. Whether the draft value clamps at the ends
// instead of wrapping. Whether "Go Back" is the last item and cannot be
// scrolled past. Whether the new value is written back to the config at
// all. Those belong in a file the host can compile.
//
// A second reason, learned the hard way this session: a decision that
// lives in src/ is a decision nothing can reach, and the abandoned-
// browse retreat was wrong four times in a row for exactly that
// reason.

#include <cstdint>

namespace retroroom_core {

// The label of the implicit last item. Not a `led` key -- it edits
// nothing, it leaves.
extern const char* const kMenuGoBackLabel;

// How long a saved value stays on screen before the label comes back.
extern const std::uint32_t kMenuSavedMs;

enum class MenuMode : std::uint8_t {
	CLOSED,
	// Choosing an item. The knob scrolls, a click opens it.
	LIST,
	// Changing it. The knob adjusts the draft, a click commits.
	EDIT,
};

// One adjustable `led` option, parsed from the config's `menu` array.
//
// `key` names a field in the shared table in
// lib/ConsoleConfig/LedFieldTable.cpp -- the same table the file parser
// used to read the value. That is the whole point: adding an option to
// the menu is a line of JSON, because the menu needs no per-key
// plumbing that the parser does not already have.
struct MenuItem {
	// What the operator reads. Must fit the display; the renderer
	// clips rather than wraps, because a label that silently runs off
	// the right edge is worse than a truncated one.
	const char* label;
	const char* key;
	// A bool cycles 0 <-> 1 and ignores lo/hi/step. An int steps by
	// `step` and clamps at lo/hi.
	bool isBool;
	int lo;
	int hi;
	int step;
};

// The parsed `menu` array. The shell owns the storage; the core only
// reads it. `count` may be zero, which is a menu with nothing in it
// except "Go Back" -- see the lock-down item in todo/.
struct Menu {
	const MenuItem* items = nullptr;
	int count = 0;
};

struct MenuState {
	MenuMode mode = MenuMode::CLOSED;
	// 0..count-1 for an item, and `count` for "Go Back". One past the
	// array rather than a sentinel inside it, so the selection and the
	// index into the items are the same number and cannot disagree.
	int selected = 0;
	// The value being edited, and the value it started from. Separate
	// because the editor shows both -- "Current" against "New" -- and
	// because the value just committed is a third thing again.
	int draft = 0;
	int current = 0;
	int saved = 0;
	// While the clock is before this, the selected row shows the saved
	// value in place of its label. Zero means "not showing".
	std::uint32_t savedUntilMs = 0;
};

// Two rows of the display, NUL-terminated and always `width` columns
// wide including the terminator, so the shell can print them without
// knowing anything about clipping.
struct MenuView {
	char row[2][17];
};

// The topmost visible index for a given selection. Clamped so the
// selection is always on screen and the window stops at both ends
// rather than scrolling past the last item.
int menuWindowFirst(int selected, int total, int rows);

// Open, close, and whether the menu owns the knob right now.
void menuOpen(MenuState& s);
void menuClose(MenuState& s);
bool menuIsOpen(const MenuState& s);

// A detent. In LIST it moves the selection; in EDIT it moves the draft.
// `direction` is -1 or +1. Both clamp rather than wrap: a knob that
// wraps at the ends of a menu is a knob that has silently changed a
// setting.
void menuDetent(MenuState& s, const Menu& m, int direction);

// A click. On "Go Back" it closes. On an item it opens the editor,
// seeded with `currentValue` clamped into the item's own range -- which
// is allowed to be narrower than the field's, so a menu can offer 1..30
// for a field the file allows 1..64.
void menuSelect(MenuState& s, const Menu& m, int currentValue);

// A click in the editor. Writes the draft through `valueOut` and
// returns to the list, showing the new value for kMenuSavedMs.
// Returns false (and writes nothing) in any other mode.
bool menuCommit(MenuState& s, std::uint32_t nowMs, int* valueOut);

// Advance the clock so the saved value reverts to the label on its own.
void menuTick(MenuState& s, std::uint32_t nowMs);

// Render. `rows` is how many of the two lines to fill, `width` how many
// columns each is, and `nowMs` is the clock that decides whether a
// saved value is still being shown. All three are parameters because
// the 16x2 panel is a fact about this cabinet and not about the menu,
// and the tests want to check the window arithmetic without needing a
// display of a particular size.
MenuView menuView(const MenuState& s, const Menu& m, std::uint32_t nowMs,
                  int rows, int width);

}  // namespace retroroom_core
