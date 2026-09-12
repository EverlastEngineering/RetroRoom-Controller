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
- **Completed items:** rename to `<sha>_<short-name>.md` where `<sha>`
  is the 7-char short hash that completes the work. Example:
  `643be7c_ir-restore-via-z3t0-IRremote-4-x-and-MANUAL-OE-GP12.md`.
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
- `cyw43-picow-wifi` — Pico-W WiFi, anchored on `src/network.{h,cpp}`
- `stackselector-perfboard-pinmap` — confirm GP8/9/10 ARM/CYCLE/ENABLE on the perfboard
- `5-second-boot-fade` — single-pixel "I'm alive" pulse at boot

### deferred/
- `stackselector-daisy-chain` — only matters if the perfboard has >1 module
- `i2c-oled-expansion` — SSD1306 on I2C0/1 when the perfboard lands
- `touch-sensor-pullup` — external 10 kΩ pull-up on GP5 (PCB reminder)

### done/
- `c6005c7` — USR button advances `currentConsoleIndex`
- `b603d4b` — pinout doc
- `41c73e5` — `TODO.md` update bookkeeping
- `643be7c` — IR restore via `z3t0/IRremote@^4.7.1` + `MANUAL_OE_PIN = GP12`
