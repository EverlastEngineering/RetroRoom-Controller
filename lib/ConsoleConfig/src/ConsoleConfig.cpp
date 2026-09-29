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
			if (ms < 0) ms = 0;
			if (ms > 600000) ms = 600000;
			result.lcdBacklightOffAfterMs = static_cast<std::uint32_t>(ms);
		}
	}

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