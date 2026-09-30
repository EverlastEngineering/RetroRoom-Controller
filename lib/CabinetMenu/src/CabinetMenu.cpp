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

// "Go Back" sits one past the last item, so the selectable range is
// 0..count inclusive: count+1 entries in total.
int totalEntries(const Menu& m) {
	return (m.count > 0 ? m.count : 0) + 1;
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

// The label for one list row, or the saved value while the
// just-committed value is still on screen.
const char* listLabel(const MenuState& s, const Menu& m, int index,
					  bool showingSaved) {
	static char valueText[12];
	if (showingSaved && index == s.selected) {
		// In the label's place, right-aligned. There is no room for
		// "label = value" on sixteen columns, and the row is already
		// marked with '>' so there is no ambiguity about which item it
		// belongs to.
		std::snprintf(valueText, sizeof(valueText), "%d", s.saved);
		return valueText;
	}
	if (index >= m.count) {
		return kMenuGoBackLabel;
	}
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
	if (s.mode == MenuMode::LIST) {
		// Clamp, never wrap. A knob that wraps the ends of a menu has
		// silently changed a setting on its way to somewhere the
		// operator was not looking.
		s.selected = clampInt(s.selected + direction, 0, total - 1);
		return;
	}
	if (s.mode != MenuMode::EDIT) {
		return;
	}
	if (m.count <= 0 || s.selected < 0 || s.selected >= m.count) {
		return;  // nothing being edited
	}
	const MenuItem& item = m.items[s.selected];
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

void menuSelect(MenuState& s, const Menu& m, int currentValue) {
	if (s.mode != MenuMode::LIST) {
		return;  // a click while editing is a commit, not a re-open
	}
	const int total = totalEntries(m);
	s.selected = clampInt(s.selected, 0, total - 1);
	if (s.selected >= m.count) {
		// "Go Back": leave, rather than opening an editor for an item
		// that does not exist.
		menuClose(s);
		return;
	}
	s.mode = MenuMode::EDIT;
	const MenuItem& item = m.items[s.selected];
	// The menu's own range wins over the field's, and is allowed to be
	// narrower: a menu offering 1..30 for a field the file allows 1..64
	// is a menu choosing what to expose, which is the point of a
	// data-driven menu.
	s.current = item.isBool ? (currentValue != 0 ? 1 : 0) : currentValue;
	s.draft = item.isBool ? s.current : clampInt(currentValue, item.lo, item.hi);
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
		std::snprintf(buf, sizeof(buf), "1:Current: %d", s.current);
		place(v.row[0], 17, width, 0, buf);
		std::snprintf(buf, sizeof(buf), "2:New: %d", s.draft);
		place(v.row[1], 17, width, 0, buf);
		return v;
	}

	const int total = totalEntries(m);
	const int first = menuWindowFirst(s.selected, total, rows);
	const bool savedVisible = showingSaved(nowMs, s.savedUntilMs);

	for (int r = 0; r < rows; ++r) {
		const int index = first + r;
		if (index < 0 || index >= total) {
			continue;
		}
		char buf[17];
		// "1:>Label" when selected, "1:Label" when not. The marker
		// shifts the text by a column on purpose: a row that is
		// indented is a row that is selected, without spending two
		// columns on a highlight.
		std::snprintf(buf, sizeof(buf), "%d:%s%s", r + 1,
					  index == s.selected ? ">" : "",
					  listLabel(s, m, index, savedVisible));
		place(v.row[r], 17, width, 0, buf);
	}
	return v;
}

}  // namespace retroroom_core
