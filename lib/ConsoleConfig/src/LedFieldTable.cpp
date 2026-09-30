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

#include <cstring>

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

// ---- the lcd block ------------------------------------------------------
//
// One field today, and that is not the point. What matters is that it
// exists as a *block* in the same registry as `led`, so a menu item
// naming "lcd.backlightOffAfterMs" resolves through exactly the path
// "led.detentsPerStep" already takes, and a third block is a row here
// rather than a new special case in the parser, the setter and the
// writer.
//
// `member` is null and deliberately so: it is typed
// `int LedFeel::*` and only the `led` block is bound to a LedFeel. The
// shell resolves the others against whatever storage they actually
// have. See ConfigBlock in ConsoleConfig.h.
const LedField kLcdFields[] = {
    {"backlightOffAfterMs", nullptr, 0, 600000},
};

const int kLcdFieldCount =
    static_cast<int>(sizeof(kLcdFields) / sizeof(kLcdFields[0]));

// "led" first, always: it is the block a bare key means, and it is the
// one the parser's own readInt loop walks.
const ConfigBlock kBlocks[] = {
    {"led", kFields, kFieldCount},
    {"lcd", kLcdFields, kLcdFieldCount},
};

const int kBlockCount = static_cast<int>(sizeof(kBlocks) / sizeof(kBlocks[0]));

// The one field whose change cannot take effect without a restart.
// totalLeds is handed to FastLED.addLeds() at init, so the strip
// controller stays bound to the old length however many times the value
// is written afterwards. Everything else is read per frame, per tick or
// per call, which is the whole reason the other two "read the config
// too early" bugs this session had to be fixed at all.
bool isTotalLeds(const LedField* field) {
    return field != nullptr && field->member == &LedFeel::totalLeds;
}

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

const ConfigBlock* configBlocks(int* count) {
    if (count != nullptr) {
        *count = kBlockCount;
    }
    return kBlocks;
}

const LedField* findConfigField(const char* path, const char** blockOut) {
    if (path == nullptr || path[0] == '\0') {
        return nullptr;
    }
    if (blockOut != nullptr) {
        *blockOut = nullptr;
    }

    // Split on the first dot. No dot means the first block, which is
    // "led" -- so a config can say "detentsPerStep" as well as
    // "led.detentsPerStep", and the menu parser normalises to the
    // long form so what gets written back is always unambiguous.
    const char* dot = path;
    while (*dot != '\0' && *dot != '.') {
        ++dot;
    }
    const char* key = (*dot == '.') ? dot + 1 : path;
    const std::size_t prefixLen =
        (*dot == '.') ? static_cast<std::size_t>(dot - path) : 0;

    for (int b = 0; b < kBlockCount; ++b) {
        const ConfigBlock& block = kBlocks[b];
        if (prefixLen == 0) {
            // A bare key means the FIRST block, not "a block whose
            // prefix is the empty string" -- there is no such block, so
            // without this every bare key resolved to nothing and the
            // test caught it immediately.
            if (b != 0) {
                continue;
            }
        } else {
            const std::size_t n = std::strlen(block.prefix);
            if (n != prefixLen) {
                continue;
            }
            bool same = true;
            for (std::size_t i = 0; i < n; ++i) {
                if (block.prefix[i] != path[i]) {
                    same = false;
                    break;
                }
            }
            if (!same) {
                continue;
            }
        }
        for (int i = 0; i < block.count; ++i) {
            const char* candidate = block.fields[i].key;
            const char* a = candidate;
            const char* c = key;
            while (*a != '\0' && *a == *c) {
                ++a;
                ++c;
            }
            if (*a == '\0' && *c == '\0') {
                if (blockOut != nullptr) {
                    *blockOut = block.prefix;
                }
                return &block.fields[i];
            }
        }
        return nullptr;  // right block, wrong key
    }
    return nullptr;  // no such block
}

bool configFieldNeedsReboot(const LedField* field) {
    return isTotalLeds(field);
}

}  // namespace retroroom_core
