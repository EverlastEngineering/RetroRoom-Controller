// agent-script/led-feel-dump.cpp
//
// Print every `led` option as JSON, at the value the firmware uses when
// a config does not set it. Driven by agent-script/led-feel-dump.sh.
//
// This file is the reference for the `led` block in
// /consoles.json: it is *generated* from defaultLedFeel() rather than
// typed out, so it cannot drift from the firmware's own defaults.
//
// It used to carry two parallel lists -- a table of names and ranges,
// and a hand-written `v[N] = f.someField` ladder -- with a comment
// claiming "a mismatch here is a compile error, which is the point".
// It is not. A *missing* entry is a compile error; a *shifted* one is
// a silently wrong number, and adding `brightnessPct` shifted three of
// them, so the reference reported frameIntervalMs as 0 and brightnessPct
// as 8. A reference document that can be wrong without saying so is
// worse than no reference document.
//
// So there is one list, built from the struct as it goes. There is no
// second thing to keep in step, which is the only version of this that
// actually holds.

#include <cstdio>

#include <ConsoleConfig.h>
#include <CabinetMenu.h>  // ConsoleConfig.h names MenuItem for the `menu` array

using retroroom_core::LedFeel;
using retroroom_core::defaultLedFeel;
using retroroom_core::kLedStripCapacity;

namespace {

struct Field {
        const char* key;
        int value;
        int lo;
        int hi;
};


}  // namespace

int main() {
    const LedFeel f = defaultLedFeel();

// Key order here is the order they are printed in. Grouped the way the
// operator meets them: the strip, then the movement, then the knob,
// then brightness, then the commit, then the ring, then the strip as a
// whole.
#define FIELD(key, member, lo, hi) {key, f.member, lo, hi}

const Field kFields[] = {
    // The strip.
    FIELD("totalLeds", totalLeds, 1, kLedStripCapacity),

    // The travel: the block moving between consoles.
    FIELD("travelMs", travelMs, 0, 60000),
    FIELD("travelPeakWidth", travelPeakWidth, 1, 64),
    FIELD("travelSparkLeds", travelSparkLeds, 1, 64),

    // The knob.
    FIELD("detentsPerStep", detentsPerStep, 1, 64),
    FIELD("fastDetentsPerStep", fastDetentsPerStep, 1, 64),
    FIELD("fastSpinWindowMs", fastSpinWindowMs, 0, 60000),
    FIELD("settleLockoutMs", settleLockoutMs, 0, 60000),

    // The progression run.
    FIELD("fillMinLeds", fillMinLeds, 0, 512),
    FIELD("fillRetreatDelayMs", fillRetreatDelayMs, 0, 60000),
    FIELD("fillRetreatStepMs", fillRetreatStepMs, 0, 60000),
    FIELD("blobWidth", blobWidth, 1, 512),

    // The preview pulse.
    FIELD("pulseMs", pulseMs, 1, 60000),
    FIELD("pulseMinPct", pulseMinPct, 0, 100),
    FIELD("pulseMaxPct", pulseMaxPct, 0, 100),

    // Brightness, as a percentage of the role's own colour.
    FIELD("abovePct", abovePct, 0, 100),
    FIELD("selfPct", selfPct, 0, 100),
    FIELD("dimPct", dimPct, 0, 100),
    FIELD("fillPct", fillPct, 0, 100),
    FIELD("blobPct", blobPct, 0, 100),
    FIELD("browseFromPct", browseFromPct, 0, 100),
    FIELD("browseToPct", browseToPct, 0, 100),

    // The commit, in two halves.
    FIELD("explodeMs", explodeMs, 0, 60000),
    FIELD("igniteMs", igniteMs, 0, 60000),

    // The ring.
    FIELD("ringIdleMs", ringIdleMs, 0, 600000),
    FIELD("ringFlashMs", ringFlashMs, 0, 60000),
    FIELD("ringOffDelayMs", ringOffDelayMs, 0, 60000),
    FIELD("ringFadeMs", ringFadeMs, 0, 60000),

    // The strip as a whole. brightnessPct stops at 100 rather than 255
    // because the base it multiplies is already the most the fitted
    // supply can drive; asking for more is a brown-out under load, not
    // a brighter cabinet.
    FIELD("brightnessPct", brightnessPct, 0, 100),
    FIELD("frameIntervalMs", frameIntervalMs, 1, 100),
};

#undef FIELD

    const int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

    printf("\"led\": {\n");
    for (int i = 0; i < kFieldCount; ++i) {
        // Comma-separated, with the last one closed by the brace below
        // rather than carrying a trailing comma that would not parse.
        printf("\t\t\"%s\": %d%s\n", kFields[i].key, kFields[i].value,
               i + 1 < kFieldCount ? "," : "");
    }
    printf("\t}\n");
    return 0;
}
