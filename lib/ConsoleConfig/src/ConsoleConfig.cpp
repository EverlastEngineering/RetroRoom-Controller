#include "ConsoleConfig.h"

#include <ArduinoJson.h>

#include <cstdlib>
#include <cstring>

namespace retroroom_core {

namespace {

// Parse "0x430", "0X430", or "430" into a uint16_t. Returns false on bad input.
bool parseHex(const std::string& s, std::uint16_t& out) {
	if (s.empty()) {
		return false;
	}
	const char* start = s.c_str();
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		start += 2;
	}
	char* end = nullptr;
	unsigned long v = strtoul(start, &end, 16);
	if (end == start || *end != '\0' || v > 0xFFFF) {
		return false;
	}
	out = static_cast<std::uint16_t>(v);
	return true;
}

const IrCode* findIrCode(const std::vector<IrCode>& codes, const std::string& name) {
	for (const auto& c : codes) {
		if (c.name == name) return &c;
	}
	return nullptr;
}

}  // namespace

// ===========================================================================
// The LED string's feel
// ===========================================================================
//
// Every default and every range is in this file and nowhere else. The
// JSON is parsed *into* the struct in the header, so "the value is
// missing" is not a case each call site has to reason about -- it is
// the value below.
//
// The policy on a bad value follows the one already used for the LCD
// backoff: accept, clamp, and say so. A config the operator wrote by
// hand should not be a reason their cabinet stops working, and the
// alternative to clamping is booting values nobody asked for. But a
// clamp nobody hears about is no better than the bug it replaced, so
// every one appends a line to result.warnings for the shell to print.

namespace {

// Read an integer: the default when absent, clamped to [lo, hi] when
// not, and a line in `warnings` when the clamp bit.
int readInt(JsonObjectConst obj, const char* key, int def, int lo, int hi,
			std::vector<std::string>* warnings) {
	JsonVariantConst v = obj[key];
	if (v.isNull()) {
		return def;
	}
	const long long raw = v.as<long long>();
	const long long clamped = (raw < lo) ? lo : ((raw > hi) ? hi : raw);
	if (clamped != raw && warnings != nullptr) {
		warnings->push_back(std::string("led.") + key + " = " +
							std::to_string(raw) + " is outside " +
							std::to_string(lo) + ".." + std::to_string(hi) +
							"; using " + std::to_string(clamped));
	}
	return static_cast<int>(clamped);
}

}  // namespace

