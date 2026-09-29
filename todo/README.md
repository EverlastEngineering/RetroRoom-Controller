# TODO — RetroRoom-Controller

Session-scoped tracker for open work on `session/merge-pico-json` (and
future branches off it). Each item is its own file in this directory.
When you complete an item, `git mv` it to `done/` and rename it to
include the commit SHA. The `LOG.md` is the audit trail of decisions;
this folder is the live task list.

## Layout

```
todo/
  README.md                       (this file)
  open/                           (act now)
    YYYY-MM-DD_<short-name>.md    ← filename pattern for an open item
    YYYY-MM-DD_<short-name>_DRAFT.md  ← holding pen for a sub-item list;
                                           requires breakdown before work begins
  deferred/                       (waiting on the perfboard / external dependency)
    YYYY-MM-DD_<short-name>.md
  done/                           (completed)
    <sha>_<short-name>.md         ← filename includes the commit SHA
```

## Conventions

### Naming
- **New items:** `YYYY-MM-DD_<short-name>.md` where `<short-name>` is
  lowercase, dash-separated, and <= 50 chars. Example:
  `2026-09-12_cyw43-picow-wifi.md`.
- **Drafts (sub-item holding pen):** `YYYY-MM-DD_<short-name>_DRAFT.md`.
  Holds a breakdown checklist for sub-items carved out of a parent
  todo. **Not actionable** — first line must say `**Status:** draft
  (needs breakdown — do not start work from this file)`. Do not list
  in the `### open/` inventory block (it isn't open). When the parent
  work closes, break into per-sub-item todos and `git rm` the draft.
  Example: `2026-09-25_led-string-light-shows_DRAFT.md`.
- **Completed items:** rename to `<sha>_<short-name>.md` where `<sha>`
  is the 7-char short hash that completes the work. Example:
  `643be7c_ir-restore-via-z3t0-IRremote-4-x-and-MANUAL-OE-GP12.md`.
  A handful of older items still carry their `YYYY-MM-DD_` prefix
  because they were closed before this convention was adopted; leave
  them alone rather than churning history for cosmetics.
- The SHA in the filename is the completion receipt: `git log --follow
  done/<sha>_<short-name>.md` shows the rename commit + the original
  add.

### Filing
- Header in each item: `**Status:**` (open / done / deferred),
  `**File anchor:**` (relative path to the source files that will /
  did change), optional `**Completed:**` (YYYY-MM-DD) and a commit SHA.
- Each item has a self-contained story: "What", "Why", "How". When
  closing an item, update its body to record the actual implementation
  in addition to the headline "what" — git diff then shows the editor
  thinking.

### Completion ritual
1. Land the code commit (item's work merged).
2. Edit the open file's body to record the actual implementation
   (links to the commit SHA, decisions made along the way).
3. `git mv todo/open/<old-name>.md todo/done/<sha>_<short-name>.md`
   and commit the rename with the message "todo: <name> done in <sha>"
   (or use the same commit as the implementation if clean).
4. Optionally update [LOG.md](../../LOG.md) under the latest entry to
   cross-reference the now-done item.

## Why per-file instead of one TODO.md
The single-file approach drifted: items got duplicated across edits,
done items were forgotten and re-opened, stale descriptions competed
with current ones. Per-file gives:
- **Clean git history** — `git log --follow done/<sha>_*.md`
  shows the full history of an item in one place, including the
  completion commit.
- **No edits needed on close** — just `git mv` and rename. The
  original problem statement stays intact for posterity.
- **Impossible to lose items** — each lives in its own file; new
  sessions can scan `open/` and `deferred/` directories to know what
  outstanding work exists without parsing an arbitrary section.
- **Filterable in editors** — list all `*.md` under `open/` to see
  just the active work.

## Current inventory

### open/
- `2026-09-25_led-string-wiring-diagram` — S3 of the old DRAFT: the 6-segment serpentine wiring diagram and the per-segment LED count at 30/60 LED/m. Document only, no code.
- `2026-09-25_led-string-e2e-readback` — S4 of the old DRAFT: `/leds.json` snapshot endpoint so the browse/selection frames can be asserted from the host. Blocked on deciding whether the endpoint is worth it now the animations are in.

### deferred/
- `stackselector-daisy-chain` — only matters if the perfboard has >1 module
- `i2c-oled-expansion` — SSD1306 on I2C0/1 when the perfboard lands
- `touch-sensor-pullup` — external 10 kΩ pull-up on GP5 (PCB reminder)
- `stackselector-perfboard-pinmap` — confirm GP8/9/10 ARM/CYCLE/ENABLE on the perfboard

### done/
- `2b28560` — browse + selection animation on the GP21 string: N detents per console with a travelling blob, a pulsing preview of the target console, fast-spin escalation, and the twinkle-then-settle on commit. Closes S1 of `2026-09-25_led-string-light-shows_DRAFT.md`.
- `f93927b` — ring highlight lifecycle: `lightSingle()` is the single choke point and always arms an idle deadline, so the ring can no longer be left lit with nothing to turn it off. `lighting_loop()` owns the fade and returns on completion so the browsed cursor reverts. Split `browsedConsoleIndex` from `currentConsoleIndex` so the knob is browse-only and the click commits. Prerequisite for the GP21 twinkle in `2026-09-25_led-string-light-shows_DRAFT.md` S1.
- `9c1304b` — second-strip LED driver on GP21 (`src/ledstring.{h,cpp}` + `lib/LedStringPaint`); lights `[ledPosition, ledPosition+ledWidth)` from `selectConsole()`. Companion draft `2026-09-25_led-string-light-shows_DRAFT.md` parks the pulled-out sub-items (animation, shows, wiring diagram, e2e readback).
- `c6005c7` — USR button advances `currentConsoleIndex`
- `b603d4b` — pinout doc
- `41c73e5` — `TODO.md` update bookkeeping
- `643be7c` — IR restore via `z3t0/IRremote@^4.7.1` + `MANUAL_OE_PIN = GP12`
- `ca03bc0` — `pin-map-chart.md` promoted to source of truth; `pico-pin-mapping.md` + `pin-map-plan.md` deleted; references rewired
- `6130cac` — hardware next/prev console buttons via EasyButton
- `51cd125` — per-console `tagline` + top-level `lcd.backlightOffAfterMs` schema; example configs backfilled
- `2aadbd7` — 16×2 I2C LCD driver (`enjoyneering/LiquidCrystal_I2C@^1.4.0` on GP4/GP5)
- `e16d447` — close-out for the LCD pair (`51cd125` + `2aadbd7`)
- `2026-09-22_drop-non-pico2w-envs` — single-target `platformio.ini` ([env:pico2w] only)
- `2026-09-21_pico-2-w-platform` — Pico 2 W bring-up + CYW43 webserver
- `2026-09-22_consoles-json-upload-endpoint` — POST/GET `/consoles.json` + TDD harness
- `2026-09-22_openapi-spec-and-swagger-ui` — `/openapi` + `/openapi.yaml` live
- `2026-09-12_cyw43-picow-wifi` — superseded by `2026-09-21_pico-2-w-platform`
- `2026-09-12_5-second-boot-fade` — closed without merging; LED_BUILTIN heartbeat + YD lightCycle cover the intent
