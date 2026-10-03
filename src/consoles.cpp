#include "consoles.h"

#include <ConsoleConfig.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "lighting.h"
#include "ledstring.h"
#include "controls.h"
#include "consoleconfig_store.h"
#include "display.h"
#if defined(HAS_IR)
#include "ircontrol.h"
#endif
#if defined(HAS_WIFI)
#include "network.h"
#endif
#include "stackselector.h"

// The config a cabinet ships with. The document itself is
// src/factory-config.json, pulled in through factoryconfig.h by the
// preprocessor; see that header for why it is a file and not a
// literal. (The legacy approach was a hand-rolled addConsole(...)
// sequence in main.cpp, and before that a 1.5 KB string literal in this
// file, which was unreadable in a diff and impossible to lint.)
//
// At boot, consoleDefinitions() prefers /consoles.json from LittleFS
// (the slot maintained by POST /consoles.json and the serial
// PUT CONFIG). This is the fallback used when:
//   - the FS isn't mounted (board with no filesystem partition),
//   - /consoles.json is missing (factory-fresh device, or one that has
//     been RESET),
//   - or /consoles.json fails to parse (corrupt or partial write).
//
// Keeping a default here means the device never wedges at boot just
// because no one has configured it yet: it comes up with three
// consoles, a full `led` block, and a `menu` array with a row for
// every setting in the registry. That last part is the load-bearing
// one -- it is what makes `network.disable` reachable by the knob on a
// cabinet nobody has ever put a config on, and therefore what stops
// that switch from being the permanent brick it was the commit before
// last.
//
// It is deliberately NOT written to flash on the first boot. See
// consoleConfigIsUploaded() for why the absence of /consoles.json is
// the flag and saving the default would destroy it.
//
// Nothing below includes this file directly. The three places that
// need "the config to run" -- this file's boot path, configSave(),
// and the serial channel's GET CONFIG -- ask the store instead
// (retroroom_store::loadConsoleConfigOrDefault), so there is one
// answer to that question rather than three that can disagree.
#include "factoryconfig.h"

int currentConsoleIndex = 0;
// The console the operator is *looking at*, which is not the same as
// currentConsoleIndex until they commit with the rotary click. Moved by
// rotaryEncoderTick() on each detent; reverted to currentConsoleIndex
// when the ring gives up. Kept in lockstep with currentConsoleIndex
// whenever the selection changes by any other route (next/prev buttons,
// /next, /prev, the WS commands, the post-boot restore).
int browsedConsoleIndex = 0;

// Debounced LittleFS save state. Each commit through selectConsole()
// stamps pendingSaveDueMs = millis() + kSaveQuietMs and remembers the
// index it wanted saved in pendingSaveIndex. consoles_loop() flushes
// the write once the deadline passes with no further requests, so
// back-to-back rotary clicks coalesce into a single write+sync instead
// of paying one per click. The write still survives a power loss --
// worst case the operator loses the last ~kSaveQuietMs of changes.
//
// kSaveQuietMs is the "quiet window" after the last click before we
// actually touch the FS. 1500 ms is large enough that a normal
// click-click-pause click sequence is one write, and small enough that
// a single click persists well before the operator moves on.
namespace {
constexpr uint32_t kSaveQuietMs = 1500;
uint32_t pendingSaveDueMs = 0;
int pendingSaveIndex = 0;
}  // namespace
// millis() at the moment we last decided on the current console
// (i.e. immediately after stepWithin() resolves in advanceConsole()
// / rewindConsole()). Used by GET /state.json to surface
// `selectedAtUptimeMs` so the e2e harness can verify the device
// actually moved (delta changes) without trusting response codes.
// RAM-only; resets to 0 on every boot (RP2350 .bss is zeroed by crt0
// on every boot, warm or cold -- same lifetime as currentConsoleIndex).
uint32_t currentConsoleSelectedAtMs = 0;
uint32_t lcdBacklightOffAfterMs = 30000;  // default; overwritten by consoleDefinitions()
// Whether a failed WiFi join is announced on the LCD before the
// SoftAP comes up. Default true; overwritten by consoleDefinitions()
// from `network.showWIFIConnectionFailureMessage`. Read by
// src/network.cpp at the moment the join resolves, which is why it is
// a plain global rather than something the network stack pulls from
// the config itself.
bool showWifiConnectionFailureMessage = true;
// Whether the radio is left off entirely. Read by src/network.cpp
// before it touches the radio, and by src/state.cpp to keep the
// on-board LED and the heartbeat from reporting a network problem
// that the operator asked for.
bool networkDisabled = false;

