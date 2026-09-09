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

		result.consoles.push_back(con);
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

}  // namespace retroroom_core