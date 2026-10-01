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
    {"Detents", "detentsPerStep", retroroom_core::MenuAction::NONE, false, 1, 30, 1},
    {"Level", "selfPct", retroroom_core::MenuAction::NONE, false, 10, 100, 10},
};
// The shell appends its action rows to whatever the config declared, so
// the tests build the same shape rather than a menu with a row missing.
// The caller owns `buf`: the Menu points into it, so it must outlive
// every use of the Menu, which a returned-by-value helper could not
// guarantee.
static Menu menuWithActions(MenuItem* buf, int max, const MenuItem* items,
                            int count) {
	int n = 0;
	for (int i = 0; i < count && n < max - 2; ++i) {
		buf[n++] = items[i];
	}
	buf[n].label = retroroom_core::kMenuGoBackLabel;
	buf[n].key = nullptr;
	buf[n].action = retroroom_core::MenuAction::GO_BACK;
	buf[n].isBool = false;
	buf[n].lo = 0;
	buf[n].hi = 0;
	buf[n].step = 1;
	++n;
	Menu m;
	m.items = buf;
	m.count = n;
	return m;
}

static Menu menu2(MenuItem* buf) {
	return menuWithActions(buf, 8, kItems, 2);
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
	MenuItem oneBuf[4];
	const Menu one = menuWithActions(oneBuf, 4, &kItems[0], 1);
	menuOpen(s);
	MenuView v = menuView(s, one, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Detents"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, " Go Back"), v.row[1]);

	// Two settings: the second row is the other setting, and the window
	// only slides once the selection is past the bottom row.
	MenuItem twoBuf[8];
	const Menu m = menu2(twoBuf);
	menuOpen(s);
	v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Detents"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, " Level"), v.row[1]);

	// One detent down: the marker moves, the window does not.
	menuDetent(s, m, 1);
	v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, " Detents"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, ">Level"), v.row[1]);

	// Again: the window slides so Go Back is on the last row.
	menuDetent(s, m, 1);
	v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, " Level"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, ">Go Back"), v.row[1]);
}

// ---------------------------------------------------------------------------
// Scrolling clamps
// ---------------------------------------------------------------------------

// The selection stops at both ends. A knob that wrapped would take a
// setting past its limit without the operator seeing it happen.
static void test_the_selection_clamps_at_both_ends(void) {
	MenuItem buf[8];
	const Menu m = menu2(buf);
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
	MenuItem buf[8];
	const Menu m = menu2(buf);
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
	MenuItem wide = {"D", "detentsPerStep", retroroom_core::MenuAction::NONE, false, 1, 30, 1};
	MenuItem wideBuf[4];
	Menu m = menuWithActions(wideBuf, 4, &wide, 1);
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
	MenuItem stuck = {"D", "detentsPerStep", retroroom_core::MenuAction::NONE, false, 1, 30, 0};
	MenuItem stuckBuf[4];
	Menu m = menuWithActions(stuckBuf, 4, &stuck, 1);
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
	MenuItem buf[8];
	const Menu m = menu2(buf);
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
	// No declared items at all: the shell still appends Go Back, so
	// this is a menu with exactly one row rather than a menu with
	// nothing in it.
	MenuItem bareBuf[2];
	Menu m = menuWithActions(bareBuf, 2, nullptr, 0);
	MenuState s;
	menuOpen(s);
	TEST_ASSERT_EQUAL_INT(0, s.selected);

	menuDetent(s, m, 1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.selected,
								  "an empty menu must not scroll anywhere");
	menuDetent(s, m, -1);
	TEST_ASSERT_EQUAL_INT(0, s.selected);

	const MenuView v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Go Back"), v.row[0]);

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
	MenuItem buf[8];
	const Menu m = menu2(buf);
	MenuState s;
	menuOpen(s);
	menuSelect(s, m, 5);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(MenuMode::EDIT),
						  static_cast<int>(s.mode));

	MenuView v = menuView(s, m, 0, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "Current: 5"), v.row[0]);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "New: 5"), v.row[1]);

	menuDetent(s, m, 1);
	menuDetent(s, m, 1);
	v = menuView(s, m, 200, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "Current: 5"),
							 "Current must not move with the draft");
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "New: 7"), v.row[1]);
}

// A commit writes the *draft*, returns to the list, and shows the new
// value in the row for a moment before the label comes back.
static void test_a_commit_writes_the_draft_and_shows_it_briefly(void) {
	MenuItem buf[8];
	const Menu m = menu2(buf);
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
        // A confirmation of its own: the "Current:" line the operator was
        // just reading, carrying the value they chose, nothing below it.
        // It used to replace the row's *label* with a bare number, so the
        // setting appeared to lose its name for a second.
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "Current: 7"), v.row[0]);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, ""), v.row[1]);

	// And it reverts to the label on its own once the moment passes.
	menuTick(s, 1000 + kMenuSavedMs + 1);
	v = menuView(s, m, 1000 + kMenuSavedMs + 1, 2, 16);
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Detents"),
							 "the label must come back after the moment");
}

