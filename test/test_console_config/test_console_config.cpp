#include <ConsoleConfig.h>
#include <unity.h>

#include <string>

using retroroom_core::Console;
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

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	UNITY_BEGIN();
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
	return UNITY_END();
}