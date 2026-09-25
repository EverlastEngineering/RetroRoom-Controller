# Console config — add `tagline` per console + `lcd` block

**Status:** open
**Branch:** session/pico-2-wireless
**File anchor:** [lib/ConsoleConfig/src/ConsoleConfig.h](../../lib/ConsoleConfig/src/ConsoleConfig.h), [lib/ConsoleConfig/src/ConsoleConfig.cpp](../../lib/ConsoleConfig/src/ConsoleConfig.cpp), [src/consoleconfig_store.cpp](../../src/consoleconfig_store.cpp), [src/consoles.cpp](../../src/consoles.cpp), [src/Console.h](../../src/Console.h), [src/Console.cpp](../../src/Console.cpp), [example-configurations/example1.json](../../example-configurations/example1.json), [example-configurations/example2.json](../../example-configurations/example2.json), [test/test_console_config/test_console_config.cpp](../../test/test_console_config/test_console_config.cpp), [test/test_console_store/test_console_store.cpp](../../test/test_console_store/test_console_store.cpp)
**Related doc:** [tag-lines.md](../../tag-lines.md), [todo/open/2026-09-24_i2c-lcd-console-name.md](./2026-09-24_i2c-lcd-console-name.md)

## What
Two changes to the `consoles.json` schema (one per-console field, one
top-level block) so the LCD driver has the data it needs:

1. **Per-console `tagline` field (optional).** Each entry in the
   `consoles` array gains an optional `tagline` string. Default:
   empty string (LCD driver handles the empty case — line 2 blank
   or shows a static placeholder).
2. **Top-level `lcd` block (optional).** New object with
   `backlightOffAfterMs` (integer ms). Default if the block is
   absent: 30000 ms (30 s). Default if the block is present but the
   field is missing: 30000 ms.

Plus the example configurations (`example1.json`, `example2.json`)
get backfilled with the taglines from [tag-lines.md](../../tag-lines.md)
verbatim.

## Why
The LCD driver ([todo/open/2026-09-24_i2c-lcd-console-name.md](./2026-09-24_i2c-lcd-console-name.md))
needs the `tagline` field to render line 2 and the `lcd.backlightOffAfterMs`
field to know when to dim the backlight. Both belong in the JSON
config (operator-editable, survives reboots, lives in the existing
LittleFS `/consoles.json`) rather than as `#define`s in `src/configuration.h`.

## How

### 1. Functional core (`lib/ConsoleConfig`)
- **`Console` struct** (in `lib/ConsoleConfig/src/ConsoleConfig.h`)
  gains `std::string tagline;` field with a default of `""`.
- **`validateConsoleConfigJson`** (in `lib/ConsoleConfig/src/ConsoleConfig.cpp`)
  gains parsing for `c["tagline"]` (optional string) and a new
  top-level `lcd` object containing `backlightOffAfterMs` (optional
  integer, default 30000). No validation errors on either absence —
  both fields are purely additive. Bounds-check `backlightOffAfterMs`
  to a sane range (e.g. 0 = never-off, max 600000 = 10 min) and warn
  but accept out-of-range values.
- **`ConsoleConfig` struct** (in `lib/ConsoleConfig/src/ConsoleConfig.h`)
  gains `uint32_t lcdBacklightOffAfterMs;` field, default 30000.
  Populated by `validateConsoleConfigJson` from the parsed JSON or
  the default.

### 2. Shell-side (`src/`)
- **`src/Console.h`** — mirror the new `tagline` field on the shell
  `Console` struct (or, if the shell `Console` is just an alias /
  thin wrapper for the core `Console`, pass it through directly).
- **`src/consoles.cpp`** — `CurrentConsole()` and `selectConsole()`
  already expose `c.name`; add `c.tagline` access for the LCD
  driver. No behavior change.
- **`src/consoleconfig_store.cpp`** — `loadLiveConsoleConfig` already
  reads the JSON into a `retroroom_core::ConsoleConfig`. After
  load, expose `lcdBacklightOffAfterMs` to the LCD driver via a new
  getter (e.g. `retroroom_store::getLcdBacklightOffAfterMs()`).