// A commit outside the editor is not a commit. Otherwise a stray click
// in the list would write a value nobody chose.
static void test_a_click_in_the_list_writes_nothing(void) {
	MenuItem buf[8];
	const Menu m = menu2(buf);
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
	MenuItem toggle = {"Night", "nightMode", retroroom_core::MenuAction::NONE, true, 0, 1, 1};
	MenuItem toggleBuf[4];
	Menu m = menuWithActions(toggleBuf, 4, &toggle, 1);
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
	MenuItem buf[8];
	const Menu m = menu2(buf);
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
						  retroroom_core::MenuAction::NONE, false, 1, 30, 1};
	MenuItem longBuf[4];
	Menu m = menuWithActions(longBuf, 4, &longLabel, 1);
	MenuState s;
	menuOpen(s);
	const MenuView v = menuView(s, m, 0, 2, 16);
	// Exactly the panel width -- no more, so nothing can have wrapped.
	TEST_ASSERT_EQUAL_INT_MESSAGE(16, static_cast<int>(std::strlen(v.row[0])),
								  "a row must be exactly the panel width");
	// And the visible part is the beginning of the label.
	TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">An Extremely Lo"), v.row[0]);
}

// ---- actions -------------------------------------------------------------

// The item array the shell builds: the declared settings, then Save,
// then Reboot, then Go Back. Go Back is last because that is where a
// menu is expected to put it, and Save/Reboot sit together because both
// are "things the system does", not settings.
struct SystemItem {
        const char* label;
        retroroom_core::MenuAction action;
};
static const SystemItem kSystem[] = {
        {"Save", retroroom_core::MenuAction::SAVE},
        {"Reboot", retroroom_core::MenuAction::REBOOT},
        {retroroom_core::kMenuGoBackLabel, retroroom_core::MenuAction::GO_BACK},
};

static Menu systemMenu(MenuItem* buf, const MenuItem* items, int count) {
        int n = 0;
        for (int i = 0; i < count && n < 8; ++i) {
                buf[n++] = items[i];
        }
        for (int i = 0; i < 3 && n < 8; ++i) {
                buf[n].label = kSystem[i].label;
                buf[n].key = nullptr;
                buf[n].action = kSystem[i].action;
                buf[n].isBool = false;
                buf[n].lo = 0;
                buf[n].hi = 0;
                buf[n].step = 1;
                ++n;
        }
        Menu m;
        m.items = buf;
        m.count = n;
        return m;
}

// An action row is not editable. Clicking one must not open the editor
// and leave the knob turning a value that does not exist.
static void test_an_action_row_cannot_be_edited(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);  // now on Save
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, s.selected, "expected to be on Save");

        menuSelect(s, m, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(retroroom_core::MenuMode::CONFIRM),
                                      static_cast<int>(s.mode),
                                      "Save must ask rather than just do it");
        // And the knob does not adjust a draft that is not there.
        const int before = s.draft;
        menuDetent(s, m, 1);
        TEST_ASSERT_EQUAL_INT_MESSAGE(before, s.draft,
                                      "an action row has no draft to move");
}

// The prompt says what it will do rather than asking a yes/no. That is
// the whole reason it exists: "Save" and "Save+Reboot" are the same
// decision told honestly, and the operator finds out about the restart
// before committing to it.
static void test_the_prompt_names_the_consequence(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;

        // Save, nothing needing a restart.
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);
        s.pendingNeedsReboot = false;
        menuSelect(s, m, 0);
        MenuView v = menuView(s, m, 0, 2, 16);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Save"), v.row[0]);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, " Go Back"), v.row[1]);

        // Save, with a restart owed.
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);
        s.pendingNeedsReboot = true;
        menuSelect(s, m, 0);
        v = menuView(s, m, 0, 2, 16);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Save & Reboot"), v.row[0]);
        // rowIs() trims the padding, so the comparison above is against
        // the text and not the filled row: ">Save & Reboot" is thirteen
        // columns. ">Save + Reboot" would be exactly sixteen and fit
        // with nothing to spare, which is the kind of tight fit that
        // breaks silently the next time somebody rewords it.
}

// Backing out changes nothing. The action is not returned, so a caller
// switching on the result cannot act on something the operator declined.
static void test_backing_out_of_the_prompt_chooses_nothing(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);
        menuSelect(s, m, 0);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(retroroom_core::MenuMode::CONFIRM),
                             static_cast<int>(s.mode));

        menuDetent(s, m, 1);  // move to Go Back
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, s.selected, "expected to be on Go Back");
        TEST_ASSERT_EQUAL_INT_MESSAGE(
                static_cast<int>(retroroom_core::MenuAction::NONE),
                menuSelect(s, m, 0),
                "backing out must not return an action to perform");
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(retroroom_core::MenuMode::LIST),
                                      static_cast<int>(s.mode),
                                      "backing out returns to the list");
}

