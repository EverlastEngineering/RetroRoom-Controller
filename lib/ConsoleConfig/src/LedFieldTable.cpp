// The one table of `led` options.
//
// Every `led.*` key, where it lives in LedFeel, and the range it may
// legally hold. Split into its own translation unit so it is obviously
// the single home for the ranges, and so the parser and the runtime
// setter cannot each grow their own copy.
//
// It deliberately does NOT carry defaults. defaultLedFeel() is the
// default, in LedFeelDefaults.cpp, beside the comment explaining what
// each number trades off -- and before this table the parser *also*
// carried a default per key, passed to readInt() as its `def`. That was
// two homes for every number in the file, and the two were not checked
// against each other: loadLedFeel() seeded the struct from
// defaultLedFeel() and then immediately overwrote every field with the
// macro's copy, so defaultLedFeel()'s values were dead unless the two
// happened to agree. loadLedFeel() now passes the struct's current value
// as the default, which makes LedFeelDefaults.cpp the only place a
// default is written down and this table the only place a range is.

#include "ConsoleConfig.h"

namespace retroroom_core {

namespace {

// One row per int field of LedFeel. The colours are absent on purpose:
// they are [r, g, b] arrays read as a nested object, not scalars, and
// pretending otherwise would mean a range that means nothing.
const LedField kFields[] = {
    // The strip.
    {"totalLeds", &LedFeel::totalLeds, 1, kLedStripCapacity},

    // The travel.
    {"travelMs", &LedFeel::travelMs, 0, 60000},
    {"travelPeakWidth", &LedFeel::travelPeakWidth, 1, 64},
    {"travelSparkLeds", &LedFeel::travelSparkLeds, 1, 64},

    // The knob.
    {"detentsPerStep", &LedFeel::detentsPerStep, 1, 64},
    {"fastDetentsPerStep", &LedFeel::fastDetentsPerStep, 1, 64},
    {"fastSpinWindowMs", &LedFeel::fastSpinWindowMs, 0, 60000},
    {"settleLockoutMs", &LedFeel::settleLockoutMs, 0, 60000},

    // The progression run.
    {"fillMinLeds", &LedFeel::fillMinLeds, 0, 512},
    {"fillRetreatDelayMs", &LedFeel::fillRetreatDelayMs, 0, 60000},
    {"fillRetreatStepMs", &LedFeel::fillRetreatStepMs, 0, 60000},
    {"blobWidth", &LedFeel::blobWidth, 1, 512},

    // The preview pulse.
    {"pulseMs", &LedFeel::pulseMs, 1, 60000},
    {"pulseMinPct", &LedFeel::pulseMinPct, 0, 100},
    {"pulseMaxPct", &LedFeel::pulseMaxPct, 0, 100},

    // Brightness, as a percentage of the role's own colour.
    {"abovePct", &LedFeel::abovePct, 0, 100},
    {"selfPct", &LedFeel::selfPct, 0, 100},
    {"dimPct", &LedFeel::dimPct, 0, 100},
    {"fillPct", &LedFeel::fillPct, 0, 100},
    {"blobPct", &LedFeel::blobPct, 0, 100},
    {"browseFromPct", &LedFeel::browseFromPct, 0, 100},
    {"browseToPct", &LedFeel::browseToPct, 0, 100},

    // The commit, in two halves.
    {"explodeMs", &LedFeel::explodeMs, 0, 60000},
    {"igniteMs", &LedFeel::igniteMs, 0, 60000},

    // The ring.
    {"ringIdleMs", &LedFeel::ringIdleMs, 0, 600000},
    {"ringFlashMs", &LedFeel::ringFlashMs, 0, 60000},
    {"ringOffDelayMs", &LedFeel::ringOffDelayMs, 0, 60000},
    {"ringFadeMs", &LedFeel::ringFadeMs, 0, 60000},

    // The strip as a whole.
    {"frameIntervalMs", &LedFeel::frameIntervalMs, 1, 100},
};

const int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

}  // namespace

const LedField* ledFields(int* count) {
    if (count != nullptr) {
        *count = kFieldCount;
    }
    return kFields;
}

const LedField* findLedField(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < kFieldCount; ++i) {
        const char* candidate = kFields[i].key;
        const char* a = candidate;
        const char* b = key;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') {
            return &kFields[i];
        }
    }
    return nullptr;
}

int ledFeelGet(const LedFeel& f, const char* key, int fallback) {
    const LedField* field = findLedField(key);
    return field != nullptr ? f.*(field->member) : fallback;
}

bool ledFeelSet(LedFeel* f, const char* key, int value, bool* clamped) {
    if (f == nullptr) {
        return false;
    }
    const LedField* field = findLedField(key);
    if (field == nullptr) {
        return false;
    }
    int next = value;
    if (next < field->lo) {
        next = field->lo;
    }
    if (next > field->hi) {
        next = field->hi;
    }
    if (clamped != nullptr) {
        *clamped = (next != value);
    }
    f->*(field->member) = next;
    return true;
}

}  // namespace retroroom_core