// Night mode. See the declaration in this header for why this is not
// a brightness override -- it used to be one, and scaling the whole
// strip to zero also scaled away the browse and the commit animation,
// which are the parts of the cabinet you need to see when you are
// using it.
static bool sNightMode = false;

bool nightMode() {
	return sNightMode;
}

void setNightMode(bool on) {
	sNightMode = on;
}

std::vector<Console> consoles;
// The shelf extents from the config's optional `shelves` block. Empty
// is the normal case and is not an error.
std::vector<Shelf> shelfBounds;
// Defaults first, so anything that reads this before the config is
// loaded gets the same numbers the config would have given it.
retroroom_core::LedFeel ledFeel = retroroom_core::defaultLedFeel();

// Whether the console list now running came off flash, or is the
// built-in PROGMEM default nobody has chosen yet. Set by
// consoleDefinitions(); read through consoleConfigIsUploaded() so the
// reason it is not simply "a file exists" lives with the accessor.
static bool consoleConfigUploaded = false;

// The menu the shell actually shows: whatever the config declared,
// then the actions. Built once, after the config lands, because the
// action rows are fixed strings and the settings' labels and keys are
// owned by menuDef -- which a later load can re-assign.
//
// The actions are not config items. Saving and restarting are not
// things an operator should be able to delete from their own menu, and
// appending them here is what means a config declaring no items still
// yields a menu with a way out of it. That is also what makes an empty
// `menu` array a lock-down rather than a trap; see
// todo/open/2026-09-30_menu-lock-down-mode.md.
//
// Save and Reboot sit together because both are things the system does
// rather than settings; Go Back is last because that is where a menu is
// expected to put it.
static retroroom_core::MenuItem shellMenuItems[retroroom_core::kMaxMenuItems + 3];
static int shellMenuCount = 0;

// The operator's menu, parsed from the config's `menu` array. Owns its
// strings; CabinetMenu() hands out a non-owning view of them, which is
// why the view must be fetched fresh rather than cached -- the strings
// move if this is ever re-assigned.
static retroroom_core::ParsedMenu menuDef;

static void buildShellMenu() {
        shellMenuCount = 0;
        // Counted separately from shellMenuCount so the overflow message
        // below is about how many rows the operator *asked* to see.
        // Counting every parsed row would make a config that hides half
        // its settings report itself as overflowing a screen it fits on.
        int visible = 0;
        for (size_t i = 0; i < menuDef.items.size(); ++i) {
                // `visible: false` is a statement about a 16x2 screen,
                // not about whether the setting exists. The row is
                // still in /consoles.json and still editable there --
                // it just does not take up one of the forty slots.
                if (!menuDef.items[i].visible) {
                        continue;
                }
                ++visible;
                if (shellMenuCount < retroroom_core::kMaxMenuItems) {
                        shellMenuItems[shellMenuCount++] = menuDef.items[i];
                }
        }
        // Say so when rows were left off. The cap exists to bound a
        // fixed array, and the array bound means a config declaring
        // more than the cap gets the first N and no indication -- a
        // setting that is in the file, adjustable in principle, and
        // simply not there. That is exactly the kind of silence this
        // repo has spent a session removing elsewhere. Hiding rows is
        // the cure, so say that too.
        if (visible > retroroom_core::kMaxMenuItems) {
                Serial.print("menu: config shows ");
                Serial.print(visible);
                Serial.print(" items, only ");
                Serial.print(retroroom_core::kMaxMenuItems);
                Serial.println(" fit; the rest were dropped. Set \"visible\": false on rows you do not want on the screen.");
        }
        struct {
                const char* label;
                retroroom_core::MenuAction action;
        } const actions[] = {
                {"Save", retroroom_core::MenuAction::SAVE},
                {"Reboot", retroroom_core::MenuAction::REBOOT},
                {retroroom_core::kMenuGoBackLabel,
                 retroroom_core::MenuAction::GO_BACK},
        };
        for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
                retroroom_core::MenuItem& item = shellMenuItems[shellMenuCount++];
                item.label = actions[i].label;
                item.key = nullptr;
                item.action = actions[i].action;
                item.isBool = false;
                item.lo = 0;
                item.hi = 0;
                item.step = 1;
                // Explicit, though nothing reads it for an action row.
                // The array is static, so a slot a settings row never
                // reached is zero here, and "Save" would carry
                // visible == false -- which happens to be true today and
                // is exactly the kind of accident that is true until
                // someone filters on it.
                item.visible = true;
                item.preview = false;
        }
}

