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

LedFeel defaultLedFeel() {
	LedFeel f;
	f.totalLeds = kLedStripCapacity;

	// The travel. The block's peak is a *cap* on how wide it gets, not
	// a target: the block always ends exactly the width of the console
	// it lands on, because travelEdges() floors the cap at the target's
	// width. A value below the widest window in the cabinet is therefore
	// simply ignored -- which is the safe direction, and worth knowing
	// before anyone lowers it hoping for a slimmer block.
	f.travelMs = 420;
	f.travelPeakWidth = 6;
	f.travelSparkLeds = 2;  // 1 reads as a stray pixel, 2 as an object

	// Brightness, as a percentage of the role's own colour. ABOVE is the
	// resting stack and defaults to 0: a cumulative reading was reported
	// from the bench as "the whole string is lit" rather than as a stack.
	// The dim/fill split is deliberate -- sharing a level between "where
	// the stack ends" and "how far I have got" leaves nothing to read
	// progress from.
	f.abovePct = 0;
	f.selfPct = 100;
	f.dimPct = 22;
	f.fillPct = 45;
	f.blobPct = 100;
	f.browseFromPct = 25;
	f.browseToPct = 45;

	// The knob. Five detents was chosen by feel on the bench: fewer and
	// a step happens by accident, more and the knob stops feeling like it
	// is choosing anything. The fast path is two.
	//
	// fastSpinWindowMs is 0, which DISABLES the escalation, and the
	// reason is worth keeping: it was 1000ms, which sounds generous but
	// is *shorter than a deliberate human detent*. The first detent
	// registered as deliberate, the second tripped the window, and from
	// there on the browse was permanently in fast mode. It has to be
	// comfortably LONGER than the operator's slowest deliberate turn.
	// We do not have a number for that yet, which is why it is off
	// rather than merely retuned; a starting guess would be 2000-3000ms.
	// The latching that compounded this is fixed independently -- the
	// escalation now reflects the gap before each detent, so it drops
	// back the moment they slow down.
	f.detentsPerStep = 5;
	f.fastDetentsPerStep = 2;
	f.fastSpinWindowMs = 0;

	// 250ms after a commit, detents are ignored so an overshoot costs one
	// step rather than two. Deliberately shorter than the travel: once
	// the lockout expires a turn cuts the animation short, which is what
	// a mid-travel detent already did, and covering the whole travel
	// would swallow real input for twice as long.
	f.settleLockoutMs = 250;

	// The progression run. The floor exists because a step *between
	// shelves* is a couple of pixels in index space and a long way round
	// physically, so filling the literal gap would leave the indicator
	// barely moving on exactly the steps hardest to read.
	//
	// The retreat gives an abandoned run back rather than leaving it
	// pointing at a console nobody asked for, one LED at a time from the
	// leading edge. 250ms per LED is also that LED's fade, so the run
	// reels in rather than strobing; 0 for the delay disables it.
	f.fillMinLeds = 3;
	f.fillRetreatDelayMs = 3000;
	f.fillRetreatStepMs = 250;
	f.blobWidth = 3;

	// The preview pulse. 1100ms reads as a slow breath rather than a
	// heartbeat. The dim end wants to stay clearly non-zero or the pulse
	// strobes.
	f.pulseMs = 1100;
	f.pulseMinPct = 30;
	f.pulseMaxPct = 100;

	// The commit, in two halves: the window dissolving outward, then
	// coming back. 400 to dissolve and 200 to rebuild -- the explosion
	// is the one that has to be read as a movement, and the ignite is the
	// one that only has to arrive. They meet at zero brightness, which
	// is the gap between the old console going and the new one arriving.
	f.explodeMs = 400;
	f.igniteMs = 200;

	// The ring. The idle timeout is also what reverts an abandoned
	// browse, and it is *held* while the progression run is still
	// unwinding so it never cuts a retreat short.
	f.ringIdleMs = 5000;
	f.ringFlashMs = 120;  // the strike on a commit. 0 disables

	// How often an in-flight frame goes to the wire. A sampling rate, not
	// a step count: every frame is computed from elapsed time, so raising
	// this plays the same animation more smoothly. The floor is how long
	// FastLED.show() takes to clock the strip out plus whatever the rest
	// of loop() needs -- the driver measures that and prints it, so set
	// this from the measurement rather than by guessing.
	f.frameIntervalMs = 8;

	// Colours. Amber for what the operator is being offered, cool blue
	// for the context it is contrasted against: that is the one
	// distinction worth having by eye alone. Values are deliberately
	// conservative -- this strip sits next to a television in a dark
	// room, and a misconfigured colour here is a glare problem.
	//
	// TRAVEL must equal PROPOSAL. The travel's last frame *is* the target
	// window and the frame after it is that window pulsing as a
	// proposal, so any difference is a flash of a different hue at exactly
	// the moment the movement resolves into an answer.
	const int defaults[kRoleCount][3] = {
		{12, 28, 40},  // stack: context, and dark so it never competes
		{20, 34, 48},  // leaving: the console being turned away from
		{16, 40, 56},  // fill: the progression run, cool so it reads as
		{64, 40, 8},   //       progress and not as a console that is on
		{64, 40, 8},   // travel: the proposal in flight -- see above
		{48, 36, 24},  // selected: the resting selection, warmer and calmer
	};
	for (int r = 0; r < kRoleCount; ++r) {
		f.colorR[r] = defaults[r][0];
		f.colorG[r] = defaults[r][1];
		f.colorB[r] = defaults[r][2];
	}
	return f;
}

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

