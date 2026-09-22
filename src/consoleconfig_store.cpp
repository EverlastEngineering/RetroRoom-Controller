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

#include <ConsoleConfig.h>

namespace retroroom_store {

namespace {

// File paths on LittleFS. Picked to mirror /wifi.json (the only other
// config file on the FS) so the naming is consistent: live + .bak1 +
// .bak2 is the canonical "one live + two backups" convention.
constexpr const char* kLivePath    = "/consoles.json";
constexpr const char* kBackup1Path = "/consoles.bak1";
constexpr const char* kBackup2Path = "/consoles.bak2";

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

}  // namespace retroroom_store
