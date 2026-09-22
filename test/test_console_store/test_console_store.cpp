// Host-side Unity tests for the functional-core config-store additions
// to lib/ConsoleConfig. These cover the two new free functions:
//   - validateConsoleConfigJson(json, out)
//   - rotateBackupBlobs(live, backup1, new_payload)
// The shell-side LittleFS wrappers (src/consoleconfig_store.{h,cpp}) are
// not exercised here -- they're thin pass-throughs and the underlying
// policy lives in the pure core so we can unit-test it without a FS.
//
// All tests assume ArduinoJson v7 syntax (the rest of the repo does too).

#include <ConsoleConfig.h>
#include <unity.h>

#include <string>

using retroroom_core::BackupRotation;
using retroroom_core::LoadResult;
using retroroom_core::validateConsoleConfigJson;
using retroroom_core::rotateBackupBlobs;

void setUp(void) {}
void tearDown(void) {}

// ---------- validateConsoleConfigJson ----------

void test_validate_accepts_example1(void) {
	// example1.json is the same shape as the embedded default. Should
	// validate cleanly: 3 irCodes, 3 consoleNames, 3 consoles.
	const char* json =
		R"({
            "irCodes": {
                "SVideo": "0x030",
                "Front": "0x830",
                "Video": "0x430",
                "YUV":   "0xE30"
            },
            "consoleNames": {
                "NES":  "Nintendo Entertainment System",
                "SNES": "Super Nintendo Entertainment System",
                "GEN":  "Sega Genesis"
            },
            "consoles": [
                {"id": "NES",  "tvInput": "Video", "selectorPosition": 1, "ledPosition": 1,  "ledWidth": 1},
                {"id": "SNES", "tvInput": "YUV",   "selectorPosition": 2, "ledPosition": 7,  "ledWidth": 5},
                {"id": "GEN",  "tvInput": "YUV",   "selectorPosition": 3, "ledPosition": 13, "ledWidth": 5}
            ]
        })";
	LoadResult result;
	TEST_ASSERT_TRUE_MESSAGE(validateConsoleConfigJson(json, result),
	                         result.error.c_str());
	TEST_ASSERT_EQUAL(4, result.irCodes.size());
	TEST_ASSERT_EQUAL(3, result.consoles.size());
}

void test_validate_accepts_empty_config(void) {
	// empty.json -- zero irCodes, zero consoles. The core parser
	// accepts this (the structure is well-formed, the maps are just
	// empty). The shell decides what to do with a zero-console world
	// (we currently boot into it and the operator sees a "no consoles
	// loaded" log line).
	const char* json =
		R"({
            "irCodes": {},
            "consoleNames": {},
            "consoles": []
        })";
	LoadResult result;
	TEST_ASSERT_TRUE_MESSAGE(validateConsoleConfigJson(json, result),
	                         result.error.c_str());
	TEST_ASSERT_EQUAL(0, result.irCodes.size());
	TEST_ASSERT_EQUAL(0, result.consoles.size());
}

void test_validate_rejects_malformed_json(void) {
	const char* json = "{ this is not json";
	LoadResult result;
	TEST_ASSERT_FALSE(validateConsoleConfigJson(json, result));
	TEST_ASSERT_FALSE(result.error.empty());
}

void test_validate_rejects_missing_ir_codes(void) {
	const char* json =
		R"({
            "consoleNames": {"NES": "Nintendo Entertainment System"},
            "consoles": [{"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 1, "ledWidth": 1}]
        })";
	LoadResult result;
	TEST_ASSERT_FALSE(validateConsoleConfigJson(json, result));
	TEST_ASSERT_FALSE(result.error.empty());
}

void test_validate_rejects_unknown_tv_input(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "0x430"},
            "consoles": [{"id": "NES", "tvInput": "HDMI", "selectorPosition": 1, "ledPosition": 1, "ledWidth": 1}]
        })";
	LoadResult result;
	TEST_ASSERT_FALSE(validateConsoleConfigJson(json, result));
	TEST_ASSERT_FALSE(result.error.empty());
}

void test_validate_rejects_bad_hex(void) {
	const char* json =
		R"({
            "irCodes": {"Video": "not-hex"},
            "consoles": [{"id": "NES", "tvInput": "Video", "selectorPosition": 1, "ledPosition": 1, "ledWidth": 1}]
        })";
	LoadResult result;
	TEST_ASSERT_FALSE(validateConsoleConfigJson(json, result));
	TEST_ASSERT_FALSE(result.error.empty());
}

