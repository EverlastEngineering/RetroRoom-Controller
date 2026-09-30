#include <ConsoleConfig.h>
#include <unity.h>

#include <string>

using retroroom_core::Console;
using retroroom_core::stepWithin;
using retroroom_core::canStepWithin;
using retroroom_core::IrCode;
using retroroom_core::LoadResult;
using retroroom_core::Selection;

void setUp(void) {}
void tearDown(void) {}

void test_loads_valid_config(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_EQUAL(1, result.consoles.size());
	TEST_ASSERT_EQUAL_STRING("NES", result.consoles[0].id.c_str());
	TEST_ASSERT_EQUAL_STRING("Nintendo Entertainment System", result.consoles[0].name.c_str());
	TEST_ASSERT_EQUAL(0x430, result.consoles[0].tvinput);
	TEST_ASSERT_EQUAL(1, result.consoles[0].selector_position);
	TEST_ASSERT_EQUAL(5, result.consoles[0].led_position);
	TEST_ASSERT_EQUAL(1, result.consoles[0].led_width);
}

void test_loads_multiple_consoles(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430", "YUV": "0xE30"},
            "consoleNames": {
                "NES": "Nintendo Entertainment System",
                "SNES": "Super Nintendo Entertainment System"
            },
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1},
                {"id": "SNES", "tvInput": "YUV", "selectorPosition": 2, "ledPosition": 15, "ledWidth": 5}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_EQUAL(2, result.consoles.size());
	TEST_ASSERT_EQUAL(0x430, result.consoles[0].tvinput);
	TEST_ASSERT_EQUAL(0xE30, result.consoles[1].tvinput);
	TEST_ASSERT_EQUAL_STRING("NES", result.consoles[0].id.c_str());
	TEST_ASSERT_EQUAL_STRING("SNES", result.consoles[1].id.c_str());
}

void test_resolves_display_name_from_console_names(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"GEN": "Sega Genesis"},
            "consoles": [
                {"id": "GEN", "tvInput": "Video", "selectorPosition": 3, "ledPosition": 15, "ledWidth": 5}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_EQUAL_STRING("Sega Genesis", result.consoles[0].name.c_str());
}

void test_falls_back_to_id_when_no_display_name(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_EQUAL_STRING("NES", result.consoles[0].name.c_str());
}

void test_rejects_unknown_tv_input(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "YUV", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_FALSE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_TRUE(result.error.find("YUV") != std::string::npos);
}

void test_rejects_bad_hex(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "NOTHEX"},
            "consoles": []
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_FALSE(result.ok);
}

void test_rejects_malformed_json(void) {
	LoadResult result = retroroom_core::loadFromJson("{not valid");
	TEST_ASSERT_FALSE(result.ok);
}

void test_rejects_missing_ir_codes(void) {
	const char* json = R"({"consoles": []})";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_FALSE(result.ok);
	TEST_ASSERT_TRUE(result.error.find("irCodes") != std::string::npos);
}

void test_rejects_missing_consoles_array(void) {
	const char* json = R"({"irCodes": {"Video": "0x430"}})";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_FALSE(result.ok);
}

void test_rejects_empty_id(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 0, "ledWidth": 1}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_FALSE(result.ok);
}

void test_selection_rotate_forward(void) {
	std::vector<Console> cs = {
		{"A", "a", 0x1, 1, 0, 1},
		{"B", "b", 0x2, 2, 1, 1},
		{"C", "c", 0x3, 3, 2, 1},
	};
	Selection sel(cs);
	TEST_ASSERT_EQUAL(0, sel.index());
	TEST_ASSERT_TRUE(sel.rotate(1));
	TEST_ASSERT_EQUAL(1, sel.index());
	TEST_ASSERT_EQUAL_STRING("b", sel.current().id.c_str());
	TEST_ASSERT_TRUE(sel.rotate(1));
	TEST_ASSERT_EQUAL(2, sel.index());
}

void test_selection_rotate_backward(void) {
	std::vector<Console> cs = {
		{"A", "a", 0x1, 1, 0, 1},
		{"B", "b", 0x2, 2, 1, 1},
	};
	Selection sel(cs);
	sel.rotate(1);  // move to 1
	TEST_ASSERT_EQUAL(1, sel.index());
	TEST_ASSERT_TRUE(sel.rotate(-1));
	TEST_ASSERT_EQUAL(0, sel.index());
}