#define RR_FEEL(key, field, def, lo, hi) \
	f.field = readInt(led, key, def, lo, hi, warnings)

	// The strip. totalLeds is the one value that cannot be believed if
	// it exceeds the build: the buffer is allocated at compile time and
	// the animation would address LEDs that do not exist. A config
	// claiming more is pulled down to the capacity rather than refused,
	// for the same accept-and-clamp reason as everything else -- but the
	// clamp is reported, because this one is a hardware mismatch and the
	// operator is the one who needs to know.
	RR_FEEL("totalLeds", totalLeds, kLedStripCapacity, 1, kLedStripCapacity);

	RR_FEEL("travelMs", travelMs, 420, 0, 60000);
	RR_FEEL("travelPeakWidth", travelPeakWidth, 6, 1, 64);
	RR_FEEL("travelSparkLeds", travelSparkLeds, 2, 1, 64);
	RR_FEEL("abovePct", abovePct, 0, 0, 100);
	RR_FEEL("selfPct", selfPct, 100, 0, 100);
	RR_FEEL("dimPct", dimPct, 22, 0, 100);
	RR_FEEL("fillPct", fillPct, 45, 0, 100);
	RR_FEEL("blobPct", blobPct, 100, 0, 100);
	RR_FEEL("browseFromPct", browseFromPct, 25, 0, 100);
	RR_FEEL("browseToPct", browseToPct, 45, 0, 100);
	RR_FEEL("detentsPerStep", detentsPerStep, 5, 1, 64);
	RR_FEEL("fastDetentsPerStep", fastDetentsPerStep, 2, 1, 64);
	RR_FEEL("fastSpinWindowMs", fastSpinWindowMs, 0, 0, 60000);
	RR_FEEL("settleLockoutMs", settleLockoutMs, 250, 0, 60000);
	RR_FEEL("fillMinLeds", fillMinLeds, 3, 0, 512);
	RR_FEEL("fillRetreatDelayMs", fillRetreatDelayMs, 3000, 0, 60000);
	RR_FEEL("fillRetreatStepMs", fillRetreatStepMs, 250, 0, 60000);
	RR_FEEL("blobWidth", blobWidth, 3, 1, 512);
	RR_FEEL("pulseMs", pulseMs, 1100, 1, 60000);
	RR_FEEL("pulseMinPct", pulseMinPct, 30, 0, 100);
	RR_FEEL("pulseMaxPct", pulseMaxPct, 100, 0, 100);
	RR_FEEL("explodeMs", explodeMs, 400, 0, 60000);
	RR_FEEL("igniteMs", igniteMs, 200, 0, 60000);
	RR_FEEL("ringIdleMs", ringIdleMs, 5000, 0, 600000);
	RR_FEEL("ringFlashMs", ringFlashMs, 120, 0, 60000);
	RR_FEEL("frameIntervalMs", frameIntervalMs, 8, 1, 100);

#undef RR_FEEL

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
			if (ms < 0) ms = 0;
			if (ms > 600000) ms = 600000;
			result.lcdBacklightOffAfterMs = static_cast<std::uint32_t>(ms);
		}
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