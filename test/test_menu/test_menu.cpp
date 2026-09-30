// Host-side Unity tests for the long-press config menu state machine in
// lib/CabinetMenu.
//
// The shell that draws this is src/display.cpp, which needs Arduino and
// LiquidCrystal and so cannot run on the host. Everything here is the
// decision half -- which row is selected, what the draft is, and what
// the two rows say -- and the shell's only remaining job is to print
// them.
//
// These are the properties worth having a test for, because every one
// of them is something a person watching a 16x2 panel cannot check:
//
//   Window      -- the selection stays on screen as it scrolls, and the
//                  window stops at both ends instead of running off.
//   Clamping    -- the draft stops at its limits rather than wrapping. A
//                  knob that wraps the ends of a menu has silently
//                  changed a setting.
//   Go Back     -- it is the last entry, always, and it cannot be
//                  scrolled past -- even in a menu with no items at all.
//   The editor  -- "Current" is the value the row started from, and
//                  "New" is the draft, and they are not the same thing.
//   The commit  -- it writes the *draft* back, and only once.

#include <cstdlib>
#include <cstring>
#include <CabinetMenu.h>
#include <unity.h>

using retroroom_core::Menu;
using retroroom_core::MenuItem;
using retroroom_core::MenuMode;
using retroroom_core::MenuState;
using retroroom_core::MenuView;
using retroroom_core::menuCommit;
using retroroom_core::menuDetent;
using retroroom_core::menuIsOpen;
using retroroom_core::menuOpen;
using retroroom_core::menuSelect;
using retroroom_core::menuTick;
using retroroom_core::menuView;
using retroroom_core::menuWindowFirst;
using retroroom_core::kMenuGoBackLabel;
using retroroom_core::kMenuSavedMs;

void setUp(void) {}
void tearDown(void) {}

// The example from the spec: two configurable items plus Go Back.
static const MenuItem kItems[] = {
    {"Detents", "detentsPerStep", false, 1, 30, 1},
    {"Level", "selfPct", false, 10, 100, 10},
};
static Menu menu2() {
	Menu m;
	m.items = kItems;
	m.count = 2;
	return m;
}

// Compare a row against an expectation, ignoring the padding to the
// panel width.
//
// The padding is deliberate and matches display.cpp's lcd_print_fitted():
// a row shorter than the panel is space-filled so the previous contents
// are overwritten rather than leaving a tail of the old line behind. So
// every row really is `width` columns, and comparing against an unpadded
// string would fail on correct output.
static bool rowIs(const MenuView& v, int r, const char* expected) {
	char actual[17];
	int i = 0;
	for (; i < 16 && v.row[r][i] != '\0'; ++i) {
		actual[i] = v.row[r][i];
	}
	actual[i] = '\0';
	for (int k = i - 1; k >= 0 && actual[k] == ' '; --k) {
		actual[k] = '\0';
	}
	return std::strcmp(actual, expected) == 0;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

// The selection is always on screen. Three entries in two rows: the
// window slides so the selected row is never off the top or the bottom.
static void test_the_window_follows_the_selection(void) {
	// 0..2 over two rows.
	TEST_ASSERT_EQUAL_INT(0, menuWindowFirst(0, 3, 2));
	TEST_ASSERT_EQUAL_INT(0, menuWindowFirst(1, 3, 2));
	TEST_ASSERT_EQUAL_INT(1, menuWindowFirst(2, 3, 2));
	// And never before the first entry, scrolling up. Note that the
	// window does *not* move on the first step down -- the selection
	// moves to the bottom row first and only then does the window
	// follow, which is what the spec's three screens show.
	TEST_ASSERT_EQUAL_INT(0, menuWindowFirst(0, 5, 2));
	TEST_ASSERT_EQUAL_INT(0, menuWindowFirst(1, 5, 2));
	TEST_ASSERT_EQUAL_INT(1, menuWindowFirst(2, 5, 2));
	// Nor past the last, scrolling down. The last selectable entry with
	// five in a two-row window is 4, and the window stops at 3 so 4 is
	// on the bottom row.
	TEST_ASSERT_EQUAL_INT(3, menuWindowFirst(4, 5, 2));
	// Fewer entries than rows: the window is the whole list.
	TEST_ASSERT_EQUAL_INT(0, menuWindowFirst(0, 1, 2));
	TEST_ASSERT_EQUAL_INT(0, menuWindowFirst(1, 2, 2));
}

// The exact screens from the spec, row for row.
//
// Two cases, because the spec draws two: with a single setting "Go Back"
// is the second row, and with two settings the window has to slide to
// reach it.
static void test_the_list_renders_as_specified(void) {
	MenuState s;

	// One setting: both rows are visible, no scrolling.
	Menu one;
	one.items = &kItems[0];
	one.count = 1;
	menuOpen(s);
	MenuView v = menuView(s, one, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:>Detents"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:Go Back"), v.row[1]);

	// Two settings: the second row is the other setting, and the window
	// only slides once the selection is past the bottom row.
	const Menu m = menu2();
	menuOpen(s);
	v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:>Detents"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:Level"), v.row[1]);

	// One detent down: the marker moves, the window does not.
	menuDetent(s, m, 1);
	v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:Detents"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:>Level"), v.row[1]);

	// Again: the window slides so Go Back is on the last row.
	menuDetent(s, m, 1);
	v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:Level"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:>Go Back"), v.row[1]);
}