const retroroom_core::Menu CabinetMenu() {
        retroroom_core::Menu m;
        m.items = shellMenuItems;
        m.count = shellMenuCount;
        return m;
}

bool consoleConfigIsUploaded() {
        return consoleConfigUploaded;
}

// ---- applying and saving settings at runtime -----------------------------
//
// Two operations, kept apart on purpose: apply() changes what the
// cabinet is doing, save() changes what is on flash. The menu and the
// API are both clients of these two and neither has its own idea of
// what "save" means.

namespace {

// What has been changed since the last save.
//
// A list rather than a pair of flags, because the writer needs to know
// *what* to write. The two questions the prompt asks -- is anything
// unsaved, and does anything need a restart -- are derived from it, so
// there is one structure and no way for the menu and the API to
// disagree about the answer.
//
// Bounded, and reports a drop rather than silently forgetting: a set
// that grew without limit would be a slow way to run out of RAM, and a
// setting that quietly stopped saving is worse than one that says it
// could not.
constexpr int kMaxPending = 32;
struct PendingChange {
        const retroroom_core::LedField* field;
        const char* block;
        int value;
};
PendingChange pending[kMaxPending];
int pendingCount = 0;
bool pendingOverflow = false;

// Clamp to the field's declared range, exactly as the parser would.
// The blocks that are not bound to a LedFeel have no set-by-key helper
// to do this for them, and the alternative -- each branch remembering
// its own bounds -- is how the writer and the parser end up disagreeing
// about what a value means.
int clampToField(const retroroom_core::LedField* field, int value) {
        int next = value;
        if (next < field->lo) next = field->lo;
        if (next > field->hi) next = field->hi;
        return next;
}

// "<block>.<field>" for a resolved field.
//
// The long form is what the menu stores and what the writer consumes,
// because a bare key resolves to the *first* block -- so writing back
// "backlightOffAfterMs" would put an LCD setting inside `led`. The
// buffer is the caller's; the longest path in the registry is far
// shorter than this, and the function refuses rather than truncates if
// that ever stops being true.
const char* fieldPath(const retroroom_core::LedField* field, const char* block,
                      char* buf, std::size_t len) {
        if (field == nullptr || block == nullptr) {
                return nullptr;
        }
        int n = snprintf(buf, len, "%s.%s", block, field->key);
        if (n < 0 || static_cast<std::size_t>(n) >= len) {
                return nullptr;
        }
        return buf;
}

}  // namespace

bool applyConfigValue(const char* path, int value) {
        if (path == nullptr) {
                return false;
        }
        const char* block = nullptr;
        const retroroom_core::LedField* field =
                retroroom_core::findConfigField(path, &block);
        if (field == nullptr || block == nullptr) {
                return false;
        }
        // Storage is per block: only `led` is bound to a LedFeel. A
        // second block is a second `if` here and nothing anywhere else.
        if (std::strcmp(block, "led") == 0) {
                bool clamped = false;
                if (!retroroom_core::ledFeelSet(&ledFeel, field->key, value, &clamped)) {
                        return false;
                }
        } else if (std::strcmp(block, "lcd") == 0) {
                lcdBacklightOffAfterMs =
                        static_cast<uint32_t>(clampToField(field, value));
        } else if (std::strcmp(block, "network") == 0) {
                int next = clampToField(field, value);
                if (std::strcmp(field->key, "disable") == 0) {
                        networkDisabled = next != 0;
                } else {
                        showWifiConnectionFailureMessage = next != 0;
                }
        } else {
                return false;
        }

        // Record it for save. Matching on the field rather than the
        // path means changing the same setting twice keeps one entry at
        // the latest value, so a set of edits is a set of *final*
        // values and never a replay.
        //
        // The recorded value is read back through configValueOf()
        // rather than being the value that was asked for: a setting
        // clamped on the way in has to be recorded clamped, or the
        // pending set and the running cabinet disagree about what the
        // operator is looking at until they save.
        char full[64];
        const char* fullPath = fieldPath(field, block, full, sizeof(full));
        if (fullPath == nullptr) {
                return false;
        }
        for (int i = 0; i < pendingCount; ++i) {
                if (pending[i].field == field) {
                        pending[i].value = configValueOf(fullPath, value);
                        return true;
                }
        }
        if (pendingCount >= kMaxPending) {
                pendingOverflow = true;
                return true;  // it still applies; it just may not save
        }
        pending[pendingCount].field = field;
        pending[pendingCount].block = block;
        pending[pendingCount].value = configValueOf(fullPath, value);
        ++pendingCount;
        return true;
}

