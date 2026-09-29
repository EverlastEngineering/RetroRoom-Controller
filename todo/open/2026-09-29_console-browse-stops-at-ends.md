# Console browse must stop at the ends, not wrap

**Status:** open
**File anchor:** `lib/ConsoleConfig/src/ConsoleConfig.{h,cpp}`,
`src/consoles.cpp`, `src/controls.cpp`
**Created:** 2026-09-29

## What

Reaching either end of the console list and continuing in that
direction should do nothing. It should not wrap around to the far end
of the list.

Currently `retroroom_core::wraparoundNext()` wraps at both ends, and
every step path goes through it:

| Call site | Trigger |
|---|---|
| `advanceConsole()` in [src/consoles.cpp](../../src/consoles.cpp) | NEXT button, `GET /next`, WebSocket `next` |
| `rewindConsole()` in [src/consoles.cpp](../../src/consoles.cpp) | PREV button, `GET /prev`, WebSocket `prev` |
| `rotaryEncoderTick()` in [src/controls.cpp](../../src/controls.cpp) | the gated rotary browse, resolving the target console |

Reported on the bench 2026-09-29: spinning the knob past the last
console silently lands you back on the first, which is disorienting —
the operator is turning *forward*, past the end of the cabinet, and
gets a console on the other side of the room.

## Why

The list is the cabinet. The first and last consoles are the physical
ends of it, and continuing past them is not "next" — there is nothing
there. Wrapping also makes the LED animations lie: the strip would have
to sweep a block of light the whole length of the cabinet, around and
back, to represent a step that is a couple of LEDs away in the wiring.

## How

- Add a clamping counterpart to `wraparoundNext()` in
  `lib/ConsoleConfig`. Keep `wraparoundNext()` in the tree only if
  something still genuinely wants wraparound — otherwise delete it
  rather than leaving a policy function nothing calls.
- `clampIndex()` already exists in the same file and clamps a
  persisted index. It is not quite the right shape here (it clamps a
  *value into range*, not a *step in a direction*), so this is a new
  function rather than a reuse — but they should agree about the empty
  and out-of-range cases, so they probably belong next to each other
  with a comment saying so.
- Change the three call sites above. They all want the same thing, so
  this is one decision applied three times, not three decisions.
- The rotary path needs more than the swap. Today the detent gate
  accumulates progress and, on the final detent, moves the cursor to
  whatever the target resolved to. At the end of the list the target
  stops moving, so the gate needs to decide what a detent *at* the end
  means. See the open question below.
- `advanceConsole()` / `rewindConsole()` should return without doing
  anything at the end — no `selectConsole()`, no WebSocket broadcast,
  no `selectedAtUptimeMs` stamp. A caller that is already at the last
  console and gets the end is a no-op request, and the e2e harness
  asserts on `selectedAtUptimeMs` changing, so stamping it on a no-op
  would make a stuck device look alive.
- Cover it in `test_console_config` for the pure function, and extend
  the browse tests in `test_ledstring_browse` for the gate.

## Open question — what does a detent at the end *do*?

Two reasonable behaviours, and they feel different:

1. **Refuse the step.** The gate does not start accumulating in that
   direction at all. The operator turns and nothing happens, which is
   honest but silent — there is no "you've hit the end" signal.
2. **Fill the gate, then decline.** The detents still register and the
   progression indicator still fills, but the 5th detent resolves to
   "stay here". This keeps the 4-detent feedback consistent and gives
   the operator a clear "that one is as far as this goes" at the end
   of it, at the cost of spending detents on a step that cannot
   happen.

Either is defensible. What is not defensible is leaving it accidental:
if the gate keeps filling and the target clamps, the operator watches
a full progression animate and then nothing happens, which reads as a
broken strip rather than an end of list.

**Default if nobody decides:** (1), plus a brief dim pulse on the
current console's window as the end-of-list acknowledgement. It is the
smaller change and the failure mode is quiet rather than confusing.

## Notes

- Not urgent on its own; it was found while designing the browse
  animation, where a wrap would also mean an animation that is wrong
  rather than merely surprising. Fold it into that work.
- The console list is rebuilt from `/consoles.json` at boot, so "the
  end" moves if the config is replaced. Nothing here should assume a
  fixed count.
