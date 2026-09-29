# LED string — 6-segment wiring diagram + LED density

**Status:** open
**File anchor:** `plans/` (deliverable), `example-configurations/`
(input to check against)
**Created:** 2026-09-29, split out of the retired
`2026-09-25_led-string-light-shows_DRAFT.md` (its S3).

## What

Produce a wiring diagram for the GP21 string's physical layout, plus the
per-segment LED count at both 30 and 60 LED/m density, and verify the
`ledPosition` / `ledWidth` ranges in the example configs actually
project onto those segments.

The layout being described, from the original draft: a two-column ×
three-string serpentine, traversed

```
right column bottom  R -> L
right column middle  L -> R
right column top     R -> L
left  column top     L -> R
left  column middle  R -> L
left  column bottom  L -> R
```

Deliverable is a diagram (Mermaid or ASCII) with explicit pixel-index
ranges per segment, not code. It lives in `plans/` once drafted, never
in `src/`.

## Why

The firmware treats the strip as a flat array and computes "above the
selected console" as the lower pixel indices. That assumption is
unverified against the physical layout. The two done items that built
the browse blob and the selection settle both depend on it — if the
string is actually numbered bottom-up, "above" is wrong and the resting
paint lights the wrong side of the cabinet.

## How

- Sketch the diagram and check it against `example-configurations/`.
  The MAME entry (`ledPosition: 27, ledWidth: 15`) is the widest
  window and should land inside a single segment.
- Record the segment order and ranges in the diagram itself, and state
  the traversal direction explicitly for each.
- Note the 30 vs 60 LED/m density for each segment length so the ranges
  can be re-derived if a segment is respooled.
- If the traversal does **not** go top-down, raise it before building
  anything on top of the current assumption — it changes
  `computeKeepEnd()` and `paintResting()`, and re-orienting it later
  means re-tuning the "feel" numbers in `src/configuration.h`.

## Notes

- No hardware needed; can be done entirely from the example configs.
- The LED string is 64 pixels
  (`NUM_SELECTED_CONSOLE_LED_STRING_LEDS`).
- Blocked on nothing. Independent of the e2e readback todo.
