# Ring highlight lifecycle — turn lights it, select clears it, idle reverts

**Status:** done — commit [`f93927b`](../../) on `session/pico-2-wireless`
**Completed:** 2026-09-28
**File anchor:** [src/lighting.cpp](../../src/lighting.cpp), [src/lighting.h](../../src/lighting.h), [src/controls.cpp](../../src/controls.cpp), [src/consoles.cpp](../../src/consoles.cpp)
**Related:** [todo/open/2026-09-25_led-string-light-shows_DRAFT.md](../open/2026-09-25_led-string-light-shows_DRAFT.md) (S1, knob-spin behaviour on the GP21 string)

---

## What

Give the GP20 ring a defined lifecycle instead of a set of
uncoordinated paint calls:

1. **Turn the knob → ring lights.** So a cabinet with no capacitive
   pad still gets the affordance.
2. **Select a console (rotary click) → ring goes dark.** The ring is an
   attention cue, not a persistent selection indicator.
3. **Turn but don't select → ring fades after an idle timeout**
   (default 5 s), and the browsed cursor reverts to the selected console.
4. **`proximityActive` overrides the timeout.** A hand still at the
   knob keeps it lit indefinitely and suppresses the revert.

## Why

Found while wiring the capacitive proximity pad: `lightSingle()` is the
single paint path for every ring-on (rotary turn via
`ringLEDNext`/`ringLEDPrevious`, next/prev via
`advanceConsole`/`rewindConsole`), but it wrote `leds[]` and called
`FastLED.show()` **without touching `ringLit` or `ringFading`**. Those
two flags were only ever written inside `lightRing()`, and
`lighting_loop()` gated the fade on `ringFading`.

So turning the knob painted the ring on, left `ringFading == false`,
and nothing was left to turn it off. Repro: proximity-clear the ring,
then turn the knob — the ring lights and stays lit.

Two things were entangled, and both had to be resolved:

- **The ring's lifetime was undefined.** There was no "requested off"
  state except a fade already in flight, so any repaint mid-fade was a
  one-way trip.
- **"Browse" and "selected" were the same variable.** `rotaryEncoderTick()`
  mutated `currentConsoleIndex` on every detent and the rotary click
  just committed whatever it was sitting at. Requirement 3 had nothing
  to revert *to* until the two were split.

## Implementation

### 1. Browsed cursor split from the selected index

- `browsedConsoleIndex` + `BrowsedConsole()` in
  [src/consoles.cpp](../../src/consoles.cpp) / [src/consoles.h](../../src/consoles.h).
- `currentConsoleIndex` is now written **only** by `selectConsole()` /
  `advanceConsole()` / `rewindConsole()`.
- `rotaryEncoderTick()` moves the browsed index on each detent and no
  longer touches the selection; the knob is browse-only.
- `rotarySelectorPressed()` commits `browsedConsoleIndex` →
  `currentConsoleIndex`.
- `advanceConsole()` / `rewindConsole()` sync the browsed cursor as they
  commit, so the two never drift when selection happens by another route.
- No downstream call site needed changing: `ledstring_setConsole`,
  `display_show_console`, `/state.json`, the `console:<name>:<idx>` WS
  broadcast and the `saveLastSelectedConsole` arm all already read
  `currentConsoleIndex` and already meant it as "the selection".
- The e2e harness drives `/next` and `/prev`, both of which commit, so
  the index it asserts is unaffected.

### 2. `lightSingle()` as the single choke point

Sets `ringLit = true`, clears `ringFading` and `ringFadeRequested`, and
always stamps `ringHoldUntilMs = millis() + RING_HIGHLIGHT_IDLE_MS`.
"Lit with no scheduled off" is unreachable by construction — this is the
actual fix for the reported bug.

`lightRing(true)` was reduced to an unconditional
`lightSingle(currentRingLED)`. Its old `lit && (ringFading || !ringLit)`
guard stopped being a reliable "should I repaint" test once
`lightSingle()` owned `ringLit`.

### 3. `lighting_loop()` owns the fade decision

No longer merely pumps a fade someone else had to start. It:

- holds the ring while `lightRingSetProximityHold()` is set,
- otherwise starts the fade once the idle deadline passes,
- returns `true` on the tick the fade completes, which `main.cpp` uses
  to call `controls_ringFadedOut()`.

`controls_ringFadedOut()` reverts `browsedConsoleIndex` back to
`currentConsoleIndex` so the next detent starts from the live console.

New API: `lightRingSetProximityHold(bool)`, `lightRingForceOff()`,
and `lighting_loop()` returning `bool`.

### 4. Timeout config — `#define`, as decided

`RING_HIGHLIGHT_IDLE_MS` defaults to 5000 in
[src/configuration.h](../../src/configuration.h), wrapped in `#ifndef`
so any env can override it via `build_flags` in
[platformio.ini](../../platformio.ini). A `#define` rather than a
`/consoles.json` field — many settings are due to migrate to JSON
eventually and this one does not need to be per-install yet.

### 5. Bounds clamp (found while in there, not in the original plan)

`lightSingle()` is called with a **console index**, but the ring is
`NUM_RING_LEDS` (8) pixels. Any config with more than 8 consoles wrote
past the end of `leds[]` — silent memory corruption, and it would have
become live the moment the LED string work grows the console list. Now
clamped to `NUM_RING_LEDS - 1`.

## Decisions made along the way

- **Ring pixel semantics: left as the free-running spinner.** An open
  question in the plan was whether the ring pixel means "which way am I
  spinning" (the existing `currentRingLED` counter) or "which console
  am I on" (`lightSingle(currentConsoleIndex)`). Kept the existing
  spinner behaviour; the revert resets the *browsed cursor*, not the
  pixel counter. Revisit if the ring should read as a console position.
- **`ringFadeRequested` is separate from the deadline.** An earlier pass
  in this session collapsed them, which meant a proximity-clear event
  expired the deadline and silently disabled the timeout entirely when
  `RING_HIGHLIGHT_IDLE_MS` is 0. They are different signals — "someone
  asked for dark now" vs "nobody has asked for light recently".
- **Twinkle left out of scope.** It is on GP21 and belongs with the
  light-shows draft. This commit only had to leave the browsed index
  distinct and leave `selectConsole()` as the single commit point.

## Done-when evidence

All confirmed on the perfboard:

- Turn knob, walk away → ring lights, fades at ~5 s, next detent starts
  from the selected console.
- Select → ring goes dark.
- Turn knob with a hand held near it → stays lit past 5 s; fades when the
  hand leaves.
- `pico2w` builds clean; host test suite passes.

## Not verified

- The encoder desync noted in the plan (first detent after a revert
  looks like a jump) — cosmetic, left as-is.
