# LED string — browse blob, preview pulse, selection twinkle

**Status:** done
**File anchor:** `lib/LedStringPaint/`, `src/ledstring.{h,cpp}`,
`src/controls.{h,cpp}`, `src/consoles.cpp`, `src/configuration.h`,
`test/test_ledstring_browse/`
**Completed:** 2026-09-29
**Commits:** `d5ad7ce` (functional core + host tests), `2b28560`
(shell wiring), `c76a666` (frame-level core extraction + simulator)
**Closes:** S1 of the former
`2026-09-25_led-string-light-shows_DRAFT.md`, rewritten into real
todos when that draft was retired.

## What

The rotary browse used to move one console per detent, and the only
feedback about which console you were heading for was a line of serial
text. Three behaviours replace that:

1. **Gated browse.** A console step takes
   `LEDSTRING_DETENTS_PER_STEP` detents (5). A blob creeps along the
   strip from the current console toward the next as they accumulate.
   The final detent snaps the blob onto the target and starts a
   pulsating glow on that console's `ledPosition` / `ledWidth` window —
   the same window the old static paint used, now showing *what a click
   would select* before you click.
2. **Fast-spin escalation.** A detent arriving within
   `LEDSTRING_FAST_SPIN_WINDOW_MS` of the previous one drops the
   requirement to `LEDSTRING_FAST_DETENTS_PER_STEP` (2), so running
   through the list stays quick.
3. **Selection effect.** Committing twinkles the whole strip, then
   collapses it in well under a second to only the pixels above the
   selected console. The resting state is now the stack lit down to the
   selection rather than the selection's own window alone.

## Why

The knob gave no visual indication of *where* a click would land. The
only way to know was to spin and read the serial log, which is not
available to someone standing in front of the cabinet. The cost of the
old behaviour was also asymmetric: browsing was free but blind, and
committing was instantaneous and irreversible-ish (it drives the
StackSelector latch and blaster codes the TV).

Gating the browse trades a little speed for being able to see the
target before committing, and the fast-spin escalation means the cost is
only paid by operators who are actually being deliberate.

## How

### The functional core owns the decisions

`lib/LedStringPaint` grew a `DetentGate` class plus blob / pulse /
selection-effect math, all pure and host-tested
(`test/test_ledstring_browse/`). `src/ledstring.cpp` only turns those
decisions into CRGB pixels, which is what keeps the host build free of
FastLED.

The gate tracks a **continuous position** in permille of one console
step, not a detent count. That was the key design decision: with a
counter, turning the knob back mid-transit either snaps to the other
side of the anchor or requires a separate "undo" state. With a
position, reversing simply decrements it and the blob walks back along
the path it came. The target console is derived from the *sign of the
position*, not the sign of the detent, which is what makes that work —
turning back while still out on the forward side keeps aiming at the
forward console while the blob retreats toward the anchor.

The division remainder of `1000 / detentsPerStep` is carried, so a
threshold that doesn't divide 1000 evenly (3, 6, 7) still completes in
exactly that many detents instead of rounding down and never arriving.
`test_thresholds_that_do_not_divide_1000_still_arrive` pins this.

### Three indices, not one

The shell now distinguishes:

| name | meaning |
|---|---|
| `currentConsoleIndex` | the live selection (consoles.cpp) |
| `browsedConsoleIndex` | the console being *approached* |
| `browseAnchorIndex` | the console the blob *departs from* — the last one the browse snapped onto |

`browseAnchorIndex` is static in `controls.cpp` and is seeded from
`currentConsoleIndex` on the first detent after boot, so a boot that
restores a saved selection browses relative to the live console.

### The strip has four states

`src/ledstring.cpp` runs a small mode machine pumped from
`main.cpp::loop()`'s `ledstring_loop()`: `RESTING`, `TRANSIT`,
`PREVIEW`, `SELECTING`. `ledstring_loop()` returns immediately when
resting, so the idle cost is one comparison and no PIO traffic.