// Confirming returns the action that was asked about, and the restart
// flag is readable next to it.
static void test_confirming_returns_the_action_and_its_reboot_flag(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);
        s.pendingNeedsReboot = true;
        menuSelect(s, m, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
                static_cast<int>(retroroom_core::MenuAction::SAVE),
                menuSelect(s, m, 0),
                "confirming must return the action being asked about");
        TEST_ASSERT_TRUE_MESSAGE(retroroom_core::menuActionNeedsReboot(s),
                                 "the reboot flag must survive the choice");
}

// Go Back needs no prompt. It is reversible and the operator asked for
// it; a confirm step here is friction on an already deep menu.
static void test_go_back_is_immediate(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        for (int i = 0; i < 4; ++i) {
                menuDetent(s, m, 1);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(
                static_cast<int>(retroroom_core::MenuAction::GO_BACK),
                menuSelect(s, m, 0),
                "Go Back must act rather than ask");
        TEST_ASSERT_FALSE(retroroom_core::menuIsOpen(s));
}

// The message covers the list and expires on its own. It is a
// transient notice, not a menu, and a fixed three seconds on a 16x2 is
// a blink rather than a message.
static void test_a_message_covers_the_list_then_goes_away(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        menuMessage(s, 1000, 500);

        MenuView v = menuView(s, m, 1100, 2, 16);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, "Reboot to see"), v.row[0]);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, "all changes"), v.row[1]);

        menuTick(s, 1600);
        TEST_ASSERT_FALSE(retroroom_core::menuMessageVisible(s, 1600));
        v = menuView(s, m, 1600, 2, 16);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Detents"),
                                 "the list must come back under the message");
}

// The confirmation screen scrolls two rows, not the whole menu. Reported
// as "it takes three extra detents to get back up": the clamp was
// against the menu's length, so turning up from the top of a two-row
// prompt silently walked the three rows underneath it before returning.
static void test_the_prompt_only_scrolls_its_own_two_rows(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);
        menuSelect(s, m, 0);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(retroroom_core::MenuMode::CONFIRM),
                             static_cast<int>(s.mode));

        // Down to the second row, and no further.
        menuDetent(s, m, 1);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, s.selected, "one row down");
        menuDetent(s, m, 1);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, s.selected,
                                      "a two-row prompt has nowhere further to go");
        // And back up in one, not three.
        menuDetent(s, m, -1);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.selected,
                                      "one row up, not a walk back through the "
                                      "items below the prompt");
        menuDetent(s, m, -1);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.selected, "and it stays at the top");
}

// The marker belongs to the selected row in the prompt too, and neither
// row carries a number.
static void test_the_prompt_marks_the_chosen_row(void) {
        MenuItem buf[8];
        const Menu m = systemMenu(buf, kItems, 2);
        MenuState s;
        menuOpen(s);
        menuDetent(s, m, 1);
        menuDetent(s, m, 1);
        s.pendingNeedsReboot = true;
        menuSelect(s, m, 0);

        MenuView v = menuView(s, m, 0, 2, 16);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, ">Save & Reboot"), v.row[0]);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, " Go Back"), v.row[1]);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, " Go Back") == false ||
                                 v.row[1][0] == ' ',
                                 "the unselected row must not carry the marker");

        // Move down: the marker follows.
        menuDetent(s, m, 1);
        v = menuView(s, m, 0, 2, 16);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 0, " Save & Reboot"), v.row[0]);
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, ">Go Back"), v.row[1]);
        // rowIs() trims the panel padding, so the comparison above is
        // against the text: ">Go Back" is eight columns and would still
        // read the same with a "2:" in front. Asserting the width here
        // only proved the row was padded.
        TEST_ASSERT_TRUE_MESSAGE(rowIs(v, 1, ">Go Back"),
                                 "the marker must move to the chosen row");
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
	// Actions: the rows that do something other than edit a setting.
	RUN_TEST(test_an_action_row_cannot_be_edited);
	RUN_TEST(test_the_prompt_names_the_consequence);
	RUN_TEST(test_the_prompt_only_scrolls_its_own_two_rows);
	RUN_TEST(test_the_prompt_marks_the_chosen_row);
	RUN_TEST(test_backing_out_of_the_prompt_chooses_nothing);
	RUN_TEST(test_confirming_returns_the_action_and_its_reboot_flag);
	RUN_TEST(test_go_back_is_immediate);
	RUN_TEST(test_a_message_covers_the_list_then_goes_away);
	return UNITY_END();
}