namespace {

// Parse the optional top-level `led` block. Internal on purpose: taking
// a JsonObjectConst in the public header would put ArduinoJson in front
// of every consumer of this library -- the shell, the simulator, the
// tests -- none of which should have to know it parses anything.
// Driven through loadFromJson(), which is the real entry point and
// therefore the better thing for the tests to exercise anyway.
void loadLedFeel(JsonObjectConst led, std::vector<std::string>* warnings,
				 LedFeel* out) {
	LedFeel f = defaultLedFeel();
	if (out == nullptr) {
		return;
	}
	if (led.isNull()) {
		// No `led` block at all. The defaults are already in `f`, and
		// that is the whole contract: a config written before this
		// feature existed behaves exactly as it did.
		*out = f;
		return;
	}

	// Every scalar, through the one table in LedFieldTable.cpp.
	//
	// The default passed to readInt() is the struct's *current* value,
	// which is defaultLedFeel()'s -- not a second copy of it. The old
	// RR_FEEL macro passed a literal, which meant loadLedFeel() seeded
	// the struct from defaultLedFeel() and then overwrote every field
	// with the macro's copy, so the defaults file was dead code for any
	// key absent from the JSON and nothing checked the two agreed.
	// totalLeds is not special-cased: the parser's job is the range, and
	// the capacity ceiling is that field's `hi`.
	int fieldCount = 0;
	const LedField* fields = ledFields(&fieldCount);
	for (int i = 0; i < fieldCount; ++i) {
		const LedField& field = fields[i];
		f.*(field.member) = readInt(led, field.key, f.*(field.member),
								   field.lo, field.hi, warnings);
	}

	// Cross-field rules, applied after the reads so one number in the
	// file cannot be a lie on its own. Kept here rather than at the call
	// sites because each is a relationship, and a relationship restated
	// in two places is one that will eventually disagree with itself.
	if (f.fastDetentsPerStep > f.detentsPerStep) {
		if (warnings != nullptr) {
			warnings->push_back("led.fastDetentsPerStep was above "
								"led.detentsPerStep; clamped to it");
		}
		f.fastDetentsPerStep = f.detentsPerStep;
	}
	if (f.blobWidth > f.totalLeds) {
		f.blobWidth = f.totalLeds;
	}
	if (f.fillMinLeds > f.totalLeds) {
		f.fillMinLeds = f.totalLeds;
	}

	// Colours: one [r, g, b] array per role, named, rather than
	// twenty-one keys. An absent or malformed one leaves the default --
	// half a colour is not a thing anybody meant to ask for.
	static const char* kRoleKeys[kRoleCount] = {
		"stack", "leaving", "fill", "travel", "proposal", "selected",
	};
	static const char* kChannel[3] = {"r", "g", "b"};
	JsonObjectConst colors = led["colors"];
	for (int r = 0; r < kRoleCount; ++r) {
		JsonVariantConst v = colors[kRoleKeys[r]];
		if (v.isNull() || !v.is<JsonArrayConst>()) {
			continue;
		}
		JsonArrayConst rgb = v.as<JsonArrayConst>();
		if (rgb.size() != 3) {
			if (warnings != nullptr) {
				warnings->push_back(std::string("led.colors.") + kRoleKeys[r] +
									" needs exactly [r, g, b]; left at the default");
			}
			continue;
		}
		int* dst[3] = {&f.colorR[r], &f.colorG[r], &f.colorB[r]};
		for (int ch = 0; ch < 3; ++ch) {
			const long long raw = rgb[ch].as<long long>();
			const long long clamped = (raw < 0) ? 0 : ((raw > 255) ? 255 : raw);
			if (clamped != raw && warnings != nullptr) {
				warnings->push_back(std::string("led.colors.") + kRoleKeys[r] +
									"." + kChannel[ch] +
									" is outside 0..255; using " +
									std::to_string(clamped));
			}
			*dst[ch] = static_cast<int>(clamped);
		}
	}

	*out = f;
}

}  // namespace

