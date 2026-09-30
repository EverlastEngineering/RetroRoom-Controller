// Writing changed values back into a config document.
//
// I originally proposed patching the original text in place, key by
// key, on the grounds that it could not lose a key the parser does not
// understand. That was solving the wrong problem. This round-trips the
// document through ArduinoJson instead, which is both simpler and
// *more* safe, because it never looks at LedFeel at all:
//
//   - it cannot lose an unknown key, because the JsonDocument holds
//     every key in the file, known or not, and serialising writes them
//     all back;
//   - it preserves key order, which ArduinoJson's memory layout keeps;
//   - it needs no text surgery, so there is no "did the brace land in
//     the right place" class of bug to have.
//
// What it costs is formatting: the file comes back pretty-printed by
// ArduinoJson rather than by hand. For a machine-read config that is a
// non-issue, and the checked-in examples will show the reformat in git
// the first time one is written, which is honest.
//
// And it removes the awkward case. There is no "what if there is no
// file to patch" -- a device running off the PROGMEM default can be
// saved too, by round-tripping that text instead, so the file is
// created from the same content the cabinet is already using.

#include "ConsoleConfig.h"

#include <ArduinoJson.h>

namespace retroroom_core {

bool applyConfigEdits(const std::string& json, const ConfigEdit* edits,
                      int count, std::string* out, std::string* error) {
	if (out == nullptr) {
		if (error != nullptr) {
			*error = "no output buffer";
		}
		return false;
	}
	// Parsed rather than validated: a document that does not parse is
	// not a document we can round-trip, and refusing here is what
	// stops a bad save from replacing a working file with a worse one.
	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, json);
	if (err) {
		if (error != nullptr) {
			*error = err.c_str();
		}
		return false;
	}

	for (int i = 0; i < count; ++i) {
		const ConfigEdit& edit = edits[i];
		const char* block = nullptr;
		const LedField* field = findConfigField(edit.path, &block);
		if (field == nullptr) {
			if (error != nullptr) {
				*error = std::string("unknown setting: ") + edit.path;
			}
			return false;
		}
		// Clamped here, not left to the next boot. A value the parser
		// would pull back into range is a value that would have made the
		// file and the running cabinet quietly disagree, and that is
		// the whole class of bug this file exists to stop. The menu's own
		// range is required to be a subset of this one, so in practice
		// this only fires for an API caller.
		int value = edit.value;
		if (value < field->lo) {
			value = field->lo;
		}
		if (value > field->hi) {
			value = field->hi;
		}
		// The block is created if it is not already there. An operator
		// who adds a menu item for a setting the file never mentioned
		// is doing something the file has to grow into, not something
		// the file is allowed to reject.
		doc[block][field->key] = value;
	}

	std::string result;
	serializeJsonPretty(doc, result);
	*out = result;
	if (error != nullptr) {
		error->clear();
	}
	return true;
}

}  // namespace retroroom_core
