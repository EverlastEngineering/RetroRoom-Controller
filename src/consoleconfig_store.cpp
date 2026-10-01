// Shell-side LittleFS persistence for the console config JSON.
//
// The functional core (lib/ConsoleConfig) provides:
//   - validateConsoleConfigJson(json, out)  -- "is this safe to write?"
//   - rotateBackupBlobs(live, b1, payload)  -- "what goes in each slot?"
//
// This TU only:
//   1. Mounts LittleFS (idempotently).
//   2. Reads the three file slots into std::string buffers.
//   3. Writes the rotated slots back via File.print()/File.close().
//
// The validation step happens at the call-site (src/network.cpp) so
// this TU can stay minimal and stay out of the parse-error reporting
// path. If a future caller forgets to validate, the only failure
// mode is a corrupt live config on the next boot, which falls through
// to the embedded PROGMEM default -- not catastrophic.

#include "consoleconfig_store.h"

#include <LittleFS.h>

#include <stdlib.h>

#include <ConsoleConfig.h>

// The compiled-in default. The one copy of it in the firmware, so
// "what a fresh cabinet runs" has exactly one answer.
#include "factoryconfig.h"

namespace retroroom_store {

namespace {

// File paths on LittleFS. Picked to mirror /wifi.json (the only other
// config file on the FS) so the naming is consistent: live + .bak1 +
// .bak2 is the canonical "one live + two backups" convention.
constexpr const char* kLivePath    = "/consoles.json";
constexpr const char* kBackup1Path = "/consoles.bak1";
constexpr const char* kBackup2Path = "/consoles.bak2";

// The saved WiFi credentials. This used to be a file-local constant in
// src/network.cpp, with the erase left to whichever caller happened to
// be doing a reset. It lives here because wipeEverything() has to
// remove it, and a path constant that only one of the two files that
// erase it can see is a path constant that will eventually be wrong in
// one of them.
constexpr const char* kWifiPath = "/wifi.json";

// Last-selected console index. Kept separate from the config blobs
// above because its lifetime is different: the config changes only
// when an operator POSTs a new one (which wipes this file), whereas
// this changes on every rotary detent.
constexpr const char* kLastSelectedPath = "/lastconsole";

// Read a whole file into `out`. Returns true on success; on failure
// (file missing, open() failed) returns false and leaves `out`
// untouched. The file is read in 256-byte chunks so we don't have to
// trust an FS-side size() return for the buffer allocation.
bool readFileInto(const char* path, std::string& out) {
	File f = LittleFS.open(path, "r");
	if (!f) {
		return false;
	}
	out.clear();
	// Cap at 8 KB. The example configs are < 1 KB; 8 KB is a comfortable
	// upper bound for the JSON form. Bigger payloads get truncated --
	// which the parser will reject on next boot, falling back to the
	// embedded PROGMEM. We'd rather lose a malformed upload than let
	// the device wedge on a multi-megabyte POST body.
	constexpr std::size_t kMaxBytes = 8 * 1024;
	char buf[256];
	while (f.available() && out.size() < kMaxBytes) {
		std::size_t n = f.readBytes(buf, sizeof(buf));
		if (n == 0) {
			break;
		}
		std::size_t room = kMaxBytes - out.size();
		out.append(buf, n < room ? n : room);
	}
	f.close();
	return true;
}

// Write a whole string to a file (overwrites if present). Returns true
// on success. The caller is responsible for rotation policy; this
// function just dumps bytes.
bool writeFileFrom(const char* path, const std::string& payload) {
	File f = LittleFS.open(path, "w");
	if (!f) {
		return false;
	}
	const std::size_t written = f.print(payload.c_str());
	f.close();
	// f.print() returns the byte count or 0 on failure. Arduino's Print
	// class doesn't expose a hard error code, so we treat "didn't write
	// what we asked" as a failure -- protects against an FS that
	// accepted the open() but is now read-only or full.
	return written == payload.size();
}

}  // namespace

bool ensureMounted() {
	// LittleFS.begin() returns true if the FS was already mounted.
	// Cheap to call repeatedly.
	return LittleFS.begin();
}

bool loadLiveConsoleConfig(std::string& out) {
	if (!ensureMounted()) {
		return false;
	}
	return readFileInto(kLivePath, out);
}

bool loadBackupSlots(std::string& out_backup1, std::string& out_backup2) {
	if (!ensureMounted()) {
		return false;
	}
	const bool ok1 = readFileInto(kBackup1Path, out_backup1);
	const bool ok2 = readFileInto(kBackup2Path, out_backup2);
	// Missing slots are NOT a failure -- a fresh FS has no backups,
	// and loadBackupSlots() must succeed (with empty buffers) so the
	// rotation policy still runs. The only true failure is "couldn't
	// mount".
	return true;
}

SaveResult saveConsoleConfigWithBackups(const std::string& payload) {
	if (!ensureMounted()) {
		return SaveResult::MountFailed;
	}

	// Read the prior state so the rotation policy has the inputs it
	// needs. If the read fails (truly fresh FS) we treat the prior
	// state as empty -- the rotation will still produce a sane result.
	std::string prev_live;
	std::string prev_backup1;
	std::string prev_backup2;
	readFileInto(kLivePath, prev_live);
	readFileInto(kBackup1Path, prev_backup1);
	readFileInto(kBackup2Path, prev_backup2);
	(void)prev_backup2;  // unused directly -- the rotation policy only looks at live+bak1.

	// Delegate the byte-level rotation to the functional core. The
	// core never touches the FS; the shell commits whatever the core
	// computed. This split keeps the policy unit-testable on the host.
	const retroroom_core::BackupRotation rotated =
		retroroom_core::rotateBackupBlobs(prev_live, prev_backup1, payload);

	// Write live first. If this fails the FS is in a bad way and we
	// bail before touching the backup slots -- the previous good
	// config remains on disk as /consoles.json, and the operator can
	// retry the POST. Writing backup slots before the new live would
	// risk losing the last good config if the live write failed.
	if (!writeFileFrom(kLivePath, rotated.live)) {
		return SaveResult::WriteFailed;
	}
	if (!rotated.backup1.empty() && !writeFileFrom(kBackup1Path, rotated.backup1)) {
		return SaveResult::WriteFailed;
	}
	if (!rotated.backup2.empty() && !writeFileFrom(kBackup2Path, rotated.backup2)) {
		return SaveResult::WriteFailed;
	}
	return SaveResult::Ok;
}

bool loadLastSelectedConsole(int& out) {
	if (!ensureMounted()) {
		return false;
	}
	std::string raw;
	if (!readFileInto(kLastSelectedPath, raw)) {
		return false;
	}
	// Deliberately not bounds-checked here. The stored value is only
	// meaningful against the config that wrote it, and this TU has no
	// idea how long that list is. The caller clamps with
	// retroroom_core::clampIndex(stored, HowManyConsoles()) -- the same
	// guard every other index-restoring path in the firmware uses.
	//
	// strtol on a hand-edited or truncated file yields 0, which is a
	// legal index, so a corrupt file degrades to "start at the first
	// console" rather than to a wild out-of-range read.
	out = static_cast<int>(strtol(raw.c_str(), nullptr, 10));
	return true;
}

bool saveLastSelectedConsole(int index) {
	if (!ensureMounted()) {
		return false;
	}
	const std::string payload = std::to_string(index);
	return writeFileFrom(kLastSelectedPath, payload);
}

bool loadConsoleConfigOrDefault(ConsoleConfigSource& out) {
	// A missing or unmountable filesystem is not an error here. It is
	// the "board with no filesystem partition" case, and the whole
	// point of a compiled-in default is that the cabinet still comes
	// up. The caller distinguishes the two cases with
	// `fromFlash`, not by this returning false.
	std::string fromFlash;
	if (loadLiveConsoleConfig(fromFlash) && !fromFlash.empty()) {
		out.json = std::move(fromFlash);
		out.fromFlash = true;
		return true;
	}
	out.json.assign(kFactoryConfigJson);
	out.fromFlash = false;
	return !out.json.empty();
}

bool clearLastSelectedConsole() {
	if (!ensureMounted()) {
		return false;
	}
	// LittleFS::remove() on a missing file is a no-op, so this is
	// safe to call unconditionally. The exists() re-check is what
	// turns "remove failed" into an honest return value.
	LittleFS.remove(kLastSelectedPath);
	return !LittleFS.exists(kLastSelectedPath);
}

bool wipeEverything() {
	if (!ensureMounted()) {
		return false;
	}
	// Every file the cabinet keeps state in, in one list so the set
	// cannot be half-remembered. The two backups are here because the
	// boot path prefers live -> bak1 -> bak2 -> PROGMEM: remove only
	// the live file and the next boot loads the backup, which looks
	// exactly like a reset that did nothing.
	constexpr const char* const kAll[] = {
	    kLivePath, kBackup1Path, kBackup2Path, kLastSelectedPath, kWifiPath,
	};
	for (const char* path : kAll) {
		LittleFS.remove(path);
	}
	// Verified rather than assumed. A LittleFS::remove() that failed
	// would otherwise be reported to the operator as a clean factory
	// reset, and the first thing they would find is a config that came
	// straight back.
	for (const char* path : kAll) {
		if (LittleFS.exists(path)) {
			return false;
		}
	}
	return true;
}

}  // namespace retroroom_store