int configValueOf(const char* path, int fallback) {
        const char* block = nullptr;
        const retroroom_core::LedField* field =
                retroroom_core::findConfigField(path, &block);
        if (field == nullptr) {
                return fallback;
        }
        if (block == nullptr) {
                return fallback;
        }
        if (std::strcmp(block, "lcd") == 0) {
                return static_cast<int>(lcdBacklightOffAfterMs);
        }
        if (std::strcmp(block, "network") == 0) {
                const char* key = field->key;
                if (std::strcmp(key, "disable") == 0) {
                        return networkDisabled ? 1 : 0;
                }
                return showWifiConnectionFailureMessage ? 1 : 0;
        }
        return retroroom_core::ledFeelGet(ledFeel, field->key, fallback);
}

bool configHasUnsavedChanges() {
        return pendingCount > 0;
}

bool configNeedsReboot() {
        for (int i = 0; i < pendingCount; ++i) {
                if (retroroom_core::configFieldNeedsReboot(pending[i].field)) {
                        return true;
                }
        }
        return false;
}

int configPendingCount() {
        return pendingCount;
}

const char* configPendingPath(int i) {
        static char buf[64];
        if (i < 0 || i >= pendingCount) {
                return nullptr;
        }
        // The long form, "<block>.<field>", for the same reason the
        // menu stores it that way: a bare key is ambiguous about which
        // block it belongs to, and the one thing an API caller does
        // with a path is send it back.
        const char* path = fieldPath(pending[i].field, pending[i].block, buf, sizeof(buf));
        return path;
}

int configPendingValue(int i) {
        if (i < 0 || i >= pendingCount) {
                return 0;
        }
        return pending[i].value;
}

retroroom_store::SaveResult configSave() {
        if (pendingCount == 0) {
                return retroroom_store::SaveResult::Ok;  // nothing to do is success
        }
        retroroom_core::ConfigEdit edits[kMaxPending];
        // One buffer per edit because ConfigEdit holds a `const char*`
        // and applyConfigEdits() reads them all at once. A std::string
        // array would heap; a single scratch buffer would leave every
        // entry pointing at the last one written.
        char paths[kMaxPending][64];
        for (int i = 0; i < pendingCount; ++i) {
                const char* path = fieldPath(pending[i].field, pending[i].block,
                                             paths[i], sizeof(paths[i]));
                if (path == nullptr) {
                        // Cannot happen with the registry as it stands;
                        // if it ever does, the honest answer is to not
                        // save rather than to save a truncated path.
                        return retroroom_store::SaveResult::WriteFailed;
                }
                edits[i].path = path;
                edits[i].value = pending[i].value;
        }
        // Whatever the cabinet is running from: the file if there is
        // one, the built-in default otherwise. That is what makes Save
        // work on a device that has never been configured, and it saves
        // the exact document the cabinet booted from rather than a
        // reconstruction of it. Same source the boot path used, so Save
        // cannot edit a different document from the one running.
        retroroom_store::ConsoleConfigSource cfg;
        if (!retroroom_store::loadConsoleConfigOrDefault(cfg)) {
                Serial.println("save refused: no config to edit");
                return retroroom_store::SaveResult::WriteFailed;
        }
        std::string result;
        std::string error;
        if (!retroroom_core::applyConfigEdits(cfg.json, edits, pendingCount, &result, &error)) {
                // A refusal, not a write. Nothing on flash changed and
                // the running cabinet is exactly as it was, which is the
                // entire point of checking before writing.
                Serial.print("save refused: ");
                Serial.println(error.c_str());
                return retroroom_store::SaveResult::WriteFailed;
        }
        const retroroom_store::SaveResult r =
                retroroom_store::saveConsoleConfigWithBackups(result);
        if (r == retroroom_store::SaveResult::Ok) {
                pendingCount = 0;
                pendingOverflow = false;
        }
        return r;
}

void addConsole(const Console& console) {
	consoles.push_back(console);
}