// ---------------------------------------------------------------------------
// Scrolling clamps
// ---------------------------------------------------------------------------

// The selection stops at both ends. A knob that wrapped would take a
// setting past its limit without the operator seeing it happen.
static void test_the_selection_clamps_at_both_ends(void) {
	const Menu m = menu2();
	MenuState s;
	menuOpen(s);

	for (int i = 0; i < 20; ++i) {
		menuDetent(s, m, 1);
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(2, s.selected,
								  "the selection must stop on Go Back");
	TEST_ASSERT_EQUAL_STRING_MESSAGE(kMenuGoBackLabel, "Go Back",
									  kMenuGoBackLabel);

	for (int i = 0; i < 20; ++i) {
		menuDetent(s, m, -1);
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.selected,
								  "the selection must stop on the first item");
}

// The draft clamps at the item's own limits, which may be narrower than
// the field's.
static void test_the_draft_clamps_at_its_limits(void) {
	const Menu m = menu2();
	MenuState s;
	menuOpen(s);
	menuSelect(s, m, 5);  // detentsPerStep, 1..30
	TEST_ASSERT_EQUAL_INT(5, s.draft);

	for (int i = 0; i < 40; ++i) {
		menuDetent(s, m, 1);
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(30, s.draft, "the draft must stop at hi");
	for (int i = 0; i < 80; ++i) {
		menuDetent(s, m, -1);
	}
	TEST_ASSERT_EQUAL_INT_MESSAGE(1, s.draft, "the draft must stop at lo");
}

// A menu item's range may be narrower than the field's own, and the
// value the row started from is clamped into it on the way in.
static void test_a_narrow_menu_range_clamps_the_incoming_value(void) {
	MenuItem wide = {"D", "detentsPerStep", false, 1, 30, 1};
	Menu m;
	m.items = &wide;
	m.count = 1;
	MenuState s;
	menuOpen(s);
	// The live config says 64, which the *file* allows but the menu
	// does not offer. Opening the editor must land somewhere legal.
	menuSelect(s, m, 64);
	TEST_ASSERT_EQUAL_INT_MESSAGE(30, s.draft,
								  "a value outside the menu's range must be "
								  "clamped on the way in");
	TEST_ASSERT_EQUAL_INT(30, s.draft);
}

// A step of 0 would pin the value and read as a broken encoder.
static void test_a_zero_step_still_moves(void) {
	MenuItem stuck = {"D", "detentsPerStep", false, 1, 30, 0};
	Menu m;
	m.items = &stuck;
	m.count = 1;
	MenuState s;
	menuOpen(s);
	menuSelect(s, m, 5);
	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(6, s.draft, "a zero step must behave as 1");
}

// ---------------------------------------------------------------------------
// Go Back
// ---------------------------------------------------------------------------

// Clicking Go Back leaves. It is the last entry, always.
static void test_go_back_closes_the_menu(void) {
	const Menu m = menu2();
	MenuState s;
	menuOpen(s);
	menuDetent(s, m, 1);
	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT(2, s.selected);
	menuSelect(s, m, 0);
	TEST_ASSERT_FALSE_MESSAGE(menuIsOpen(s), "Go Back must close the menu");
	TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuMode::CLOSED),
						  static_cast<int>(s.mode));
}

// A menu with no items at all is "Go Back" and nothing else -- which is
// what makes an empty `menu` array in the config a lock-down rather
// than a menu with a blank screen in it.
static void test_an_empty_menu_is_only_go_back(void) {
	Menu m;  // count = 0, items = null
	MenuState s;
	menuOpen(s);
	TEST_ASSERT_EQUAL_INT(0, s.selected);

	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.selected,
								  "an empty menu must not scroll anywhere");
	menuDetent(s, m, -1);
	TEST_ASSERT_EQUAL_INT(0, s.selected);

	const MenuView v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:>Go Back"), v.row[0]);

	// And it closes.
	menuSelect(s, m, 0);
	TEST_ASSERT_FALSE(menuIsOpen(s));
}

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------

// "Current" is what the row started from and "New" is the draft. They
// are not the same number, and conflating them is the bug this shape of
// UI usually has: turn the knob, see "Current" change too, and there is
// no way to tell what you started from.
static void test_the_editor_shows_current_and_new_separately(void) {
	const Menu m = menu2();
	MenuState s;
	menuOpen(s);
	menuSelect(s, m, 5);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuMode::EDIT),
						  static_cast<int>(s.mode));

	MenuView v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:Current: 5"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:New: 5"), v.row[1]);

	menuDetent(s, m, 1);
	menuDetent(s, m, 1);
	v = menuView(s, m, 200, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:Current: 5"),
							 "Current must not move with the draft");
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:New: 7"), v.row[1]);
}

