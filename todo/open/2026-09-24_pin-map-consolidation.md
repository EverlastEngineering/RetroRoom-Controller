# Pin-map consolidation — pin-map-chart.md is the source of truth

**Status:** open
**Branch:** session/pico-2-wireless
**File anchor:** [pin-map-chart.md](../../pin-map-chart.md), [pico-pin-mapping.md](../../pico-pin-mapping.md), [pin-map-plan.md](../../pin-map-plan.md), [src/configuration.h](../../src/configuration.h)
**Related docs:** [todo/open/2026-09-24_i2c-lcd-console-name.md](./2026-09-24_i2c-lcd-console-name.md), [todo/open/2026-09-24_console-config-lcd-fields.md](./2026-09-24_console-config-lcd-fields.md), [todo/deferred/2026-09-12_i2c-oled-expansion.md](../deferred/2026-09-12_i2c-oled-expansion.md), [todo/deferred/2026-09-12_stackselector-perfboard-pinmap.md](../deferred/2026-09-12_stackselector-perfboard-pinmap.md), [todo/deferred/2026-09-12_touch-sensor-pullup.md](../deferred/2026-09-12_touch-sensor-pullup.md)

## What
Three pin-map files currently coexist with overlapping and stale
content:

| File | Status | Notes |
|---|---|---|
| [pin-map-chart.md](../../pin-map-chart.md) | **Authoritative** (this commit's intent) | Compact per-pin table; treats GP5 as free for I2C0 SCL; matches `pin-map-plan.md`'s migrated state. |
| [pico-pin-mapping.md](../../pico-pin-mapping.md) | Stale | Narrative doc. Says `TOUCH_SENSOR_PIN` is GP12 (correct) but §4 says "External 10 kΩ pull-up on `TOUCH_SENSOR_PIN` (GP5)" — wrong; touch moved off GP5 in the plan. Also says I2C0 = GP4/GP5 "fully free" — correct, but the file's role table is a long narrative that's drifted from the chart. |
| [pin-map-plan.md](../../pin-map-plan.md) | Stale | A migration plan from an earlier state to the chart. The migration already landed in `src/configuration.h` (the defines match the plan's table). The plan doc itself is now historical. |

Goal: delete the two stale files, promote the chart with a
"source of truth" callout, and rewire every reference in the repo.

## Why
Three sources of truth means three ways to be wrong. New contributors
don't know which to read first; the LCD bring-up item
([todo/open/2026-09-24_i2c-lcd-console-name.md](./2026-09-24_i2c-lcd-console-name.md))
explicitly needs the GP4/GP5 = I2C0 story locked down so the firmware
can wire it up cleanly.

## How

### 1. Promote `pin-map-chart.md`
Add a header banner immediately under the title:

```markdown
> **Source of truth.** This file is the single, authoritative pin
> assignment table for the RetroRoom-Controller firmware on the
> Raspberry Pi Pico / Pico-W / Pico 2 W. `src/configuration.h`'s
> `#define`s MUST match this table; any change to one MUST be
> reflected in the other in the same commit.
>
> Older narrative docs (`pico-pin-mapping.md`, `pin-map-plan.md`)
> were removed on 2026-09-24 — see
> `todo/done/2026-09-24_pin-map-consolidation.md` for the deletion
> commit.
```

### 2. Delete
- `pico-pin-mapping.md`
- `pin-map-plan.md`

Both files are git-tracked; deletion is `git rm`.

### 3. Rewire references in surviving files
Every doc or comment that referenced the deleted files gets a
one-line substitution:

| File | Old | New |
|---|---|---|
| [src/configuration.h](../../src/configuration.h) (Pico block comment, ~line 41) | "See `pin-map-chart.md` / `pin-map-plan.md` for the reshuffle rationale and `Preserved peripheral blocks` below for the inventory." | "See [pin-map-chart.md](../../pin-map-chart.md) for the authoritative pin table and reshuffle rationale." |
| [src/configuration.h](../../src/configuration.h) (Pico block comment, ~line 38) | "see [env:pico2w] in platformio.ini and todo/open/2026-09-21_pico-2-w-platform.md for the CYW43 / webserver bring-up." | (unchanged — not a pin-map reference) |
| [todo/deferred/2026-09-12_i2c-oled-expansion.md](../deferred/2026-09-12_i2c-oled-expansion.md) | "[pico-pin-mapping.md](../../pico-pin-mapping.md) Section 2 -- I2C pins reserved for this." | "[pin-map-chart.md](../../pin-map-chart.md) -- I2C pins reserved for this." |
| [todo/deferred/2026-09-12_stackselector-perfboard-pinmap.md](../deferred/2026-09-12_stackselector-perfboard-pinmap.md) | "[pico-pin-mapping.md](../../pico-pin-mapping.md) Section 5" | "[pin-map-chart.md](../../pin-map-chart.md)" |
| [todo/deferred/2026-09-12_touch-sensor-pullup.md](../deferred/2026-09-12_touch-sensor-pullup.md) | "GP5 and 3.3V... (PCB reminder)" | Rewrite — `TOUCH_SENSOR_PIN` moved to GP12 in the plan; the GP5 pull-up note is stale. Drop the file's whole premise or rewrite as "external 10 kΩ pull-up on `TOUCH_SENSOR_PIN` (GP12)". Likely rewrite + move back to `deferred/` with the corrected pin. |
| New `todo/open/2026-09-24_i2c-lcd-console-name.md` | (will reference `pin-map-chart.md` from the start) | (already correct) |
| New `todo/open/2026-09-24_console-config-lcd-fields.md` | (no pin-map reference needed) | (n/a) |

### 4. Touch-sensor pull-up item rewrite
`todo/deferred/2026-09-12_touch-sensor-pullup.md` is now factually
wrong (the plan moved `TOUCH_SENSOR_PIN` off GP5 to GP12; the chart
treats GP5 as plain I2C0 SCL). Options:

- **Rewrite the item** to say "external 10 kΩ pull-up on
  `TOUCH_SENSOR_PIN` (GP12)" — preserves the PCB reminder, fixes
  the pin. Stays in `deferred/` (still blocked on perfboard).
- **Delete the item** — the 10 kΩ requirement is now visible in the
  consolidated chart and any future perfboard schematic review.

Recommend rewrite — the operator shouldn't have to remember the
plan moved touch off GP5.

### 5. Verification
- `grep -r 'pico-pin-mapping\|pin-map-plan' --include='*.md' --include='*.h' --include='*.cpp' .`
  must return zero hits (modulo `pin-map-chart.md`'s own content if it
  ever cross-references the old files; it shouldn't).
- `pio run -e pico2w` green (no code path changes; this is a doc-only
  commit).
- `pio test -e test_native` green (no code path changes).

## Open questions
- Should the touch-sensor pull-up item be **rewritten** or
  **deleted**? (Default: rewrite — see §4 above.)
- Should `pin-map-chart.md` get expanded to include the narrative
  content that lived in `pico-pin-mapping.md` (per-role name table,
  wiring notes, KiCad net list)? Default: **no** — the chart is the
  source of truth for the *pin assignments*, not for the narrative
  per-role documentation. The per-role table is already in
  [pico-pin-mapping.md §1](pico-pin-mapping.md), but that's the
  file we're deleting. Move the per-role table to a new
  `pin-roles.md` if it needs to survive; otherwise let it go (the
  defines in `src/configuration.h` are self-documenting via comments).

  Recommend: **let the per-role table go**. The `#define`s in
  `src/configuration.h` already have inline comments explaining each
  role. A separate narrative file would just drift.

## Out of scope (separate todos)
- [todo/open/2026-09-24_i2c-lcd-console-name.md](./2026-09-24_i2c-lcd-console-name.md) — LCD driver.
- [todo/open/2026-09-24_console-config-lcd-fields.md](./2026-09-24_console-config-lcd-fields.md) — JSON schema change + example-config backfill.

## Related
- [pin-map-chart.md](../../pin-map-chart.md) — the file this commit promotes.
- [LOG.md](../../LOG.md) — add an entry under the latest explaining the consolidation.