int HowManyConsoles() {
	return static_cast<int>(consoles.size());
}

const Console& CurrentConsole() {
	return consoles[currentConsoleIndex];
}

const Console& BrowsedConsole() {
	return consoles[browsedConsoleIndex];
}

void restoreLastSelectedConsole() {
	// The console list has to be populated before this is worth
	// calling: the stored value is an index into it.
	int stored = 0;
	if (retroroom_store::loadLastSelectedConsole(stored)) {
		// Clamp rather than trust: the stored index refers to the
		// config that wrote it, and a hand-edited /lastconsole is one
		// bad boot away from being out of range. CurrentConsole()
		// indexes without a bounds check, so this is load-bearing.
		// clampIndex() also folds the empty-list case to 0, which is
		// already the default cursor position.
		const int clamped = retroroom_core::clampIndex(stored, HowManyConsoles());
		Serial.print("consoles: restored selection index=");
		Serial.print(clamped);
		if (clamped != stored) {
			Serial.print(" (clamped from ");
			Serial.print(stored);
			Serial.print(")");
		}
		Serial.println();
		// Just the cursor. The rest of setup() reads
		// currentConsoleIndex as it goes -- ledstring_setConsole()
		// lights the right window, the LCD seeds its live lines from
		// CurrentConsole() at the startup->welcome handover -- so the
		// console is booted into rather than selected after the fact.
		currentConsoleIndex = clamped;
		// The browsed cursor starts wherever the selection did, so a
		// detent immediately after boot browses relative to the live
		// console rather than to console 0.
		browsedConsoleIndex = clamped;
	} else {
		// No file, no filesystem, or a mount failure. The cursor stays
		// where setup() left it, but the latch below is still driven:
		// its physical position is unknown after a power cycle whether
		// or not we remembered a selection.
		Serial.println("consoles: no saved selection");
	}

	// Step the latch to whichever console we ended up on.
	//
	// This is the one case where the cursor and the hardware genuinely
	// disagree after a reboot: the latch shares a power rail with the
	// controller, so it does not hold position across a power cycle,
	// while the cursor is just a number we remembered. That is why the
	// drive below is unconditional rather than tied to whether the
	// cursor actually moved -- a device that saved index 0 and never
	// moved still has an arm sitting somewhere unknown.
	//
	// selectStack() counts a *relative* number of pulses from wherever
	// the arm sits, so it is only meaningful once setup() has homed the
	// latch. The two calls must stay in that order.
	//
	// Deliberately selectStack() and not selectConsole(): the LCD
	// paint inside selectConsole() is exactly what would cut the
	// welcome splash short. The IR code is left alone here too.
	if (HowManyConsoles() > 0) {
		selectStack(CurrentConsole().selector_position);
	}
}