// ---------- rotateBackupBlobs ----------

void test_rotate_fresh_fs_commits_payload_only(void) {
	// On a brand-new device both prior slots are empty (file missing).
	// After rotation, only the live slot has data; the backups stay
	// empty so we don't write two redundant "empty" blobs to flash.
	BackupRotation r = rotateBackupBlobs(/*live=*/"",
	                                     /*backup1=*/"",
	                                     /*new_payload=*/"v1");
	TEST_ASSERT_EQUAL_STRING("v1", r.live.c_str());
	TEST_ASSERT_EQUAL_STRING("", r.backup1.c_str());
	TEST_ASSERT_EQUAL_STRING("", r.backup2.c_str());
}

void test_rotate_first_write_creates_backup1(void) {
	// Live slot already has "v1" (no backup1 yet). After committing
	// "v2", live=v2, backup1=v1, backup2 still empty.
	BackupRotation r = rotateBackupBlobs(/*live=*/"v1",
	                                     /*backup1=*/"",
	                                     /*new_payload=*/"v2");
	TEST_ASSERT_EQUAL_STRING("v2", r.live.c_str());
	TEST_ASSERT_EQUAL_STRING("v1", r.backup1.c_str());
	TEST_ASSERT_EQUAL_STRING("", r.backup2.c_str());
}

void test_rotate_full_history_shifts_chain(void) {
	// Standard three-slot rotation. Live=v2 -> backup1, backup1=v1 ->
	// backup2, new payload "v3" -> live. The oldest backup (v0) is
	// dropped from the chain -- we only keep two backups on disk.
	BackupRotation r = rotateBackupBlobs(/*live=*/"v2",
	                                     /*backup1=*/"v1",
	                                     /*new_payload=*/"v3");
	TEST_ASSERT_EQUAL_STRING("v3", r.live.c_str());
	TEST_ASSERT_EQUAL_STRING("v2", r.backup1.c_str());
	TEST_ASSERT_EQUAL_STRING("v1", r.backup2.c_str());
}

void test_rotate_handles_empty_payload(void) {
	// The operator might POST an empty config (matching empty.json).
	// We should still rotate -- committing "no consoles" is a valid
	// state, and the previous good config is preserved in backup1.
	BackupRotation r = rotateBackupBlobs(/*live=*/"v1",
	                                     /*backup1=*/"",
	                                     /*new_payload=*/"{}");
	TEST_ASSERT_EQUAL_STRING("{}", r.live.c_str());
	TEST_ASSERT_EQUAL_STRING("v1", r.backup1.c_str());
	TEST_ASSERT_EQUAL_STRING("", r.backup2.c_str());
}

void test_rotate_does_not_inspect_payload(void) {
	// The rotation policy is intentionally content-agnostic -- even a
	// payload that wouldn't pass validateConsoleConfigJson() still
	// rotates normally. Validation is the shell's job, not the core's;
	// this function only shuffles bytes.
	BackupRotation r = rotateBackupBlobs(/*live=*/"good",
	                                     /*backup1=*/"older",
	                                     /*new_payload=*/"junk-but-rotates-fine");
	TEST_ASSERT_EQUAL_STRING("junk-but-rotates-fine", r.live.c_str());
	TEST_ASSERT_EQUAL_STRING("good", r.backup1.c_str());
	TEST_ASSERT_EQUAL_STRING("older", r.backup2.c_str());
}

// ---------- entry point ----------

int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();
	RUN_TEST(test_validate_accepts_example1);
	RUN_TEST(test_validate_accepts_empty_config);
	RUN_TEST(test_validate_rejects_malformed_json);
	RUN_TEST(test_validate_rejects_missing_ir_codes);
	RUN_TEST(test_validate_rejects_unknown_tv_input);
	RUN_TEST(test_validate_rejects_bad_hex);
	RUN_TEST(test_rotate_fresh_fs_commits_payload_only);
	RUN_TEST(test_rotate_first_write_creates_backup1);
	RUN_TEST(test_rotate_full_history_shifts_chain);
	RUN_TEST(test_rotate_handles_empty_payload);
	RUN_TEST(test_rotate_does_not_inspect_payload);
	return UNITY_END();
}
