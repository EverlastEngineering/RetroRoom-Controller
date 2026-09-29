# LED string — E2E readback for the GP21 strip

**Status:** open
**File anchor:** `agent-script/`, `src/network.cpp`, `src/ledstring.cpp`
**Created:** 2026-09-29, split out of the retired
`2026-09-25_led-string-light-shows_DRAFT.md` (its S4).

## What

Give the host a way to assert what the strip is actually doing. Two
parts:

1. **A readback endpoint.** A `GET /leds.json` snapshot of the live
   `selectedLeds[]` framebuffer, in the same spirit as the existing
   `GET /state.json`.
2. **An e2e assertion** in `agent-script/e2e-consoles-json.sh` (or a
   sibling) that drives `/consoles.json` with explicit
   `ledPosition` / `ledWidth` per console and asserts:
   - at rest, only pixels in the current console's prefix are non-zero;
   - during a browse, the blob's position tracks the detent count;
   - after a commit, the frame settles on the prefix within the
     `LEDSTRING_SELECT_EFFECT_MS` budget.

## Why

`test/test_ledstring_browse/` covers the *decision* math thoroughly on
the host, and the browse/selection items were reviewed against it. What
is not covered is the wiring: that `src/ledstring.cpp` actually pushes
those numbers to the wire, that `ledstring_loop()` is pumped, and that
the frame rate is enough for the animation to read as smooth. Those are
exactly the failures that are invisible in a unit test and obvious on
the bench.

## How

- Follow the existing readback discipline in
  `plans/2026-09-22_tdd-e2e-readback.md` — poll and byte-compare
  snapshots after a `wait_for_state`, rather than trusting response
  codes.
- The browse is driven by the rotary encoder, which the host cannot
  turn. Two options: add a test-only way to inject a detent, or assert
  only the states reachable over HTTP (`/next` and `/prev` both go
  through `selectConsole()`, so they exercise the selection effect) and
  leave the blob to the bench. Prefer the latter unless the injected
  detent turns out to be a one-liner — a test hook on the rotary path
  is a real API surface, and the blob math is already covered on the
  host.
- Follow the file-anchor guidance in `AGENT.md` and the completion
  ritual in `todo/README.md`.

## Open hooks

- **Is the endpoint worth it?** It is a new unauthenticated route on a
  device that is otherwise mostly local. Decide before building: a
  debug-only build flag, or a route that is always on because the
  cabinet is a closed physical object anyway.
- The snapshot is 64 CRGB values; decide the encoding (hex string per
  pixel, or a compact run-length of lit ranges) before writing the
  handler. Run-length matches how the assertions will actually be
  written — "pixels 0..41 lit, rest dark" rather than 64 numbers.