LoadResult loadFromJson(const char* json, std::size_t len) {
	LoadResult result;

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, json, len);
	if (err) {
		result.error = std::string("JSON parse error: ") + err.c_str();
		return result;
	}

	JsonObjectConst irCodesObj = doc["irCodes"];
	if (irCodesObj.isNull()) {
		result.error = "Missing 'irCodes' object";
		return result;
	}

	for (JsonPairConst kv : irCodesObj) {
		IrCode ic;
		ic.name = kv.key().c_str();
		std::uint16_t code = 0;
		if (!parseHex(kv.value().as<std::string>(), code)) {
			result.error = "Invalid hex for IR code '" + ic.name + "'";
			return result;
		}
		ic.code = code;
		result.irCodes.push_back(ic);
	}

	JsonObjectConst namesObj = doc["consoleNames"];  // optional

	JsonArrayConst consolesArr = doc["consoles"];
	if (consolesArr.isNull()) {
		result.error = "Missing 'consoles' array";
		return result;
	}

	for (JsonObjectConst c : consolesArr) {
		Console con;
		con.id = c["id"].as<std::string>();
		if (con.id.empty()) {
			result.error = "Console entry missing 'id'";
			return result;
		}

		if (!namesObj.isNull()) {
			JsonVariantConst n = namesObj[con.id];
			if (!n.isNull()) {
				con.name = n.as<std::string>();
			}
		}
		if (con.name.empty()) {
			con.name = con.id;
		}

		std::string tvInput = c["tvInput"].as<std::string>();
		const IrCode* ic = findIrCode(result.irCodes, tvInput);
		if (ic == nullptr) {
			result.error = "Console '" + con.id + "' references unknown tvInput '" + tvInput + "'";
			return result;
		}
		con.tvinput = ic->code;

		con.selector_position = c["selectorPosition"] | 0;
		con.led_position = c["ledPosition"] | 0;
		con.led_width = c["ledWidth"] | 0;
		// Optional shelf number, default 0. A console's shelf is not
		// derivable from the LED layout -- two shelves are strung as one
		// continuous chain around the cabinet, so pixel order alone
		// cannot tell you where one shelf ends and the next begins. The
		// LED animations need it to know that a step crossing between
		// shelves is a different kind of move.
		//
		// Optional so every config written before this field existed
		// loads as a single shelf and behaves exactly as it did.
		con.shelf = c["shelf"] | 0;
		// Optional tagline. as<std::string>() on a null variant returns
		// "null" (ArduinoJson quirk) so we have to check isNull() first
		// and default to empty.
		JsonVariantConst taglineVar = c["tagline"];
		if (!taglineVar.isNull()) {
			con.tagline = taglineVar.as<std::string>();
		}

		result.consoles.push_back(con);
	}

	// Top-level lcd block (optional). Default backlight timeout is 30 s.
	// Bounds-checked: 0 means "never off" (legal); max is 10 min
	// (600000 ms) to prevent typos that would lock the LCD on for hours.
	// Out-of-range values are accepted but clamped -- the operator's
	// device should still boot.
	JsonObjectConst lcdObj = doc["lcd"];
	if (!lcdObj.isNull()) {
		JsonVariantConst v = lcdObj["backlightOffAfterMs"];
		if (!v.isNull()) {
			long long ms = v.as<long long>();
			// Reported, not silent. Every other value in this file says
			// so when it clamps, and this one did not -- which was fine
			// while it was only readable from the file, and stops being
			// fine the moment a menu or the API can also write it. A
			// setting that can be written from two places and can
			// disagree with itself is the failure this file has been
			// fixing all session.
			if (ms < 0) {
				result.warnings.push_back(
					"lcd.backlightOffAfterMs was negative; clamped to 0");
				ms = 0;
			}
			if (ms > 600000) {
				result.warnings.push_back(
					"lcd.backlightOffAfterMs was above 600000; clamped");
				ms = 600000;
			}
			result.lcdBacklightOffAfterMs = static_cast<std::uint32_t>(ms);
		}
	}

	// Top-level network block (optional). Two fields today, and both
	// are bools rather than numbers with a unit -- so they are stored
	// as ints in the shared field table, which is what lets the menu's
	// "type": "bool" treat them like any other 0/1 setting instead of
	// needing to know what they mean.
	//
	// Read through the registry rather than by hand, so the range the
	// parser enforces is the same one the menu offers and the same one
	// the writer clamps to. Three places to state "0 or 1" is three
	// places to disagree.
	JsonObjectConst netObj = doc["network"];
	if (!netObj.isNull()) {
		struct {
			const char* jsonKey;
			const char* path;
			bool* target;
		} const kBools[] = {
			{"showWIFIConnectionFailureMessage",
			 "network.showWIFIConnectionFailureMessage",
			 &result.showWifiConnectionFailureMessage},
			{"disable", "network.disable", &result.networkDisabled},
		};
		for (const auto& b : kBools) {
			JsonVariantConst v = netObj[b.jsonKey];
			if (v.isNull()) {
				continue;
			}
			const LedField* def = findConfigField(b.path, nullptr);
			int on = v.as<int>();
			if (def != nullptr && (on < def->lo || on > def->hi)) {
				result.warnings.push_back(
					std::string("network.") + b.jsonKey +
					" was not 0 or 1; clamped");
				on = on < def->lo ? def->lo : def->hi;
			}
			*b.target = (on != 0);
		}
	}

	// Top-level menu array (optional). The operator's own settings menu:
	// each entry names a path into one of the other blocks, so adding an
	// adjustable setting is a line of JSON rather than a firmware
	// change.
	//
	// Entries are validated against the shared field registry, and one
	// that does not name a real setting is dropped with a warning rather
	// than taking the whole menu down. A menu is the thing an operator
	// edits by hand, so a typo in it is likely, and a cabinet whose
	// menu vanished because of one is worse than a menu with one entry
	// missing.
	JsonArrayConst menuArr = doc["menu"];
	if (!menuArr.isNull()) {
		for (JsonVariantConst entry : menuArr) {
			JsonObjectConst obj = entry.as<JsonObjectConst>();
			if (obj.isNull()) {
				continue;
			}
			const char* label = obj["label"] | "";
			const char* key = obj["set"] | "";
			if (label[0] == '\0' || key[0] == '\0') {
				result.warnings.push_back(
					"menu entry needs both \"label\" and \"set\"; skipped");
				continue;
			}
			// Resolved through the same registry every other client
			// uses, and stored normalised to "<block>.<field>".
			//
			// It used to strip a literal "led." prefix and look the
			// bare key up in findLedField(), which meant a menu could
			// only ever edit the strip -- while the documentation and
			// the `lcd` block both said otherwise. The two disagreed,
			// and the code was the one that was wrong.
			//
			// Normalising rather than keeping whatever the operator
			// typed is what makes the key safe to hand straight to
			// applyConfigValue() and to the writer: "detentsPerStep"
			// and "led.detentsPerStep" mean the same setting but write
			// back differently, and only the long form says which
			// block was meant.
			const char* block = nullptr;
			const LedField* def = findConfigField(key, &block);
			if (def == nullptr || block == nullptr) {
				result.warnings.push_back(
					std::string("menu entry \"") + label +
					"\" names an unknown setting \"" + key + "\"; skipped");
				continue;
			}
			const std::string field = std::string(block) + "." + def->key;
			// `min`/`max` narrow what the menu offers and default to the
			// field's own range. They are allowed to be narrower and NOT
			// wider: a menu that offers values the file would refuse is
			// a menu that lies.
			int lo = obj["min"] | def->lo;
			int hi = obj["max"] | def->hi;
			if (lo < def->lo) lo = def->lo;
			if (hi > def->hi) hi = def->hi;
			if (lo > hi) {
				result.warnings.push_back(
					std::string("menu entry \"") + label +
					"\" has min above max; using the setting's own range");
				lo = def->lo;
				hi = def->hi;
			}
			int step = obj["step"] | 1;
			if (step < 1) step = 1;
			// "type": "bool" is sugar for min 0 / max 1 / step 1. The
			// core needs the flag because a bool alternates rather than
			// stepping, and reading min/max to work that out would put
			// the same decision in two places.
			const bool isBool =
				(obj["type"] | std::string("")) == std::string("bool");
			result.menu.labels.push_back(label);
			result.menu.keys.push_back(field);
			// Built after the strings are all in place, so the pointers
			// below cannot be invalidated by a later push_back.
			MenuItem item;
			item.label = result.menu.labels.back().c_str();
			item.key = result.menu.keys.back().c_str();
			item.isBool = isBool;
			item.lo = isBool ? 0 : lo;
			item.hi = isBool ? 1 : hi;
			item.step = isBool ? 1 : step;
			// "preview": apply while turning rather than on commit. Off
			// by default, because whether a setting benefits from live
			// feedback is a judgement about the setting.
			item.preview = (obj["preview"] | false);
			result.menu.items.push_back(item);
		}
		// Re-aim every item at the strings as they now stand.
		// ParsedMenu does this on every copy too, so there is one
		// place that knows the relationship and no ordering to get
		// right here.
		result.menu.repoint();
	}

	// Top-level shelves block (optional). The physical extent of each
	// shelf, so a console's commit animation knows where its own shelf
	// ends rather than where the next console happens to begin.
	//
	// Optional: absent means "derive from the consoles", which is what
	// every config written before this block did.
	//
	// A shelf with toLed < fromLed is a typo, and the entry is dropped
	// rather than inverted -- an inverted extent would be a *wider*
	// bound than the operator asked for, and a commit animation that
	// expands further than the shelf is worse than one that expands
	// slightly less.
	JsonArrayConst shelvesArr = doc["shelves"];
	if (!shelvesArr.isNull()) {
		for (JsonObjectConst s : shelvesArr) {
			Shelf shelf;
			shelf.id = s["id"] | 0;
			shelf.fromLed = s["fromLed"] | 0;
			shelf.toLed = s["toLed"] | 0;
			if (shelf.fromLed < 0 || shelf.toLed < shelf.fromLed) {
				continue;
			}
			result.shelves.push_back(shelf);
		}
	}

	// The `led` block, last: it has no bearing on whether the config is
	// valid, only on what the strip does with it. A missing block is not
	// an error, and a clamped one leaves a line in result.warnings.
	loadLedFeel(doc["led"], &result.warnings, &result.feel);

	result.ok = true;
	return result;
}