// A commit writes the *draft*, returns to the list, and shows the new
// value in the row for a moment before the label comes back.
static void test_a_commit_writes_the_draft_and_shows_it_briefly(void) {
	const Menu m = menu2();
	MenuState s;
	menuOpen(s);
	menuSelect(s, m, 5);
	menuDetent(s, m, 1);
	menuDetent(s, m, 1);

	int written = -1;
	TEST_ASSERT_TRUE(menuCommit(s, 1000, &written));
	TEST_ASSERT_EQUAL_INT_MESSAGE(7, written,
								  "a commit must write the draft, not the "
								  "current value");
	TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuMode::LIST),
						  static_cast<int>(s.mode));

	// The value is shown in the selected row, in place of the label. The
	// other row is untouched, which is what makes it obvious the value
	// belongs to the marked item and not to the list.
	MenuView v = menuView(s, m, 1100, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:>7"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "2:Level"), v.row[1]);

	// And it reverts to the label on its own once the moment passes.
	menuTick(s, 1000 + kMenuSavedMs + 1);
	v = menuView(s, m, 1000 + kMenuSavedMs + 1, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:>Detents"),
							 "the label must come back after the moment");
}

// A commit outside the editor is not a commit. Otherwise a stray click
// in the list would write a value nobody chose.
static void test_a_click_in_the_list_writes_nothing(void) {
	const Menu m = menu2();
	MenuState s;
	menuOpen(s);
	int written = -1;
	TEST_ASSERT_FALSE_MESSAGE(menuCommit(s, 0, &written),
							  "committing outside the editor must fail");
	TEST_ASSERT_EQUAL_INT(-1, written);
}

// A bool alternates rather than stepping, and the first turn always
// reaches the *other* value whatever it started as.
static void test_a_bool_alternates(void) {
	MenuItem toggle = {"Night", "nightMode", true, 0, 1, 1};
	Menu m;
	m.items = &toggle;
	m.count = 1;
	MenuState s;
	menuOpen(s);

	menuSelect(s, m, 0);
	TEST_ASSERT_EQUAL_INT(0, s.draft);
	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(1, s.draft, "a bool must alternate up");
	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.draft, "and back down");

	// Starting from a non-zero value, one turn must reach 0, not 2.
	menuOpen(s);
	menuSelect(s, m, 1);
	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.draft,
								  "a bool must alternate, not increment");
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// A closed menu renders nothing. The shell paints the normal live
// display underneath, and blanking it would look like a crash.
static void test_a_closed_menu_renders_nothing(void) {
	const Menu m = menu2();
	MenuState s;
	const MenuView v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ""), v.row[0]);
	TEST_ASSERT_TRUE(rowIs(v, 1, ""));
	// And it is genuinely blank, not merely short.
	TEST_ASSERT_TRUE_MESSAGE(std::strcmp(v.row[0], "                ") == 0,
							  "a closed menu must leave the row blank");
}

// A label too long for the panel is clipped, not wrapped. Sixteen
// columns is the whole width and there is no second line to spill onto.
static void test_a_long_label_is_clipped_not_wrapped(void) {
	MenuItem longLabel = {"An Extremely Long Setting Name", "detentsPerStep",
						  false, 1, 30, 1};
	Menu m;
	m.items = &longLabel;
	m.count = 1;
	MenuState s;
	menuOpen(s);
	const MenuView v = menuView(s, m, 0, 2, 16);
	// Exactly the panel width -- no more, so nothing can have wrapped.
	TEST_ASSERT_EQUAL_INT_MESSAGE(16, static_cast<int>(std::strlen(v.row[0])),
								  "a row must be exactly the panel width");
	// And the visible part is the beginning of the label.
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "1:>An Extremely"), v.row[0]);
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();
	// The window.
	RUN_TEST(test_the_window_follows_the_selection);
	RUN_TEST(test_the_list_renders_as_specified);
	// Clamping -- a knob that wraps has silently changed a setting.
	RUN_TEST(test_the_selection_clamps_at_both_ends);
	RUN_TEST(test_the_draft_clamps_at_its_limits);
	RUN_TEST(test_a_narrow_menu_range_clamps_the_incoming_value);
	RUN_TEST(test_a_zero_step_still_moves);
	// Go Back, including in a menu with nothing in it.
	RUN_TEST(test_go_back_closes_the_menu);
	RUN_TEST(test_an_empty_menu_is_only_go_back);
	// The editor.
	RUN_TEST(test_the_editor_shows_current_and_new_separately);
	RUN_TEST(test_a_commit_writes_the_draft_and_shows_it_briefly);
	RUN_TEST(test_a_click_in_the_list_writes_nothing);
	RUN_TEST(test_a_bool_alternates);
	// Rendering.
	RUN_TEST(test_a_closed_menu_renders_nothing);
	RUN_TEST(test_a_long_label_is_clipped_not_wrapped);
	return UNITY_END();
}
