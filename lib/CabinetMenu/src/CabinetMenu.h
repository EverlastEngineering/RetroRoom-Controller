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

// Upper bound on a menu's declared items, so the shell can size the
// array it appends its action rows to. Not a limit the menu enforces --
// the core takes whatever it is given -- but a ceiling on what a
// hand-edited config should be able to ask for.
constexpr int kMaxMenuItems = 32;

// The label of the "Go Back" action row. Not a config key -- it edits
// nothing, it leaves.
extern const char* const kMenuGoBackLabel;

// How long a saved value stays on screen before the label comes back.
extern const std::uint32_t kMenuSavedMs;

enum class MenuMode : std::uint8_t {
	CLOSED,
	// Choosing an item. The knob scrolls, a click opens it -- or runs it,
	// if it is an action rather than a setting.
	LIST,
	// Changing it. The knob adjusts the draft, a click commits.
	EDIT,
	// Answering "are you sure?". Only the action items get here, and
	// only the ones with a consequence: a plain GO_BACK is immediate,
	// and there is nothing to confirm about leaving.
	CONFIRM,
};

// What a row does that is not editing a setting.
//
// Actions are items rather than a special case, so the list has no
// special-cased row and no off-by-one index: GO_BACK is just the last
// entry, like everything else. The shell appends these to whatever the
// config declared, so a config that says nothing still has a menu with
// a way out of it -- which is also what makes an empty `menu` array a
// lock-down rather than a trap.
enum class MenuAction : uint8_t {
	// An ordinary setting. `key` names a config field.
	NONE,
	// Leave the menu.
	GO_BACK,
	// Write the changed settings to flash.
	SAVE,
	// Restart the cabinet.
	REBOOT,
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
	// The config field this edits, or null for an action item. The
	// presence of this is also what decides whether a click opens the
	// editor or runs the action -- there is no separate flag to fall
	// out of step with it.
	const char* key;
	MenuAction action;
	// A bool cycles 0 <-> 1 and ignores lo/hi/step. An int steps by
	// `step` and clamps at lo/hi.
	bool isBool;
	int lo;
	int hi;
	int step;
	// Apply the draft while the operator is turning, rather than
	// waiting for the click.
	//
	// Per item, and off by default, because "should this setting be
	// visible as I turn it" is a judgement about the setting and not
	// about the code. Brightness wants it -- the whole difficulty is
	// that you cannot tell what 30 looks like and it costs a
	// round-trip through save and reboot to find out otherwise.
	// detentsPerStep very much does not: a threshold that changes
	// mid-turn invalidates the detent gate's accumulated position, so
	// the knob would lurch partway through the very gesture that is
	// setting it.
	bool preview;

	// A constructor, so a MenuItem is never half-built.
	//
	// The config parser assigned six of the seven fields and left
	// `action` to whatever was on the stack. Detents happened to get 0
	// and worked; Brightness did not, and was treated as an action row,
	// so clicking it never opened an editor. The compiler cannot catch a
	// *missing* initialiser, and six assignments that look complete are
	// exactly what a reader waves through.
	//
	// Every field defaults here, so the safe thing is the only thing:
	// "MenuItem item;" is a valid NONE action, and adding an eighth
	// field later cannot be silently forgotten. Brace-initialising with
	// all seven still works, so the existing call sites are unaffected.
	MenuItem(const char* label_ = nullptr, const char* key_ = nullptr,
			 MenuAction action_ = MenuAction::NONE, bool isBool_ = false,
			 int lo_ = 0, int hi_ = 0, int step_ = 1, bool preview_ = false)
		: label(label_),
		  key(key_),
		  action(action_),
		  isBool(isBool_),
		  lo(lo_),
		  hi(hi_),
		  step(step_),
		  preview(preview_) {}
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
	// The row the operator is on. Every entry is a real item now, so
	// there is no "count means Go Back" special case to get wrong.
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
	// What CONFIRM is asking about, and whether it wants a restart as
	// part of it. `needsReboot` is what picks the wording -- "Save" or
	// "Save+Reboot" -- so the button says what it will do rather than
	// asking a yes/no the operator has to interpret.
	MenuAction pending = MenuAction::NONE;
	bool pendingNeedsReboot = false;
	// A message to show instead of the list, until this time. Used for
	// the "Reboot to see all changes" notice. Zero means none.
	std::uint32_t messageUntilMs = 0;
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

// A click. Returns the action the operator just chose, or NONE if the
// click did not choose one.
//
// That single return value is the whole shell interface: the caller
// switches on it and does not otherwise need to know which mode the
// menu was in. LIST + a setting opens the editor and returns NONE;
// LIST + Go Back closes and returns GO_BACK; LIST + Save or Reboot
// opens a confirmation and returns NONE; CONFIRM + the first row
// returns the action that was being asked about; CONFIRM + the second
// row returns NONE, having changed nothing.
//
// On a setting, `currentValue` seeds the editor, clamped into the
// item's own range -- which is allowed to be narrower than the
// field's, so a menu can offer 1..30 for a field the file allows
// 1..64.
MenuAction menuSelect(MenuState& s, const Menu& m, int currentValue);

// Show a message instead of the list, until `nowMs + durationMs`.
// Deliberately not "for N seconds": the notice exists to say something
// the operator needs to read, and a fixed duration on a 16x2 is a
// blink rather than a message. The caller decides when it has been read
// -- a detent or a click ends it early -- and this is only the cap.
void menuMessage(MenuState& s, std::uint32_t nowMs, std::uint32_t durationMs);

// Is the message still being shown?
bool menuMessageVisible(const MenuState& s, std::uint32_t nowMs);

// Does the action just returned by menuSelect() need a restart to take
// effect?
//
// The contract is narrow and deliberate: valid immediately after
// menuSelect() returns, for the action that was being confirmed. A
// single MenuAction return cannot also say "and restart", and adding a
// second return value for one bit of information would put the two out
// of step at some call site. The shell sets pendingNeedsReboot when it
// raises the confirmation, by asking configFieldNeedsReboot() whether
// any setting it changed is init-bound.
bool menuActionNeedsReboot(const MenuState& s);

// The draft to apply *right now*, if the item being edited is one the
// config asked to preview. False for everything else, so the default
// stays "a setting changes when you commit it, not while you turn it".
//
// The core decides only *whether* to preview -- it has no idea what a
// setting is or where its storage lives, so it hands back a number and
// the shell applies it through the same setLedFeelValue() the commit
// uses. One path, so a previewed value and a committed value cannot
// end up in different places.
bool menuPreviewValue(const MenuState& s, const Menu& m, int* valueOut);

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