LoadResult loadFromJson(const char* json) {
	return loadFromJson(json, std::strlen(json));
}

Selection::Selection(const std::vector<Console>& consoles)
	: consoles_(consoles) {}

const Console& Selection::current() const {
	return consoles_[index_];
}

bool Selection::rotate(int direction, bool wraparound) {
	if (consoles_.empty() || direction == 0) {
		return false;
	}

	int n = static_cast<int>(consoles_.size());
	int next = static_cast<int>(index_) + (direction > 0 ? 1 : -1);

	if (wraparound) {
		next = ((next % n) + n) % n;
	} else if (next < 0 || next >= n) {
		return false;
	}

	if (static_cast<std::size_t>(next) == index_) {
		return false;
	}
	index_ = static_cast<std::size_t>(next);
	return true;
}

int wraparoundNext(int current, int n, int direction) {
	if (n <= 0) {
		return 0;
	}
	if (direction == 0) {
		return current;
	}
	int next = current + (direction > 0 ? 1 : -1);
	return ((next % n) + n) % n;
}


int clampIndex(int requested, int n) {
	if (n <= 0) {
		return 0;
	}
	if (requested < 0) {
		return 0;
	}
	if (requested >= n) {
		return n - 1;
	}
	return requested;
}

int stepWithin(int current, int n, int direction) {
	// The cabinet has physical ends. Turning past the first or last
	// console reaches nothing, so the cursor stays put and the caller
	// is expected to *freeze* whatever it was indicating -- a knob that
	// visibly keeps moving with no destination reads as a fault.
	//
	// n <= 0 has no in-range answer, so it matches wraparoundNext()'s
	// empty-list fallback rather than inventing one.
	if (n <= 0) {
		return 0;
	}
	if (direction == 0) {
		return clampIndex(current, n);
	}
	// Fold a stale index into range first. A step function is only
	// meaningful for a cursor that is already on the list, and a caller
	// that got that wrong -- a hand-edited config, a restore that lost
	// its clamp -- should get a sane fixed point rather than have the
	// bad value carried onward and stepped from again.
	const int at = clampIndex(current, n);
	int next = at + (direction > 0 ? 1 : -1);
	if (next < 0 || next >= n) {
		return at;
	}
	return next;
}