void test_selection_no_wrap_at_edges(void) {
	std::vector<Console> cs = {
		{"A", "a", 0x1, 1, 0, 1},
		{"B", "b", 0x2, 2, 1, 1},
	};
	Selection sel(cs);
	TEST_ASSERT_EQUAL(0, sel.index());
	TEST_ASSERT_FALSE_MESSAGE(sel.rotate(-1), "should refuse to rotate below 0 without wraparound");
	TEST_ASSERT_EQUAL(0, sel.index());
	sel.rotate(1);  // → 1
	TEST_ASSERT_FALSE_MESSAGE(sel.rotate(1), "should refuse to rotate past end without wraparound");
	TEST_ASSERT_EQUAL(1, sel.index());
}

void test_selection_wraparound(void) {
	std::vector<Console> cs = {
		{"A", "a", 0x1, 1, 0, 1},
		{"B", "b", 0x2, 2, 1, 1},
		{"C", "c", 0x3, 3, 2, 1},
	};
	Selection sel(cs);
	// From 0, rotate -1 with wraparound → 2
	TEST_ASSERT_TRUE(sel.rotate(-1, true));
	TEST_ASSERT_EQUAL(2, sel.index());
	TEST_ASSERT_EQUAL_STRING("c", sel.current().id.c_str());
	// From 2, rotate +1 with wraparound → 0
	TEST_ASSERT_TRUE(sel.rotate(1, true));
	TEST_ASSERT_EQUAL(0, sel.index());
}

void test_selection_empty_vector(void) {
	std::vector<Console> cs;
	Selection sel(cs);
	TEST_ASSERT_TRUE(sel.empty());
	TEST_ASSERT_FALSE(sel.rotate(1));
	TEST_ASSERT_FALSE(sel.rotate(-1));
	TEST_ASSERT_FALSE(sel.rotate(1, true));
}

void test_selection_zero_direction_is_noop(void) {
	std::vector<Console> cs = {
		{"A", "a", 0x1, 1, 0, 1},
	};
	Selection sel(cs);
	TEST_ASSERT_FALSE(sel.rotate(0));
	TEST_ASSERT_FALSE(sel.rotate(0, true));
}

void test_wraparound_next_forward(void) {
	using retroroom_core::wraparoundNext;
	TEST_ASSERT_EQUAL(1, wraparoundNext(0, 3, 1));
	TEST_ASSERT_EQUAL(2, wraparoundNext(1, 3, 1));
	TEST_ASSERT_EQUAL(0, wraparoundNext(2, 3, 1));  // wraps end → start
}

void test_wraparound_next_backward(void) {
	using retroroom_core::wraparoundNext;
	TEST_ASSERT_EQUAL(2, wraparoundNext(0, 3, -1));  // wraps start → end
	TEST_ASSERT_EQUAL(0, wraparoundNext(1, 3, -1));
	TEST_ASSERT_EQUAL(1, wraparoundNext(2, 3, -1));
}

void test_wraparound_next_single_element(void) {
	using retroroom_core::wraparoundNext;
	TEST_ASSERT_EQUAL(0, wraparoundNext(0, 1, 1));   // single element forward stays
	TEST_ASSERT_EQUAL(0, wraparoundNext(0, 1, -1));  // single element backward stays
}

void test_wraparound_next_zero_direction(void) {
	using retroroom_core::wraparoundNext;
	TEST_ASSERT_EQUAL(2, wraparoundNext(2, 5, 0));  // no change
	TEST_ASSERT_EQUAL(0, wraparoundNext(0, 5, 0));  // no change
}

void test_wraparound_next_empty(void) {
	using retroroom_core::wraparoundNext;
	TEST_ASSERT_EQUAL(0, wraparoundNext(0, 0, 1));   // empty → 0
	TEST_ASSERT_EQUAL(0, wraparoundNext(5, 0, 1));   // any index, n=0 → 0
}

void test_clamp_index_in_bounds(void) {
	using retroroom_core::clampIndex;
	TEST_ASSERT_EQUAL(0, clampIndex(0, 3));
	TEST_ASSERT_EQUAL(1, clampIndex(1, 3));
	TEST_ASSERT_EQUAL(2, clampIndex(2, 3));
}

void test_clamp_index_out_of_bounds(void) {
	using retroroom_core::clampIndex;
	TEST_ASSERT_EQUAL(2, clampIndex(5, 3));   // too large → last valid
	TEST_ASSERT_EQUAL(0, clampIndex(0, 3));   // in-bounds lower
	TEST_ASSERT_EQUAL(0, clampIndex(-1, 3));  // negative → 0
	TEST_ASSERT_EQUAL(0, clampIndex(-100, 3));
}

void test_clamp_index_empty(void) {
	using retroroom_core::clampIndex;
	TEST_ASSERT_EQUAL(0, clampIndex(0, 0));   // empty list, any input → 0
	TEST_ASSERT_EQUAL(0, clampIndex(5, 0));
	TEST_ASSERT_EQUAL(0, clampIndex(-1, 0));
}

