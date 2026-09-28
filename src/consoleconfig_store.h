#ifndef RR_CONSOLECONFIG_STORE_H
#define RR_CONSOLECONFIG_STORE_H

// Shell-side persistence for the console configuration JSON.
//
// The functional core (lib/ConsoleConfig) handles validation and the
// pure backup-rotation policy; this header exposes the thin LittleFS
// I/O wrapper the network shell and the boot path call into. The split
// keeps the testable policy on the host and the I/O on the device.
//
// File layout in LittleFS (1 MB partition on Pico 2 W, see
// platformio.ini [env:pico2w] board_build.filesystem_size = 1MB):
//
//   /consoles.json       -- the live config the device boots with
//   /consoles.bak1       -- the previous live config
//   /consoles.bak2       -- the previous bak1 config
//   /lastconsole         -- decimal index of the last selected console
//
// All three slots are independent files; the rotate-on-write policy
// (rotateBackupBlobs in the core) decides what each slot holds before
// the shell opens the file handles. The shell never assumes the slots
// exist -- a missing slot is treated as an empty blob, and on a freshly
// wiped FS only the live slot is written.
//
// This TU is wrapped in #if defined(HAS_WIFI) at the call-site (network
// layer) but the storage primitives themselves don't depend on WiFi --
// they only need LittleFS to be mounted. On the wired-only Pico envs
// (pico_base, picow, pico_yd) this TU simply isn't linked because the
// network endpoint that uses it isn't compiled in.

#include <Arduino.h>
#include <string>

namespace retroroom_store {

// Read the live console config from LittleFS into `out`. Returns true
// on success (file exists, was opened, was read into `out`). On a
// missing file or read failure, returns false and leaves `out`
// untouched -- callers should fall back to the embedded PROGMEM
// default. Callers are responsible for calling LittleFS.begin() (or
// the wrapper in this header that does it for them).
bool loadLiveConsoleConfig(std::string& out);

// Read both backup slots into the provided buffers. Each buffer is
// overwritten only if the corresponding file exists; if the file is
// missing the buffer is set to an empty string. Returns true iff at
// least one slot was successfully read (so a fresh FS returns true
// with both slots empty). Used by the shell during a rotation commit
// to capture the previous state.
bool loadBackupSlots(std::string& out_backup1, std::string& out_backup2);

// Mount LittleFS if not already mounted. Returns true on success. Idempotent.
bool ensureMounted();

// Result of a save attempt. The shell uses this to decide what HTTP
// status to return to the operator.
enum class SaveResult {
	Ok,             // wrote all three slots successfully
	MountFailed,    // LittleFS.begin() returned false
	WriteFailed,    // one or more open()/write() calls failed; partial state may be on disk
};

// Commit a new console-config JSON to LittleFS. The shell is expected
// to have ALREADY validated the payload via
// retroroom_core::validateConsoleConfigJson() -- this function does no
// validation of its own; it just rotates the slots and writes bytes.
//
// Rotation policy (delegated to the core):
//   - On a fresh FS (live + bak1 empty): writes only /consoles.json.
//   - On a populated FS: rotates live -> bak1, bak1 -> bak2, writes
//     new payload to /consoles.json. bak2 is dropped.
//
// This is best-effort: on WriteFailed, the FS is in an indeterminate
// state (one or two slots committed, others not). The next boot will
// prefer /consoles.json if it parses; if it doesn't, the shell falls
// back through bak1 -> bak2 -> embedded PROGMEM. The operator can
// always re-POST to recover.
SaveResult saveConsoleConfigWithBackups(const std::string& payload);

// ---------- last-selected console (power-loss persistence) ----------
//
// A one-line decimal index into the live config's console array, kept
// in its own file so the operator's selection never shares a lifetime
// with the config that produced it. The POST /consoles.json handler
// clears it on every accepted upload (see clearLastSelectedConsole):
// a new config reorders, adds and removes entries, so a stored index
// from the previous config is meaningless against the new one.
//
// Restoring the raw stored value is the shell's job, not this
// header's -- clamp it with retroroom_core::clampIndex() against
// HowManyConsoles() before trusting it.

// Read the persisted index into `out`. Returns true if the file
// existed and was read. On a missing file, mount failure or an
// unparseable body, returns false and leaves `out` untouched -- the
// caller keeps whatever default it already had.
bool loadLastSelectedConsole(int& out);

// Persist `index` as the last-selected console. Returns true on a
// successful write. Best-effort: a failure here costs the operator
// their selection across the next power cycle, nothing more, so
// callers should log and carry on rather than abort a selection.
bool saveLastSelectedConsole(int index);

// Drop the persisted selection, so the next boot starts at console 0.
// Called when a new config is committed and on factory reset. Returns
// true if the file is absent afterwards (removed, or never existed).
bool clearLastSelectedConsole();

}  // namespace retroroom_store

#endif  // RR_CONSOLECONFIG_STORE_H
