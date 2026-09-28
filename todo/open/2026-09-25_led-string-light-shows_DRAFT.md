# DRAFT — LED string light shows (needs breakdown into sub-items)

**Status:** draft (needs breakdown — do **not** start work from this file)
**Branch:** session/pico-2-wireless
**Parent doc:** carve-out from a 270-line single-todo draft originally
written 2026-09-25 as `todo/open/2026-09-25_led-string-light-shows.md`
(now removed; its full content is preserved in this file's `## History`
section's references and intent).
**Sibling todo (in scope now):**
[todo/open/2026-09-25_led-string-light-control.md](./2026-09-25_led-string-light-control.md)
— the basic GP21 driver + steady-state highlight. Land *that* first.

**Created:** 2026-09-25 (as a drafting / breakdown file)
**Last touched:** 2026-09-25

---

## Purpose

This file collects the work that was carved **out** of the basic LED
strip light control item. It is *not* an open todo — the next session
should not pick it up until the sibling
[todo/open/2026-09-25_led-string-light-control.md](./2026-09-25_led-string-light-control.md)
todo closes. When that lands, this file gets broken into one or more
real todos and moved to either `todo/open/` or `todo/deferred/` per
the per-item decisions recorded below.

## Why it's a draft and not a normal open todo

The basic GP21 light control commit must land first so reviewers see
the *narrow* commit ("light the right LEDs on commit and nothing
more") before the more interesting bits (animations, shows, e2e
verification). Stuffing those into a single PR makes the diff harder
to review and inflates the risk of regressions on the steady-state
path. The carve-out is deliberate, not laziness.

## Sub-items (breakdown plan)

Each bullet below should eventually become its own todo file, named
`YYYY-MM-DD_<short-name>.md`. Decisions recorded inline:

### S1 — Knob-spin pull animation between zones

- **What:** When `currentConsoleIndex` changes (rotary turn OR
  commit), the GP21 strip animates the lit window "pulling" from
  the previous zone to the new one over ~500 ms, then snaps to
  steady state. The user described the natural cadence as "roughly
  four rotary clicks in 0.5 seconds."
- **Destination dir:** `todo/open/` (it's the headline animation;
  should land soon after the basic driver).
- **Open hooks:** Q2 from the original draft — spin re-target
  policy. Default = reset-on-each-click for first impl; switch if
  it feels wrong on the perfboard.
- **Ring:** do **not** hook the rotary-turn handler in
  [src/controls.cpp::rotaryEncoderTick](src/controls.cpp#L170-L207)
  to this animation *unless* the spin policy needs it (option (a)
  doesn't; (b) and (c) do).

### S2 — Generic light shows (rainbow chase, sparkle, breathing, etc.)

- **What:** A small set of decorative effects that ride on the same
  `selectedLeds[]` buffer and overlay (or replace) the per-console
  highlight window. State persisted in NVS / LittleFS, toggleable
  via the existing `/state.json` endpoint or a new endpoint.
- **Destination dir:** `todo/open/` after the S1 animation lands.
  Defer behind S1 because Phase B is gated on the
  per-console-window feature actually being a thing the user can
  see.
- **Open hooks:** Q5 (coexistence: pause / overlay / replace).
  Defer until the user asks for a show.

### S3 — 6-segment wiring diagram + LED density decision

- **What:** Mermaid or ASCII diagram of the two-column × three-string
  physical layout (right column bottom R→L → right column middle
  L→R → right column top R→L → left column top L→R → left column
  middle R→L → left column bottom L→R) with explicit pixel-index
  ranges per segment, plus the per-segment LED count at both 30
  and 60 LED/m density. Verify `example2.json` ranges project onto
  the right segments.
- **Destination dir:** `todo/open/`. Independent of code — could be
  done in parallel with S1/S2. **Important:** this is a *diagram*
  deliverable, not code. Lives in `plans/` once drafted, never in
  `src/`.
- **Hardware coupling:** none. Anyone can start this without
  flashing a board.

### S4 — E2E readback test for the GP21 strip

- **What:** Extend `agent-script/e2e-consoles-json.sh` (or a new
  sibling) to POST a config with explicit `ledPosition`/`ledWidth`
  per console, then read back a `/leds.json` snapshot (new
  endpoint) of the active `selectedLeds[]` framebuffer. Asserts:
  only pixels in the current console's window are non-zero at
  rest; after a simulated commit the lit window moves within
  ~500 ms (when S1 lands) or instantaneously (when this is run
  against the basic driver alone).
- **Destination dir:** `todo/open/`. Depends on a `/leds.json`
  endpoint landing first — that's its own small piece; flag it
  inline.
- **Existing readback discipline:** follow
  `plans/2026-09-22_tdd-e2e-readback.md` — byte-compare framebuf
  snapshots after `wait_for_state` rather than relying on HTTP
  response codes.

## Sub-items deliberately NOT in scope here

These were already flagged as `Out of scope` in the original draft
and stay out — do not promote them into sub-items:

- Modifying the ring driver on GP20 (off-limits to this whole line
  of work).
- `lightCycle*()` removal/gating (already a separate "should be
  removed when the perfboard lands" comment in
  [src/lighting.cpp:64-66](src/lighting.cpp#L64) — file as its own
  todo if/when it gets pulled).
- Per-LED gamma / brightness calibration.
- Music-reactive shows, DMX / E1.31 input, daisy-chained strips on
  GP21.

## How to use this file

When the sibling basic driver todo
([todo/open/2026-09-25_led-string-light-control.md](./2026-09-25_led-string-light-control.md))
closes:

1. Open a short session to break this draft into the four todos
   above (S1–S4) using `multi_replace_string_in_file` / `create_file`
   to materialize each as its own `YYYY-MM-DD_<short-name>.md`,
   copying the relevant **Sub-items** bullet content into the
   `## What` / `## Why` / `## How` body and cross-linking back to
   this draft.
2. Decide per-sub-item whether it goes to `open/` (active work) or
   `deferred/` (blocked — currently nothing here would be blocked,
   but S3 depends on a diagram decision, S4 depends on a
   `/leds.json` endpoint decision; the breakdown session should
   confirm those are not blockers before moving S3/S4 to `open/`).
3. Delete this draft file (`git rm`).
4. Update `todo/README.md`'s current-inventory block accordingly.

## History

- **2026-09-25** — Created as the single combined todo
  `2026-09-25_led-string-light-shows.md` (270 lines).
- **2026-09-25** — Carved into the focused
  `2026-09-25_led-string-light-control.md` todo + this draft file
  per user direction: "break these into separate todos. let's make
  one exclusively getting LED strip light control and remove all the
  rest of it, perhaps leaving the rest in a draft file for further
  breakdown."