void test_loads_per_console_tagline(void) {
	// Per-console `tagline` is optional. When present it surfaces on
	// the Console struct; when absent it defaults to "".
	const char* with_tagline =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1, "tagline": "Now you're playing with power!"}
            ]
        })";
	LoadResult r1 = retroroom_core::loadFromJson(with_tagline);
	TEST_ASSERT_TRUE_MESSAGE(r1.ok, r1.error.c_str());
	TEST_ASSERT_EQUAL_STRING("Now you're playing with power!", r1.consoles[0].tagline.c_str());

	const char* without_tagline =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ]
        })";
	LoadResult r2 = retroroom_core::loadFromJson(without_tagline);
	TEST_ASSERT_TRUE_MESSAGE(r2.ok, r2.error.c_str());
	TEST_ASSERT_EQUAL_STRING("", r2.consoles[0].tagline.c_str());
}

void test_loads_lcd_backlight_default_when_absent(void) {
	// No top-level `lcd` block: backlightOffAfterMs defaults to 30000 ms.
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ]
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_EQUAL_UINT32(30000, result.lcdBacklightOffAfterMs);
}

void test_loads_lcd_backlight_explicit_value(void) {
	// Top-level `lcd.backlightOffAfterMs` honored when present.
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ],
            "lcd": {"backlightOffAfterMs": 60000}
        })";
	LoadResult result = retroroom_core::loadFromJson(json);
	TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
	TEST_ASSERT_EQUAL_UINT32(60000, result.lcdBacklightOffAfterMs);
}

void test_loads_lcd_backlight_clamps_out_of_range(void) {
	// Out-of-range values are clamped (0 = never off is legal;
	// 600000 = 10 min is the upper bound). Operator typos shouldn't
	// brick the device.
	const char* too_high =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ],
            "lcd": {"backlightOffAfterMs": 99999999}
        })";
	LoadResult high = retroroom_core::loadFromJson(too_high);
	TEST_ASSERT_TRUE_MESSAGE(high.ok, high.error.c_str());
	TEST_ASSERT_EQUAL_UINT32(600000, high.lcdBacklightOffAfterMs);

	const char* negative =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 5, "ledWidth": 1}
            ],
            "lcd": {"backlightOffAfterMs": -1000}
        })";
	LoadResult neg = retroroom_core::loadFromJson(negative);
	TEST_ASSERT_TRUE_MESSAGE(neg.ok, neg.error.c_str());
	TEST_ASSERT_EQUAL_UINT32(0, neg.lcdBacklightOffAfterMs);
}

static void test_step_within_moves_inside_the_list(void) {
	TEST_ASSERT_EQUAL(1, stepWithin(0, 4, 1));
	TEST_ASSERT_EQUAL(3, stepWithin(2, 4, 1));
	TEST_ASSERT_EQUAL(1, stepWithin(2, 4, -1));
	TEST_ASSERT_EQUAL(0, stepWithin(1, 4, -1));
}

static void test_step_within_stops_at_both_ends(void) {
	// The cabinet has physical ends. Turning past one reaches nothing,
	// so the cursor stays put rather than wrapping to the far end.
	TEST_ASSERT_EQUAL(3, stepWithin(3, 4, 1));
	TEST_ASSERT_EQUAL_MESSAGE(3, stepWithin(3, 4, 1), "must not wrap to 0");
	TEST_ASSERT_EQUAL(0, stepWithin(0, 4, -1));
	TEST_ASSERT_EQUAL_MESSAGE(0, stepWithin(0, 4, -1), "must not wrap to 3");
	// A single-console list is a fixed point in both directions.
	TEST_ASSERT_EQUAL(0, stepWithin(0, 1, 1));
	TEST_ASSERT_EQUAL(0, stepWithin(0, 1, -1));
}

static void test_step_within_degenerate_inputs(void) {
	TEST_ASSERT_EQUAL(0, stepWithin(0, 0, 1));
	TEST_ASSERT_EQUAL(0, stepWithin(5, 0, -1));
	// direction 0 is a no-op, not a step.
	TEST_ASSERT_EQUAL(2, stepWithin(2, 4, 0));
	// An out-of-range current is clamped to an end rather than
	// escaping: a stale index from a hand-edited config must not walk
	// off the list.
	TEST_ASSERT_EQUAL(3, stepWithin(99, 4, 1));
	TEST_ASSERT_EQUAL(0, stepWithin(-5, 4, -1));
}

