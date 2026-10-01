// Functional core for the long-press config menu. See CabinetMenu.h for
// why it is separate from the shell.
//
// The renderer writes into a fixed char buffer and clips rather than
// wraps: a 16x2 panel has no room to spare, and a label that runs off
// the right edge is worse than one that is cut short. The shell prints
// a string and does not know about columns at all.

#include "CabinetMenu.h"

#include <cstdio>

namespace retroroom_core {

const char* const kMenuGoBackLabel = "Go Back";
const std::uint32_t kMenuSavedMs = 1500;

namespace {

// "Go Back" is an ordinary item the shell appends, not a row past the
// end of the array. That is why this is just the count: every entry
// has an index, and no caller has to remember that one of them is
// special.
int totalEntries(const Menu& m) {
	return m.count > 0 ? m.count : 0;
}

int clampInt(int v, int lo, int hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

bool expired(std::uint32_t nowMs, std::uint32_t untilMs) {
	return untilMs != 0 &&
		   static_cast<std::int32_t>(nowMs - untilMs) >= 0;
}

// The inverse of expired(), and *not* just its negation: savedUntilMs
// is zero when nothing has been saved, and `!expired(now, 0)` would
// report that a value is being shown when the field is the "nothing
// yet" sentinel. Inverting the wrong function is how every row renders
// as "1:>0".
bool showingSaved(std::uint32_t nowMs, std::uint32_t untilMs) {
	return untilMs != 0 &&
		   static_cast<std::int32_t>(nowMs - untilMs) < 0;
}

// Blank one row to `width`, NUL-terminated within its own storage.
void blank(char* out, int storage, int width) {
	for (int i = 0; i < storage - 1; ++i) {
		out[i] = (i < width) ? ' ' : '\0';
	}
	out[storage - 1] = '\0';
}

// Write `text` into `out` starting at `col`, clipped to `width`.
void place(char* out, int storage, int width, int col, const char* text) {
	if (text == nullptr) {
		return;
	}
	for (int i = 0; text[i] != '\0' && col + i < width && col + i < storage - 1;
		 ++i) {
		out[col + i] = text[i];
	}
}

// The label for one list row.
//
// A just-committed value used to be substituted *here*, so the row
// read as a bare number for a second and then got its name back. That
// is a confirmation the operator has to infer, and it looks broken
// because the thing that vanishes is the thing they were looking for.
// The value now gets its own screen; see the saved-value branch in
// menuView.
const char* listLabel(const Menu& m, int index) {
	return m.items[index].label;
}

}  // namespace

int menuWindowFirst(int selected, int total, int rows) {
	if (total < 0) total = 0;
	if (rows < 1) rows = 1;
	if (rows > total) rows = total;
	// Scrolling down parks the selection on the last row, scrolling up
	// on the first. Both ends clamp, so the window never scrolls past
	// the last entry and never goes negative.
	int first = selected - rows + 1;
	if (first < 0) first = 0;
	if (first > total - rows) first = total - rows;
	return first;
}

void menuOpen(MenuState& s) {
	s.mode = MenuMode::LIST;
	s.selected = 0;
	s.draft = 0;
	s.current = 0;
	s.saved = 0;
	s.savedUntilMs = 0;
}

void menuClose(MenuState& s) {
	s.mode = MenuMode::CLOSED;
	s.savedUntilMs = 0;
}

bool menuIsOpen(const MenuState& s) {
	return s.mode != MenuMode::CLOSED;
}

void menuDetent(MenuState& s, const Menu& m, int direction) {
	if (direction == 0) {
		return;
	}
	const int total = totalEntries(m);
	if (s.mode == MenuMode::CONFIRM) {
		// Two rows, not the whole menu. Clamping against the *menu's*
		// length here meant turning up from the top of a two-row prompt
		// silently walked through the three items below it before coming
		// back -- the confirmation was scrolling a list it is not
		// showing. Reported as "it takes three extra detents to get back".
		s.selected = clampInt(s.selected + direction, 0, 1);
		return;
	}
	if (s.mode == MenuMode::LIST) {
		// Clamp, never wrap. A knob that wraps the ends of a menu has
		// silently changed a setting on its way to somewhere the
		// operator was not looking.
		s.selected = clampInt(s.selected + direction, 0, total - 1);
		return;
	}
	if (s.mode != MenuMode::EDIT) {
		return;  // a click while confirming is a confirmation, not an edit
	}
	if (m.count <= 0 || s.selected < 0 || s.selected >= m.count) {
		return;  // nothing being edited
	}
	const MenuItem& item = m.items[s.selected];
	if (item.key == nullptr || item.action != MenuAction::NONE) {
		return;  // an action row has no draft to adjust
	}
	if (item.isBool) {
		// A bool has no ends to clamp to, it just alternates. Testing
		// the draft for non-zero rather than adding one means the first
		// turn always reaches the *other* value, whatever it started as.
		s.draft = (s.draft != 0) ? 0 : 1;
		return;
	}
	// A step of 0 would pin the value and read as a broken encoder, so
	// treat it as 1. A row that cannot be moved is not a setting.
	const int step = item.step > 0 ? item.step : 1;
	s.draft = clampInt(s.draft + direction * step, item.lo, item.hi);
}

MenuAction menuSelect(MenuState& s, const Menu& m, int currentValue) {
	if (s.mode == MenuMode::CONFIRM) {
		// Row 0 is "do it", row 1 is "go back". Decided here rather
		// than by the caller, so the answer on screen and the answer
		// acted on cannot come from different places.
		//
		// Read the row BEFORE resetting it. Capturing it afterwards
		// would compare a zero against a zero and always agree.
		const bool confirmed = (s.selected == 0);
		const MenuAction action = s.pending;
		s.pending = MenuAction::NONE;
		s.mode = MenuMode::LIST;
		s.selected = 0;
		// pendingNeedsReboot is deliberately left set: the single
		// MenuAction return cannot also carry "and restart", so the
		// caller reads menuActionNeedsReboot() immediately after. See
		// its comment for the contract.
		return confirmed ? action : MenuAction::NONE;
	}
	if (s.mode != MenuMode::LIST) {
		return MenuAction::NONE;  // a click while editing is a commit
	}
	const int total = totalEntries(m);
	s.selected = clampInt(s.selected, 0, total - 1);
	if (s.selected < 0 || s.selected >= m.count) {
		return MenuAction::NONE;
	}
	const MenuItem& item = m.items[s.selected];
	if (item.key == nullptr || item.action != MenuAction::NONE) {
		// An action. Free ones happen; the rest open a confirmation,
		// which is the shell's decision to make rather than the menu's,
		// because "is this worth a prompt" is not a property of a row.
		if (item.action == MenuAction::GO_BACK) {
			menuClose(s);
			return MenuAction::GO_BACK;
		}
		s.mode = MenuMode::CONFIRM;
		s.pending = item.action;
		s.selected = 0;
		return MenuAction::NONE;
	}
	s.mode = MenuMode::EDIT;
	// The menu's own range wins over the field's, and is allowed to be
	// narrower: a menu offering 1..30 for a field the file allows 1..64
	// is a menu choosing what to expose, which is the point of a
	// data-driven menu.
	s.current = item.isBool ? (currentValue != 0 ? 1 : 0) : currentValue;
	s.draft = item.isBool ? s.current : clampInt(currentValue, item.lo, item.hi);
	// Opening the editor is not a choice of action; the
	// click that saves the draft returns through menuCommit().
	return MenuAction::NONE;
}

bool menuCommit(MenuState& s, std::uint32_t nowMs, int* valueOut) {
	if (s.mode != MenuMode::EDIT) {
		return false;
	}
	if (valueOut != nullptr) {
		*valueOut = s.draft;
	}
	s.saved = s.draft;
	s.savedUntilMs = nowMs + kMenuSavedMs;
	s.mode = MenuMode::LIST;
	return true;
}

void menuTick(MenuState& s, std::uint32_t nowMs) {
	if (expired(nowMs, s.savedUntilMs)) {
		s.savedUntilMs = 0;
	}
	// The message expires here rather than at render time so that a
	// caller which asks "is there a message" and a caller which draws
	// one cannot disagree about whether it is still up.
	if (expired(nowMs, s.messageUntilMs)) {
		s.messageUntilMs = 0;
	}
}

void menuMessage(MenuState& s, std::uint32_t nowMs,
                 std::uint32_t durationMs) {
	s.messageUntilMs = nowMs + durationMs;
}

bool menuActionNeedsReboot(const MenuState& s) {
	return s.pendingNeedsReboot;
}

bool menuMessageVisible(const MenuState& s, std::uint32_t nowMs) {
	return s.messageUntilMs != 0 &&
		   static_cast<std::int32_t>(nowMs - s.messageUntilMs) < 0;
}

MenuView menuView(const MenuState& s, const Menu& m, std::uint32_t nowMs,
                  int rows, int width) {
	MenuView v;
	for (int r = 0; r < 2; ++r) {
		blank(v.row[r], 17, 16);
	}
	if (s.mode == MenuMode::CLOSED) {
		return v;
	}
	if (rows < 1) rows = 1;
	if (rows > 2) rows = 2;
	if (width < 6) width = 6;
	if (width > 16) width = 16;

	if (s.mode == MenuMode::EDIT) {
		// "1:Current: 5" / "2:New: 5".
		//
		// No selection marker on either row. The labels already say
		// which is which, and the one that moves when the knob turns is
		// the one being edited -- a '>' on it would be a third way of
		// saying the same thing.
		char buf[17];
		std::snprintf(buf, sizeof(buf), "Current: %d", s.current);
		place(v.row[0], 17, width, 0, buf);
		std::snprintf(buf, sizeof(buf), "New: %d", s.draft);
		place(v.row[1], 17, width, 0, buf);
		return v;
	}

	if (s.mode == MenuMode::CONFIRM) {
		// The one place the menu says what it is about to do rather
		// than asking a yes/no the operator has to interpret. "Save" and
		// "Save+Reboot" are the same decision told honestly, and the
		// operator finds out the restart before they commit to it rather
		// than after.
		char buf[17];
		std::snprintf(buf, sizeof(buf), s.selected == 0 ? ">%s" : " %s", s.pending == MenuAction::REBOOT
										   ? "Reboot"
										   : (s.pendingNeedsReboot ? "Save & Reboot"
																  : "Save"));
		place(v.row[0], 17, width, 0, buf);
		place(v.row[1], 17, width, 0, s.selected == 0 ? " Go Back"
													   : ">Go Back");
		return v;
	}
	const bool savedVisible = showingSaved(nowMs, s.savedUntilMs);


	// A message covers the list entirely. Not stacked on it: on sixteen
	// columns there is nowhere to put a notice without hiding the thing
	// the notice is about, and "Reboot to see all changes" is only
	// meaningful on its own.
	// A commit's confirmation. Its own screen rather than a mutated
	// list row, because "4" replacing the name of a setting is not
	// something the operator reads as "that worked" -- it reads as the
	// menu having lost its labels.
	if (savedVisible) {
		char buf[17];
		// "Current: N" rather than a bare number or a "Saved" header: the
		// word the operator has been reading for the last ten seconds
		// now names the number they just chose, so the screen is
		// recognisably the same one with a new value in it.
		std::snprintf(buf, sizeof(buf), "Current: %d", s.saved);
		place(v.row[0], 17, width, 0, buf);
		// Row 1 stays blank rather than echoing the value: one line
		// saying it happened is enough, and a second line of it would
		// be another thing to misread.
		place(v.row[1], 17, width, 0, "");
		return v;
	}
	if (menuMessageVisible(s, nowMs)) {
		place(v.row[0], 17, width, 0, "Reboot to see");
		place(v.row[1], 17, width, 0, "all changes");
		return v;
	}

	const int total = totalEntries(m);
	const int first = menuWindowFirst(s.selected, total, rows);

	for (int r = 0; r < rows; ++r) {
		const int index = first + r;
		if (index < 0 || index >= total) {
			continue;
		}
		char buf[17];
		// "1:>Label" when selected, "1:Label" when not. The marker
		// The marker shifts the text by a column on purpose: a row that
		// is indented is a row that is selected, without spending two
		// columns on a highlight.
		//
		// No row number. The '>' alone says which row is selected, and the
		// count is what the scrolling window is for: a number that
		// renumbers itself as you scroll tells the operator nothing they
		// can act on.
		std::snprintf(buf, sizeof(buf), "%s%s",
							  index == s.selected ? ">" : " ",
							  listLabel(m, index));
		place(v.row[r], 17, width, 0, buf);
	}
	return v;
}

}  // namespace retroroom_core