void consoleDefinitions() {
	// Functional-core load: pure parse + validation, no Arduino headers in
	// the parse path. The returned LoadResult has ok=false + a typed error
	// string on any structural problem (missing irCodes, missing consoleNames,
	// unknown tvInput reference, etc.).
	//
	// Source selection:
	//   1. Try /consoles.json from LittleFS (the live slot maintained by
	//      POST /consoles.json in src/network.cpp). If the FS isn't mounted
	//      (boards without a filesystem partition) or the file is missing
	//      (factory-fresh device), fall through silently.
	//   2. Fall back to the embedded PROGMEM CONFIG_JSON literal above.
	//      This means the device always boots with *something* -- the
	//      example 3-console config -- even on a fresh device or a board
	//      with no FS at all.
	// The config, from flash or failing that the built-in default.
	// Routed through the store so the boot path and the serial
	// channel's GET CONFIG cannot answer "what is this running"
	// differently -- and so the "has anyone configured this" flag
	// below comes from the same answer rather than from a second
	// guess about the same file.
	retroroom_store::ConsoleConfigSource cfg;
	if (!retroroom_store::loadConsoleConfigOrDefault(cfg)) {
		Serial.println("Console config load failed (no config and no built-in default)");
		return;
	}
	const std::string& source = cfg.json;
	const std::string source_label =
	    cfg.fromFlash ? "LittleFS /consoles.json"
	                  : "PROGMEM default (CONFIG_JSON)";

	retroroom_core::LoadResult result =
		retroroom_core::loadFromJson(source.data(), source.size());
	if (!result.ok) {
		Serial.print("Console config load failed (");
		Serial.print(source_label.c_str());
		Serial.print("): ");
		Serial.println(result.error.c_str());
		return;
	}
	// The strip's feel, and every clamp the parser had to apply. The
	// warnings are printed rather than swallowed: a value in the file
	// that is not the value on the strip is invisible otherwise, and
	// that is exactly how a console sat clamped to a third of its
	// declared width without anybody noticing.
	ledFeel = result.feel;
	menuDef = result.menu;
	buildShellMenu();
	// The "not set up yet" pages are keyed off this, so it is set from
	// the same answer the document came from rather than from a second
	// look at the same file.
	//
	// It is deliberately *not* "a file was read off flash". A
	// half-written /consoles.json on a cabinet nobody had configured
	// would report itself as the operator's and suppress the notice
	// that would have told them to fix it. A file nobody can parse is
	// not an upload, it is a problem, and the "not set up yet" pages
	// are the right thing to say about it.
	consoleConfigUploaded = cfg.fromFlash;
	Serial.print("Menu items: ");
	Serial.println(menuDef.items.size());
	for (const std::string& w : result.warnings) {
		Serial.print("console config: ");
		Serial.println(w.c_str());
	}
	Serial.print("LED string: ");
	Serial.print(ledFeel.totalLeds);
	Serial.println(" LEDs");

	lcdBacklightOffAfterMs = result.lcdBacklightOffAfterMs;  // RAM-only; loaded per boot
	// Same lifetime, same reason. The network stack reads this when a
	// WiFi join resolves, which is after the config is loaded but well
	// after the stack is brought up -- which is exactly why it is a
	// global the stack reads rather than something it parses.
	showWifiConnectionFailureMessage = result.showWifiConnectionFailureMessage;
	networkDisabled = result.networkDisabled;  // same lifetime, same reason
	for (const auto& c : result.consoles) {
		addConsole(c);
	}
	// The declared shelf extents, if the config has any. Empty is
	// normal and means "derive them from the consoles", so nothing has
	// to branch on it here.
	shelfBounds = result.shelves;
	for (const auto& s : shelfBounds) {
		Serial.print("Shelf ");
		Serial.print(s.id);
		Serial.print(" occupies LED ");
		Serial.print(s.fromLed);
		Serial.print("..");
		Serial.println(s.toLed);
	}
	Serial.print("Loaded ");
	Serial.print(result.consoles.size());
	Serial.print(" consoles from ");
	Serial.print(source_label.c_str());
	Serial.print("; LCD backlight off after ");
	Serial.print(lcdBacklightOffAfterMs);
	for (const auto& c : consoles) {
		Serial.print(" [");
		Serial.print(c.id.c_str());
		Serial.print(" -> ");
		Serial.print(c.name.c_str());
		Serial.print(" sel=");
		Serial.print(c.selector_position);
		Serial.print(" tvInput=0x");
		Serial.print(c.tvinput, HEX);
		Serial.print(" led=");
		Serial.print(c.led_position);
		Serial.print("..");
		// The *clamped* end, not led_position + led_width - 1. A console
		// whose declared window runs off the end of the strip is a
		// config error, and printing the declared end hides it -- the
		// log would say 55..93 on a 64-LED strip and read as though
		// the console really were that wide.
		//
		// Clamped against the *configured* length, not the build
		// capacity. The capacity is 512 and the default totalLeds is the
		// capacity, so this reports a window as fine unless the
		// operator has said the string is shorter -- which is the whole
		// point of being able to.
		const int clampedWidth = (c.led_position + c.led_width > ledFeel.totalLeds)
									? (ledFeel.totalLeds - c.led_position)
									: c.led_width;
		Serial.print(c.led_position + clampedWidth - 1);
		if (clampedWidth != c.led_width) {
			Serial.print(" (declared width ");
			Serial.print(c.led_width);
			Serial.print(" runs off the strip)");
		}
		Serial.print("]");
	}
	Serial.println();
	Serial.print("LCD backlight off after ");
	Serial.print(lcdBacklightOffAfterMs);
	Serial.println(" ms");
}