static void test_can_step_within_agrees_with_step(void) {
	TEST_ASSERT_TRUE(canStepWithin(0, 4, 1));
	TEST_ASSERT_TRUE(canStepWithin(3, 4, -1));
	TEST_ASSERT_FALSE(canStepWithin(3, 4, 1));
	TEST_ASSERT_FALSE(canStepWithin(0, 4, -1));
	TEST_ASSERT_FALSE(canStepWithin(0, 1, 1));
	TEST_ASSERT_FALSE(canStepWithin(0, 1, -1));
	TEST_ASSERT_FALSE(canStepWithin(2, 4, 0));
	TEST_ASSERT_FALSE(canStepWithin(0, 0, 1));
}

// A config with no `led` block at all must come out exactly as it did
// before the block existed. That is the whole promise of making every
// field optional, and it is the thing that lets an operator hand-edit a
// file without reading this.
void test_a_config_with_no_led_block_gets_the_defaults(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 5, "ledWidth": 1}
            ]
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
        const retroroom_core::LedFeel d = retroroom_core::defaultLedFeel();
        TEST_ASSERT_EQUAL(d.totalLeds, r.feel.totalLeds);
        TEST_ASSERT_EQUAL(d.detentsPerStep, r.feel.detentsPerStep);
        TEST_ASSERT_EQUAL(d.explodeMs, r.feel.explodeMs);
        TEST_ASSERT_EQUAL(d.igniteMs, r.feel.igniteMs);
        TEST_ASSERT_EQUAL(d.frameIntervalMs, r.feel.frameIntervalMs);
        TEST_ASSERT_EQUAL(0, static_cast<int>(r.warnings.size()));
}

// The field the whole exercise is about: a string longer than 64.
void test_total_leds_comes_from_the_config(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 55, "ledWidth": 39}
            ],
            "led": {"totalLeds": 118}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
        TEST_ASSERT_EQUAL(118, r.feel.totalLeds);
        TEST_ASSERT_EQUAL(0, static_cast<int>(r.warnings.size()));
        // ...and the window that used to be silently clipped now fits
        // inside it. This is the actual point: on a 64-LED build a
        // console at LED 55 could be at most 9 wide, and the config
        // asked for 39.
        TEST_ASSERT_TRUE_MESSAGE(55 + 39 <= r.feel.totalLeds,
                                 "the declared window must fit the declared strip");
}

// A config claiming more LEDs than the firmware was built for cannot be
// believed: the buffer is allocated at compile time and the animation
// would address LEDs that do not exist. It is pulled down to the
// capacity and *reported*, because this is a hardware mismatch and the
// operator is the one who has to resolve it.
void test_total_leds_cannot_exceed_what_was_built_for(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"totalLeds": 4096}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(retroroom_core::kLedStripCapacity, r.feel.totalLeds);
        TEST_ASSERT_TRUE_MESSAGE(r.warnings.size() > 0,
                                 "a hardware mismatch must be reported, not silent");
}

// One number, one home: a partial block changes only what it names.
void test_a_partial_led_block_leaves_everything_else_alone(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"detentsPerStep": 3}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_EQUAL(3, r.feel.detentsPerStep);
        const retroroom_core::LedFeel d = retroroom_core::defaultLedFeel();
        TEST_ASSERT_EQUAL(d.travelMs, r.feel.travelMs);
        TEST_ASSERT_EQUAL(d.explodeMs, r.feel.explodeMs);
        TEST_ASSERT_EQUAL(d.ringIdleMs, r.feel.ringIdleMs);
        TEST_ASSERT_EQUAL(d.colorR[retroroom_core::kRoleSelected],
                         r.feel.colorR[retroroom_core::kRoleSelected]);
}

// The ring's four timings all parse. They were split across three
// places -- two in the config, the fade hardcoded in the shell, and the
// grace after a hand left not existing at all -- so they are pinned
// together here rather than one at a time.
void test_the_ring_timings_all_parse(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"ringIdleMs": 4000, "ringFlashMs": 250,
                    "ringOffDelayMs": 750, "ringFadeMs": 500}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(4000, r.feel.ringIdleMs);
        TEST_ASSERT_EQUAL(250, r.feel.ringFlashMs);
        TEST_ASSERT_EQUAL(750, r.feel.ringOffDelayMs);
        TEST_ASSERT_EQUAL(500, r.feel.ringFadeMs);
}

// Both new ring timings default, so a config written before they existed
// behaves exactly as it did.
void test_the_new_ring_timings_default(void) {
        const retroroom_core::LedFeel d = retroroom_core::defaultLedFeel();
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ]
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_EQUAL(d.ringOffDelayMs, r.feel.ringOffDelayMs);
        TEST_ASSERT_EQUAL(d.ringFadeMs, r.feel.ringFadeMs);
}