bool canStepWithin(int current, int n, int direction) {
	// "Would stepWithin() actually move?" A caller that wants to freeze
	// an indication needs to ask without performing the step.
	return stepWithin(current, n, direction) != current;
}

bool validateConsoleConfigJson(const std::string& json, LoadResult& out) {
	// Out is overwritten on every call (including on failure) so the
	// caller doesn't have to remember to reset it. The error string is
	// only populated on the failure path -- on success, callers should
	// branch on the bool return and ignore out.error.
	out = loadFromJson(json.data(), json.size());
	return out.ok;
}

BackupRotation rotateBackupBlobs(const std::string& current_live,
                                 const std::string& current_backup1,
                                 const std::string& new_payload) {
	// Pure byte-level rotation. The new payload is committed to the live
	// slot unconditionally; the live and backup1 values shift down by
	// one slot. backup2 is dropped (the previous backup2 -- if any --
	// becomes the new backup1's predecessor in the chain but we only
	// keep two backups total).
	//
	// Rationale for dropping the oldest:
	//   - Two backups (live + b1 + b2 = three on-disk copies) give us a
	//     one-step-recovery window: if the new live is corrupt and the
	//     prior live was also corrupt, b1 still has the last-known-good.
	//   - Keeping a third backup would burn LittleFS space for marginal
	//     gain -- any corruption pattern that's bad enough to take out
	//     b1 + b2 is bad enough that the operator should factory-reset.
	//
	// Empty inputs (file missing / wiped FS) are propagated forward --
	// if backup1 was empty it stays empty in the new backup2 slot.
	BackupRotation out;
	out.live = new_payload;
	out.backup1 = current_live;
	out.backup2 = current_backup1;
	return out;
}

}  // namespace retroroom_core