void selectConsole(const Console& c) {
	// Drives the StackSelector AND blasts the IR code. Order matters:
	// the StackSelector takes a few ms to settle the ARM/CYCLE/ENABLE
	// state machine; blasting IR in parallel is fine (one-shot send).
	Serial.print("Select Console index=");
	Serial.print(currentConsoleIndex);
	Serial.print(": ");
	Serial.print(c.name.c_str());
	Serial.print(" selector=");
	Serial.print(c.selector_position);
	Serial.print(" tvInput=0x");
	Serial.print(c.tvinput, HEX);
	selectStack(c.selector_position);
#if defined(HAS_IR)
	setInput(c.tvinput);
#endif
	// LCD update + backlight wake. The display_* calls are inline
	// no-ops when HAS_LCD is undefined (see display.h).
	display_show_console(c.name.c_str(), c.tagline.c_str());
	display_wake();
	// The commit also ends the browse: forget any half-accumulated
	// detents and drop the blob, so the operator's next turn starts a
	// fresh step from the console that is now live. Deliberately
	// outside the HAS_LEDS guard: the detent gate lives in
	// controls.cpp regardless of whether the second strip is wired, and
	// a commit that forgot to clear it would leave the next turn
	// starting a step from the wrong place.
	//
	// ORDER MATTERS, and getting it backwards is why the selection
	// effect never appeared to run at all. ledstring_browseClear()
	// cancels whatever the strip is animating, so calling this *after*
	// starting the effect put the strip straight back to RESTING on the
	// next statement: the effect painted its first frame and nothing
	// ever advanced it. The twinkle was not weak, or the wrong colour,
	// or too short -- it was one frame, and no value of
	// LEDSTRING_SELECT_TWINKLE_MS could have shown it. The symptom was
	// a flicker or two at the moment of the commit, which is exactly
	// what the two competing paint calls leave behind.
	controls_browseReset();
#if defined(HAS_LEDS)
	// Play the selection effect on the second strip (GP21): the whole
	// string twinkles, then collapses to the pixels above the selected
	// console. This one insertion point covers every commit path --
	// rotary press, NEXT_CONSOLE_BTN, PREV_CONSOLE_BTN, HTTP /next,
	// /prev, and the WebSocket "console" message -- because they all
	// funnel through selectConsole() on the way to driving the
	// StackSelector.
	//
	// The effect is non-blocking: it hands the strip to a state machine
	// that ledstring_loop() advances from loop(), so this commit does
	// not sit waiting ~LEDSTRING_SELECT_EFFECT_MS before returning.
	//
	// After the browse reset, never before it -- see the note there.
	ledstring_selectEffect(currentConsoleIndex);
#endif

	// Remember the selection so it survives a power cycle.
	// selectConsole() is the single commit point for every selection
	// path (rotary press, NEXT/PREV buttons, /next, /prev, the WS
	// commands and the post-boot restore), so saving here covers them
	// all with no per-path bookkeeping.
	//
	// Best-effort by design: a failed write costs the selection across
	// the next power cycle and nothing more, so it must never take
	// down a selection that otherwise worked. The rotary *detent*
	// handler in src/controls.cpp moves currentConsoleIndex without
	// committing, which is right -- it only re-lights the ring, and
	// the press is what actually selects.
	//
	// DEBOUNCED: the actual LittleFS write is performed by
	// consoles_loop() after kSaveQuietMs of silence. Without this
	// every click would block for the LittleFS write+sync (on Pico
	// on-flash FS that's ~1 s per click), which made the rotary feel
	// unresponsive. Worst case now: the operator loses the last
	// ~kSaveQuietMs of changes to a power loss, not a per-click stall.
	pendingSaveIndex = currentConsoleIndex;
	pendingSaveDueMs = millis() + kSaveQuietMs;

	// A commit ends the interaction, so the ring strikes once and then
	// goes dark rather than holding for the rest of the idle timeout.
	// The rotary-click path additionally painted the ring on its way in
	// (ringLEDNext / ringLEDPrevious); this is the matching off-switch,
	// and it is why spinning the knob and then clicking settles dark.
	// The strike is the one moment the operator is definitely looking at
	// the knob, so it is where the confirmation belongs. Set
	// RING_SELECT_FLASH_MS to 0 to compile it out.
	//
	// Placed last so that everything above -- including the LED string
	// selection effect, which is a different strip and is NOT affected
	// -- has run.
#if defined(HAS_LEDS)
	lightRingSelectStrike();
#endif
}