// The `menu` array parses into items that name real `led` keys, so
// adding an adjustable setting is a line of JSON rather than a
// firmware change.
void test_the_menu_array_parses(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "menu": [
                {"label": "Detents", "set": "led.detentsPerStep",
                 "min": 1, "max": 30},
                {"label": "Ring idle", "set": "led.ringIdleMs"}
            ]
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(2, static_cast<int>(r.menu.items.size()));
        TEST_ASSERT_EQUAL_STRING("detentsPerStep", r.menu.keys[0].c_str());
        TEST_ASSERT_FALSE(r.menu.items[0].isBool);
        // The menu's own narrower range, which is allowed.
        TEST_ASSERT_EQUAL(1, r.menu.items[0].lo);
        TEST_ASSERT_EQUAL(30, r.menu.items[0].hi);
        // With no min/max, the field's own range from the shared table.
        TEST_ASSERT_EQUAL(0, r.menu.items[1].lo);
        TEST_ASSERT_EQUAL(600000, r.menu.items[1].hi);
        // The view points at the same items.
        const retroroom_core::Menu v = r.menu.view();
        TEST_ASSERT_EQUAL(2, v.count);
        TEST_ASSERT_EQUAL_STRING("Detents", v.items[0].label);
}

// A menu entry naming something that is not a `led` key is dropped with
// a warning rather than taking the whole menu with it. A menu is the
// thing an operator edits by hand, so a typo in it is likely.
void test_a_menu_entry_naming_nothing_real_is_skipped(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "menu": [
                {"label": "Good", "set": "led.detentsPerStep"},
                {"label": "Bad", "set": "led.noSuchSetting"},
                {"label": "Missing set"}
            ]
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL_MESSAGE(1, static_cast<int>(r.menu.items.size()),
                                  "only the legal entry should survive");
        TEST_ASSERT_TRUE(r.warnings.size() >= 2);
}

// A menu may not offer a wider range than the setting allows. One that
// did would be a menu that lets the operator pick a value the file
// would refuse, which is worse than not offering it.
void test_a_menu_cannot_offer_more_than_the_setting_allows(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "menu": [
                {"label": "Too wide", "set": "led.detentsPerStep",
                 "min": 0, "max": 5000}
            ]
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        // The field's own range is 1..64.
        TEST_ASSERT_EQUAL(1, r.menu.items[0].lo);
        TEST_ASSERT_EQUAL(64, r.menu.items[0].hi);
}

// A config with no `menu` is a menu with nothing in it, not a failure.
// That is the lock-down case: no long-press target at all.
void test_a_config_with_no_menu_has_an_empty_one(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ]
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(0, static_cast<int>(r.menu.items.size()));
        const retroroom_core::Menu v = r.menu.view();
        TEST_ASSERT_EQUAL(0, v.count);
        TEST_ASSERT_TRUE(v.items == nullptr);
}

// Out of range is clamped *and reported*. Clamping quietly is the same
// as being wrong, from the operator's side of the glass.
void test_out_of_range_is_clamped_and_reported(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"fillPct": 500, "detentsPerStep": 0}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_EQUAL(100, r.feel.fillPct);
        TEST_ASSERT_EQUAL(1, r.feel.detentsPerStep);
        TEST_ASSERT_TRUE_MESSAGE(r.warnings.size() >= 2,
                                 "both clamps must be reported");
}

// Cross-field rules live in the loader, so one number in the file
// cannot be a lie on its own.
void test_fast_detents_cannot_exceed_detents_per_step(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"detentsPerStep": 4, "fastDetentsPerStep": 9}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_EQUAL(4, r.feel.fastDetentsPerStep);
        TEST_ASSERT_TRUE(r.warnings.size() > 0);
}

// Colours as [r, g, b] arrays, one per role, and a malformed one
// leaves the default rather than half a colour.
void test_colours_parse_and_malformed_ones_keep_the_default(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {
                "colors": {
                    "selected": [10, 20, 30],
                    "proposal": [1, 2, 3],
                    "fill": [9, 9]
                }
            }
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_EQUAL(10, r.feel.colorR[retroroom_core::kRoleSelected]);
        TEST_ASSERT_EQUAL(20, r.feel.colorG[retroroom_core::kRoleSelected]);
        TEST_ASSERT_EQUAL(30, r.feel.colorB[retroroom_core::kRoleSelected]);
        TEST_ASSERT_EQUAL(1, r.feel.colorR[retroroom_core::kRoleProposal]);
        const retroroom_core::LedFeel d = retroroom_core::defaultLedFeel();
        TEST_ASSERT_EQUAL_MESSAGE(d.colorR[retroroom_core::kRoleFill],
                                  r.feel.colorR[retroroom_core::kRoleFill],
                                  "a malformed colour keeps the default");
        TEST_ASSERT_TRUE(r.warnings.size() > 0);
}