### Commit is the single reset point

`controls_browseReset()` is called from `selectConsole()` in
`consoles.cpp`, not from each caller. That is deliberate: every commit
path (rotary click, NEXT/PREV buttons, `/next`, `/prev`, WebSocket)
funnels through `selectConsole()`, and a leftover detent count would
make the operator's next turn start a step from the wrong place. The
call sits outside the `HAS_LEDS` guard even though it resets the LED
string, because the gate lives in `controls.cpp` regardless of whether
the second strip is wired.

### The shell is now almost empty

`retroroom_core::computeStripFrame()` resolves a whole frame — which
pixels, at what brightness — from a `StripFrame` the caller fills in.
`src/ledstring.cpp` is left with percentage-to-CRGB and
`FastLED.show()`, plus the four-state machine. That was done *after* the
first working version, which had the paint logic in the shell: the
simulator would otherwise have needed a second copy of it, and a second
copy of animation logic is one nobody ever runs.

## Bugs found before the firmware ever ran them

All three were found by the host tests and the simulator rather than on
the bench, which is the argument for the split:

- **`targetDirection` was computed before the position updated.** Walking
  back to exactly the anchor left the target pointing at the console you
  had just come from. The target has to be re-derived *after* the
  position moves — it is a property of the position, not of the detent.
- **The settle used one flat brightness level.** The whole surviving
  prefix settled at console brightness and then visibly dimmed the
  instant the effect handed back to the resting paint — a flash at
  exactly the moment the operator looks at the result. Fixed by making
  the settle three-tier, keyed on the console window's *start*, so the
  effect's last frame is pixel-identical to the resting paint. Asserted
  by `test_selecting_frame_ends_on_the_resting_paint`.
- **The blob froze when travelling backwards.** The frame clamped the
  signed fraction to `[0, 1000]`, which eased every negative position to
  zero and parked the blob on the anchor for the whole backward half of
  a step. The travel wants the *magnitude*; the sign has already been
  consumed by the caller choosing the target window. No blob unit test
  caught this because they all pass a non-negative fraction — it took
  the simulator's reverse scenario.

## Tuning

Every value is a `#define` in `src/configuration.h` under the "LED
string (GP21) browse + selection feel" block, grouped by which of the
three behaviours it shapes. Nothing in the logic hard-codes a timing.

**Change a value, then run `./agent-script/ledstring-sim.sh`.** It
replays the same `computeStripFrame()` call the firmware makes with the
same values and prints each frame as an ASCII strip, so the effect can
be seen without a flash. `browse`, `fastspin`, `reverse`, `select` and
`frames` are the scenarios; `all` runs them.

Two values are judgement calls rather than numbers:

- `LEDSTRING_KEEP_INCLUDES_SELECTED` — whether the resting prefix
  includes the selected console's own window. Defaults to 1; setting it
  to 0 takes the literal reading of "only the ones above the selected
  console", which turns the thing you just chose off and reads as a
  glitch. It also has to gate the console's *own* fill, not just the
  prefix length, or the define is a lie —
  `test_resting_frame_honours_the_exclusive_configuration` pins that.
- `LEDSTRING_ABOVE_PCT` / `LEDSTRING_SELF_PCT` — the resting state is
  the console window *and* everything above it, and those overlap. The
  brighter self fill paints over the dim prefix fill, so the selection
  is the brightest thing on the strip.

## Open hooks

- The strip is indexed top-of-cabinet downward, which is what makes
  "above the selected console" mean the lower pixel indices. If the
  physical wiring turns out to number the other way, `computeKeepEnd()`
  and `paintResting()` are the two places to change.
- The preview pulse and the selection effect are independent; neither
  reads the other's state.
- Nothing in the blink-of-an-eye path blocks. `selectConsole()` still
  drives the StackSelector latch and the IR, and the animation is
  picked up by `ledstring_loop()` afterwards.