void advanceConsole() {
	// Step forward one console, stopping at the end of the list. Safe
	// on an empty vector.
	//
	// NOT wrap-around, whatever the older version of this comment said.
	// It stopped wrapping when the browse did, and the docstring above
	// this function and the OpenAPI summary for /next both kept claiming
	// otherwise for several commits -- so an API test written against
	// the documentation failed against a cabinet that was merely sitting
	// on its last console. If that is what you are reading after a
	// "why does /next do nothing" report, this is the answer.
	//
	// The USR button on the YD-RP2040 still advances forward on every
	// press, and still stops at the end.
	int n = HowManyConsoles();
	if (n <= 0) {
		Serial.println("advanceConsole: no consoles loaded");
		return;
	}
	// The list has physical ends. Turning past the last console (or back
	// before the first) reaches nothing, so this is a no-op: no latch
	// step, no IR, no broadcast, and no selection stamp. The stamp
	// matters -- the e2e harness asserts on `selectedAtUptimeMs`
	// changing, so writing it on a no-op would make a device that is
	// stuck at the end of the list look alive.
	const int before = currentConsoleIndex;
	currentConsoleIndex = retroroom_core::stepWithin(currentConsoleIndex, n, +1);
	if (currentConsoleIndex == before) {
		Serial.println("Button: advance -- at end of list");
		return;
	}
	// A commit moves the browsed cursor with it, so the next detent
	// browses from the console that is now live.
	browsedConsoleIndex = currentConsoleIndex;
	// Stamp the selection time at-the-moment-of-decision (after
	// stepWithin but before any side effects / WS broadcast) so the
	// e2e harness can read `selectedAtUptimeMs` from /state.json and
	// assert the device actually moved (delta changes across calls).
	currentConsoleSelectedAtMs = millis();
	Serial.print("Button: advance -> index ");
	Serial.println(currentConsoleIndex);
	const Console& c = CurrentConsole();
	selectConsole(c);
#if defined(HAS_WIFI)
	// Mirror the change to any connected web UI over WebSocket so the
	// page doesn't need to poll /state.json to stay in sync.
	{
		std::string msg = "console:";
		msg += c.name;
		msg += ":";
		msg += std::to_string(currentConsoleIndex);
		broadcastSocketMessage(msg);
	}
#endif
}

void rewindConsole() {
	// Step back one console, stopping at the start of the list.
	// Counterpart of advanceConsole(); see the note there about not
	// wrapping. Exposed for the /prev HTTP endpoint and the "prev"
	// WebSocket command so external scripts can drive the device in
	// either
	// direction without needing the physical button. Steps -1 against
	// the same clamped policy advanceConsole() uses; the math lives in
	// the functional core (stepWithin) so the policy stays unit-testable
	// on the host.
	int n = HowManyConsoles();
	if (n <= 0) {
		Serial.println("rewindConsole: no consoles loaded");
		return;
	}
	// The list has physical ends; see advanceConsole() for why a
	// no-op must not stamp the selection time.
	const int before = currentConsoleIndex;
	currentConsoleIndex =
		retroroom_core::stepWithin(currentConsoleIndex, n, -1);
	if (currentConsoleIndex == before) {
		Serial.println("Button: rewind -- at start of list");
		return;
	}
	// Same cursor sync as advanceConsole(); see the comment there.
	browsedConsoleIndex = currentConsoleIndex;
	// Stamp the selection time at-the-moment-of-decision (after
	// stepWithin but before any side effects / WS broadcast) -- same
	// contract as advanceConsole(). See comment there.
	currentConsoleSelectedAtMs = millis();
	Serial.print("Button: rewind -> index ");
	Serial.println(currentConsoleIndex);
	const Console& c = CurrentConsole();
	selectConsole(c);
#if defined(HAS_WIFI)
	{
		std::string msg = "console:";
		msg += c.name;
		msg += ":";
		msg += std::to_string(currentConsoleIndex);
		broadcastSocketMessage(msg);
	}
#endif
}
void consoles_loop() {
	// Flush the debounced LittleFS save when the quiet window has
	// elapsed with no further selectConsole() calls. See the comment
	// in selectConsole() for the why; the call site for this loop
	// pump is main.cpp::loop() so it runs alongside display_loop() /
	// network_loop() / etc.
	//
	// pendingSaveDueMs is 0 until the first click arms it, so the
	// very first loop tick after boot is a no-op. signed arithmetic
	// on the subtraction handles a wraparound of millis() correctly.
	if (pendingSaveDueMs != 0 && (int32_t)(millis() - pendingSaveDueMs) >= 0) {
		retroroom_store::saveLastSelectedConsole(pendingSaveIndex);
		pendingSaveDueMs = 0;
	}
}