// The travel must stay the same hue as the proposal it hands over to,
// and that is now a value in a file an operator can edit.
void test_travel_and_proposal_keep_the_same_colour(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"colors": {"travel": [5, 6, 7]}}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        // Allowed to differ -- it is a value, and a value the operator
        // owns. But the *default* must not, and a test that only checked
        // the defaults would miss the file saying otherwise.
        TEST_ASSERT_EQUAL(5, r.feel.colorR[retroroom_core::kRoleTravel]);
        const retroroom_core::LedFeel d = retroroom_core::defaultLedFeel();
        TEST_ASSERT_EQUAL_MESSAGE(d.colorR[retroroom_core::kRoleTravel],
                                  d.colorR[retroroom_core::kRoleProposal],
                                  "by default the travel is the proposal in flight");
}

// ---- the writer ---------------------------------------------------------
//
// applyConfigEdits() is what "save" is, so the properties that make it
// safe to point at the live config file are the properties worth
// testing. The important ones are the negative space: what it must NOT
// lose, and what it must refuse.

// A key the parser does not model must survive a round trip. This is the
// reason the document is round-tripped rather than rebuilt from
// LedFeel: a config the firmware does not fully understand yet still
// has to come back out of a save intact.
void test_saving_keeps_a_key_the_parser_never_heard_of(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "somethingNewer": {"nested": [1, 2, 3]},
            "led": {"detentsPerStep": 5}
        })";
        const retroroom_core::ConfigEdit edits[] = {
            {"led.detentsPerStep", 9},
        };
        std::string out;
        std::string error;
        TEST_ASSERT_TRUE(retroroom_core::applyConfigEdits(
            json, edits, 1, &out, &error));
        TEST_ASSERT_TRUE_MESSAGE(error.empty(), error.c_str());

        // And the result is still a config the parser accepts, with the
        // new value and the unknown block both present.
        retroroom_core::LoadResult r = retroroom_core::loadFromJson(out.c_str());
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(9, r.feel.detentsPerStep);
        TEST_ASSERT_TRUE_MESSAGE(out.find("somethingNewer") != std::string::npos,
                                 "an unknown block must survive a save");
}

// A save changes what it was asked to change and nothing else.
void test_saving_changes_only_what_it_was_told_to(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"detentsPerStep": 5, "travelMs": 420, "totalLeds": 118}
        })";
        const retroroom_core::ConfigEdit edits[] = {
            {"led.detentsPerStep", 17},
        };
        std::string out, error;
        TEST_ASSERT_TRUE(retroroom_core::applyConfigEdits(
            json, edits, 1, &out, &error));
        retroroom_core::LoadResult r = retroroom_core::loadFromJson(out.c_str());
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(17, r.feel.detentsPerStep);
        TEST_ASSERT_EQUAL(420, r.feel.travelMs);
        TEST_ASSERT_EQUAL(118, r.feel.totalLeds);
}

// A key the file never mentioned, and a block the file never mentioned,
// are both created. An operator who adds a menu item for a new setting
// is doing something the file has to grow into.
void test_saving_creates_a_setting_and_a_block_that_were_absent(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"detentsPerStep": 5}
        })";
        const retroroom_core::ConfigEdit edits[] = {
            {"led.ringFadeMs", 450},
            {"lcd.backlightOffAfterMs", 12000},
        };
        std::string out, error;
        TEST_ASSERT_TRUE_MESSAGE(
            retroroom_core::applyConfigEdits(json, edits, 2, &out, &error),
            error.c_str());
        retroroom_core::LoadResult r = retroroom_core::loadFromJson(out.c_str());
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(450, r.feel.ringFadeMs);
        TEST_ASSERT_EQUAL(12000u, r.lcdBacklightOffAfterMs);
}

// A value outside the field's range is clamped on the way out, not left
// for the next boot to discover. A file that says 999999 and a running
// cabinet at 600000 is exactly the quiet disagreement this is all about.
void test_saving_clamps_to_the_field_range(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {}
        })";
        const retroroom_core::ConfigEdit edits[] = {
            {"led.detentsPerStep", 9999},
            {"lcd.backlightOffAfterMs", -5},
        };
        std::string out, error;
        TEST_ASSERT_TRUE(retroroom_core::applyConfigEdits(
            json, edits, 2, &out, &error));
        retroroom_core::LoadResult r = retroroom_core::loadFromJson(out.c_str());
        TEST_ASSERT_EQUAL(64, r.feel.detentsPerStep);
        TEST_ASSERT_EQUAL(0u, r.lcdBacklightOffAfterMs);
        TEST_ASSERT_TRUE_MESSAGE(r.warnings.empty(),
                                 "a value we clamped on write must not need "
                                 "clamping again on the way in");
}