### 3. Host tests
- **`test/test_console_config/test_console_config.cpp`** — add 2-3
  tests:
  - Parse a config where `tagline` is present on every console →
    assert the parsed values match.
  - Parse a config where `tagline` is absent → assert the field
    defaults to `""`.
  - Parse a config where `lcd` block is absent → assert
    `lcdBacklightOffAfterMs` defaults to 30000.
  - Parse a config where `lcd.backlightOffAfterMs` is present →
    assert the parsed value is honored.
- **`test/test_console_store/test_console_store.cpp`** — unchanged
  unless the existing rotation tests need to round-trip the new
  fields. Confirm by running the suite; the existing 11 tests should
  still pass.

### 4. Example configurations — backfill from `tag-lines.md`
[tag-lines.md](../../tag-lines.md) is the operator-provided tagline
table. For each console that appears in the example configs, add a
`tagline` field matching the primary slogan from the table:

| Console | Tagline (from `tag-lines.md`) |
|---|---|
| `Nintendo Entertainment System` | `Now you're playing with power!` |
| `Super Nintendo Entertainment System` | `Now you're playing with power. Super Power!` |
| `Nintendo 64` | `Get N or get out.` |
| `Nintendo Gamecube` | `Born to play.` |
| `Nintendo Wii` | `Wii would like to play.` |
| `Sega Master System` | `The challenge will always be there.` |
| `Sega Genesis` | `Genesis does what Nintendon't!` |
| `Sega Dreamcast` | `It's thinking.` |
| `TurboGrafx-16` | `Higher Energy Gaming` |
| `Sony PlayStation` | `Live in your world. Play in ours.` |
| `Sony PlayStation 2` | `Live in your world. Play in ours.` |
| `Microsoft Xbox` | `It's good to play together.` |

Apply to both `example1.json` and `example2.json`. If a console name
in an example doesn't appear in the table, leave its `tagline` absent
(the driver handles the empty case).

### 5. Verify
- `pio test -e test_native` — all tests pass (existing 35 + new 2-3).
- `pio run -e pico2w` — green (no code-path changes beyond the
  field additions).
- Live verification on the bench with
  `./agent-script/e2e-consoles-json.sh --scenario post-then-read-back`:
  POST a modified `example1.json` (with `tagline`s + `lcd` block),
  GET it back, byte-compare. The byte-compare must succeed — i.e.
  ArduinoJson's `serializeJson` must preserve the new fields
  verbatim. The firmware doesn't re-serialize on GET (per
  `todo/done/2026-09-22_consoles-json-upload-endpoint.md`), so this
  should hold without further work.

## Open questions
- **Schema versioning.** The schema is implicit (no `version` field).
  Adding `tagline` + `lcd` is backward-compatible (both optional).
  No migration needed for existing `/consoles.json` files on devices
  already in the field.
- **Multiple taglines.** Some consoles have multiple slogans in
  `tag-lines.md` (Genesis: `Genesis does what Nintendon't!` /
  `Welcome to the next level`; Xbox: `It's good to play together.` /
  `Life is short. Play more.`). The schema currently supports one
  `tagline` per console. If we want alternation, options:
  - `tagline` (single, current plan).
  - `taglines` (array, driver picks one randomly or cycles).
  - `tagline` + `taglineAlt` (two fields, driver alternates).

  Recommend: single `tagline` for now. If the operator wants
  alternation later, add `taglineAlt` as a separate schema bump.
- **`lcd` block scope.** Currently only holds `backlightOffAfterMs`.
  Future fields (contrast, brightness, custom welcome text, etc.)
  will land here. Block name `lcd` is the right prefix.

## Related
- [tag-lines.md](../../tag-lines.md) — the operator-provided tagline table.
- [todo/open/2026-09-24_i2c-lcd-console-name.md](./2026-09-24_i2c-lcd-console-name.md) — the consumer.
- [todo/open/2026-09-24_pin-map-consolidation.md](./2026-09-24_pin-map-consolidation.md) — establishes `pin-map-chart.md` as the source of truth (this todo is unaffected by that work).
- [todo/done/2026-09-22_consoles-json-upload-endpoint.md](../done/2026-09-22_consoles-json-upload-endpoint.md) — the POST/GET path the new fields will ride on.
- [example-configurations/](../../example-configurations/) — the configs being backfilled.
