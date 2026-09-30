// agent-script/led-feel-dump.cpp
//
// Prints every field of retroroom_core::LedFeel as the `led` block a
// /consoles.json would carry, at its current default.
//
// Why a generator and not a hand-written file: a reference that somebody
// typed is a second copy of the defaults, and the whole point of moving
// them out of configuration.h was to have exactly one. This reads
// defaultLedFeel() -- the same function the firmware and the simulator
// call -- so the reference cannot disagree with the device.
//
// Needs no JSON library: the defaults file is pure data.
//
// Maintenance: if a field is added to LedFeel, add it here too. Nothing
// enforces that, and the honest reason is that there is no reflection in
// C++ to count the fields; the failure mode is a new option simply not
// appearing in the reference, which is much better than a reference with
// a stale value in it.

#include <ConsoleConfig.h>

#include <cstdio>

using retroroom_core::LedRoleId;
using retroroom_core::LedFeel;

namespace {

const char* kRoleKeys[retroroom_core::kRoleCount] = {
	"stack", "leaving", "fill", "travel", "proposal", "selected",
};

// Key order here is the order they are printed in. Grouped the way the
// operator meets them: the strip, then the movement, then the knob, then
// brightness, then the commit, then colour.
struct Field {
	const char* key;
	int value;
	int lo;
	int hi;
};

const Field kFields[] = {
	// The strip.
	{"totalLeds", 0, 1, retroroom_core::kLedStripCapacity},

	// The travel: the block moving between consoles.
	{"travelMs", 0, 0, 60000},
	{"travelPeakWidth", 0, 1, 64},
	{"travelSparkLeds", 0, 1, 64},

	// The knob.
	{"detentsPerStep", 0, 1, 64},
	{"fastDetentsPerStep", 0, 1, 64},
	{"fastSpinWindowMs", 0, 0, 60000},
	{"settleLockoutMs", 0, 0, 60000},

	// The progression run.
	{"fillMinLeds", 0, 0, 512},
	{"fillRetreatDelayMs", 0, 0, 60000},
	{"fillRetreatStepMs", 0, 0, 60000},
	{"blobWidth", 0, 1, 512},

	// The preview pulse.
	{"pulseMs", 0, 1, 60000},
	{"pulseMinPct", 0, 0, 100},
	{"pulseMaxPct", 0, 0, 100},

	// Brightness, as a percentage of the role's own colour.
	{"abovePct", 0, 0, 100},
	{"selfPct", 0, 0, 100},
	{"dimPct", 0, 0, 100},
	{"fillPct", 0, 0, 100},
	{"blobPct", 0, 0, 100},
	{"browseFromPct", 0, 0, 100},
	{"browseToPct", 0, 0, 100},

	// The commit, in two halves.
	{"explodeMs", 0, 0, 60000},
	{"igniteMs", 0, 0, 60000},

	// The ring.
	{"ringIdleMs", 0, 0, 600000},
	{"ringFlashMs", 0, 0, 60000},
	{"ringOffDelayMs", 0, 0, 60000},
	{"ringFadeMs", 0, 0, 60000},

	// The strip as a whole.
	{"frameIntervalMs", 0, 1, 100},
};

const int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

}  // namespace

int main() {
	const LedFeel f = retroroom_core::defaultLedFeel();

	// The value of each field, fetched by name. A mismatch here is a
	// compile error, which is the point: renaming a field without
	// updating the reference cannot silently produce a wrong number.
	int v[kFieldCount];
	v[0] = f.totalLeds;
	v[1] = f.travelMs;
	v[2] = f.travelPeakWidth;
	v[3] = f.travelSparkLeds;
	v[4] = f.detentsPerStep;
	v[5] = f.fastDetentsPerStep;
	v[6] = f.fastSpinWindowMs;
	v[7] = f.settleLockoutMs;
	v[8] = f.fillMinLeds;
	v[9] = f.fillRetreatDelayMs;
	v[10] = f.fillRetreatStepMs;
	v[11] = f.blobWidth;
	v[12] = f.pulseMs;
	v[13] = f.pulseMinPct;
	v[14] = f.pulseMaxPct;
	v[15] = f.abovePct;
	v[16] = f.selfPct;
	v[17] = f.dimPct;
	v[18] = f.fillPct;
	v[19] = f.blobPct;
	v[20] = f.browseFromPct;
	v[21] = f.browseToPct;
	v[22] = f.explodeMs;
	v[23] = f.igniteMs;
	v[24] = f.ringIdleMs;
	v[25] = f.ringFlashMs;
	v[26] = f.ringOffDelayMs;
	v[27] = f.ringFadeMs;
	v[28] = f.frameIntervalMs;

	printf("\"led\": {\n");
	for (int i = 0; i < kFieldCount; ++i) {
		printf("\t\t\"%s\": %d,\n", kFields[i].key, v[i]);
	}
	printf("\t\t\"colors\": {\n");
	for (int r = 0; r < retroroom_core::kRoleCount; ++r) {
		printf("\t\t\t\"%s\": [%d, %d, %d]%s\n", kRoleKeys[r], f.colorR[r],
			   f.colorG[r], f.colorB[r],
			   (r + 1 < retroroom_core::kRoleCount) ? "," : "");
	}
	printf("\t\t}\n");
	printf("\t}\n");
	return 0;
}