// A bad save must not happen. An unparseable document and an unknown
// path are both refused, and the caller is told why.
void test_saving_refuses_what_it_cannot_do(void) {
        std::string out, error;

        TEST_ASSERT_FALSE(retroroom_core::applyConfigEdits(
            "{ this is not json", nullptr, 0, &out, &error));
        TEST_ASSERT_TRUE_MESSAGE(!error.empty(), "a refusal must say why");

        const char* json = R"({"irCodes": {"Video": "0x430"},
                               "consoles": [{"id": "NES", "tvInput": "Video",
                               "selectorPosition": 1, "ledPosition": 1,
                               "ledWidth": 1}]})";
        const retroroom_core::ConfigEdit edits[] = {
            {"led.noSuchSetting", 1},
        };
        TEST_ASSERT_FALSE(retroroom_core::applyConfigEdits(
            json, edits, 1, &out, &error));
        TEST_ASSERT_TRUE_MESSAGE(!error.empty(), "a refusal must say why");
}

// The lcd clamp now reports itself, so a bad value in the file is
// visible at boot rather than silently becoming a different number.
void test_the_lcd_clamp_is_reported(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "lcd": {"backlightOffAfterMs": 999999999}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL(600000u, r.lcdBacklightOffAfterMs);
        TEST_ASSERT_TRUE_MESSAGE(!r.warnings.empty(),
                                 "a clamp nobody hears about is the bug it "
                                 "replaced");
}

// The block registry: a path resolves across blocks, and a bare key
// means "led".
void test_config_paths_resolve_across_blocks(void) {
        const char* block = nullptr;
        TEST_ASSERT_TRUE(retroroom_core::findConfigField("led.detentsPerStep", &block) != nullptr);
        TEST_ASSERT_EQUAL_STRING("led", block);
        TEST_ASSERT_TRUE(retroroom_core::findConfigField("lcd.backlightOffAfterMs", &block) != nullptr);
        TEST_ASSERT_EQUAL_STRING("lcd", block);
        // Bare means the first block.
        TEST_ASSERT_TRUE(retroroom_core::findConfigField("detentsPerStep", &block) != nullptr);
        TEST_ASSERT_EQUAL_STRING("led", block);
        // And the negative cases, which are what the menu uses to drop
        // an entry naming a setting nobody wrote.
        TEST_ASSERT_TRUE(retroroom_core::findConfigField("led.nope", &block) == nullptr);
        TEST_ASSERT_TRUE(retroroom_core::findConfigField("nope.detentsPerStep", &block) == nullptr);
        TEST_ASSERT_TRUE(retroroom_core::findConfigField("", &block) == nullptr);
}

// Exactly one setting needs a restart, and the property lives with the
// field so the menu prompt, the API response and the docs cannot
// disagree about which.
void test_only_the_strip_length_needs_a_reboot(void) {
        TEST_ASSERT_TRUE_MESSAGE(
            retroroom_core::configFieldNeedsReboot(
                retroroom_core::findConfigField("led.totalLeds", nullptr)),
            "totalLeds is bound into FastLED at init and must say so");
        TEST_ASSERT_FALSE(retroroom_core::configFieldNeedsReboot(
            retroroom_core::findConfigField("led.detentsPerStep", nullptr)));
        TEST_ASSERT_FALSE(retroroom_core::configFieldNeedsReboot(
            retroroom_core::findConfigField("lcd.backlightOffAfterMs", nullptr)));
}

// brightnessPct is the master scale for both strips, and 100 has to
// mean "exactly as it looked before" -- the base it multiplies is the
// one that used to be a #define in src/lighting.cpp.
void test_brightness_defaults_to_unchanged(void) {
        const retroroom_core::LedFeel d = retroroom_core::defaultLedFeel();
        TEST_ASSERT_EQUAL_MESSAGE(100, d.brightnessPct,
                                  "the default must preserve the previous "
                                  "appearance, or every cabinet dims on upgrade");
}

// 100 is a hardware ceiling, not a preference: above the base scale the
// strip asks for more current than the supply gives, and the cabinet
// browns out under load. The menu is allowed to offer less, never more.
void test_brightness_cannot_exceed_the_supply(void) {
        const char* json = R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [
                {"id": "NES", "tvInput": "Video", "selectorPosition": 1,
                 "ledPosition": 1, "ledWidth": 1}
            ],
            "led": {"brightnessPct": 250}
        })";
        LoadResult r = retroroom_core::loadFromJson(json);
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_EQUAL_MESSAGE(100, r.feel.brightnessPct,
                                  "more than 100 must be refused, not honoured");
        TEST_ASSERT_TRUE_MESSAGE(!r.warnings.empty(),
                                 "a clamp nobody hears about is the bug it "
                                 "replaced");
        // And zero is a legal, fully dark cabinet.
        const retroroom_core::ConfigEdit off[] = {{"led.brightnessPct", 0}};
        std::string out, error;
        // A *valid* document: loadFromJson() refuses one with no
        // irCodes, so round-tripping a fragment would test the parser's
        // rejection rather than the writer.
        const char* base =
            R"({"irCodes": {"Video": "0x430"},
                "consoles": [{"id": "NES", "tvInput": "Video",
                              "selectorPosition": 1, "ledPosition": 1,
                              "ledWidth": 1}],
                "led": {"brightnessPct": 100}})";
        TEST_ASSERT_TRUE(retroroom_core::applyConfigEdits(
            base, off, 1, &out, &error));
        LoadResult r2 = retroroom_core::loadFromJson(out.c_str());
        TEST_ASSERT_EQUAL(0, r2.feel.brightnessPct);
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();
	RUN_TEST(test_step_within_moves_inside_the_list);
	RUN_TEST(test_step_within_stops_at_both_ends);
	RUN_TEST(test_step_within_degenerate_inputs);
	RUN_TEST(test_can_step_within_agrees_with_step);
	RUN_TEST(test_loads_valid_config);
	RUN_TEST(test_loads_multiple_consoles);
	RUN_TEST(test_resolves_display_name_from_console_names);
	RUN_TEST(test_falls_back_to_id_when_no_display_name);
	RUN_TEST(test_rejects_unknown_tv_input);
	RUN_TEST(test_rejects_bad_hex);
	RUN_TEST(test_rejects_malformed_json);
	RUN_TEST(test_rejects_missing_ir_codes);
	RUN_TEST(test_rejects_missing_consoles_array);
	RUN_TEST(test_rejects_empty_id);
	RUN_TEST(test_selection_rotate_forward);
	RUN_TEST(test_selection_rotate_backward);
	RUN_TEST(test_selection_no_wrap_at_edges);
	RUN_TEST(test_selection_wraparound);
	RUN_TEST(test_selection_empty_vector);
	RUN_TEST(test_selection_zero_direction_is_noop);
	RUN_TEST(test_wraparound_next_forward);
	RUN_TEST(test_wraparound_next_backward);
	RUN_TEST(test_wraparound_next_single_element);
	RUN_TEST(test_wraparound_next_zero_direction);
	RUN_TEST(test_wraparound_next_empty);
	RUN_TEST(test_clamp_index_in_bounds);
	RUN_TEST(test_clamp_index_out_of_bounds);
	RUN_TEST(test_clamp_index_empty);
	RUN_TEST(test_loads_per_console_tagline);
	RUN_TEST(test_loads_lcd_backlight_default_when_absent);
	RUN_TEST(test_loads_lcd_backlight_explicit_value);
	RUN_TEST(test_loads_lcd_backlight_clamps_out_of_range);
	RUN_TEST(test_travel_and_proposal_keep_the_same_colour);
	RUN_TEST(test_colours_parse_and_malformed_ones_keep_the_default);
	RUN_TEST(test_fast_detents_cannot_exceed_detents_per_step);
	RUN_TEST(test_the_ring_timings_all_parse);
	RUN_TEST(test_the_new_ring_timings_default);
	RUN_TEST(test_the_menu_array_parses);
	RUN_TEST(test_a_menu_entry_naming_nothing_real_is_skipped);
	RUN_TEST(test_a_menu_cannot_offer_more_than_the_setting_allows);
	RUN_TEST(test_a_config_with_no_menu_has_an_empty_one);
	// The writer.
	RUN_TEST(test_saving_keeps_a_key_the_parser_never_heard_of);
	RUN_TEST(test_saving_changes_only_what_it_was_told_to);
	RUN_TEST(test_saving_creates_a_setting_and_a_block_that_were_absent);
	RUN_TEST(test_saving_clamps_to_the_field_range);
	RUN_TEST(test_saving_refuses_what_it_cannot_do);
	RUN_TEST(test_the_lcd_clamp_is_reported);
	RUN_TEST(test_config_paths_resolve_across_blocks);
	RUN_TEST(test_only_the_strip_length_needs_a_reboot);
	RUN_TEST(test_brightness_defaults_to_unchanged);
	RUN_TEST(test_brightness_cannot_exceed_the_supply);
	RUN_TEST(test_out_of_range_is_clamped_and_reported);
	RUN_TEST(test_a_partial_led_block_leaves_everything_else_alone);
	RUN_TEST(test_total_leds_cannot_exceed_what_was_built_for);
	RUN_TEST(test_total_leds_comes_from_the_config);
	RUN_TEST(test_a_config_with_no_led_block_gets_the_defaults);
	return UNITY_END();
}