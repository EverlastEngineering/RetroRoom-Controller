# LED strip light control (GP21 second string — basic driver)

**Status:** open
**Branch:** session/pico-2-wireless
**File anchor:**
- **New module:** [src/ledstring.h](src/ledstring.h), [src/ledstring.cpp](src/ledstring.cpp)
  (or appended into [src/lighting.h](src/lighting.h) /
  [src/lighting.cpp](src/lighting.cpp) with a clear `// STRING (GP21)`
  divider — TBD when picked up).
- **Init call:** `ledstring_init()` invoked from
  [src/main.cpp](src/main.cpp) after `lighting_init()` (search for
  `lighting_init()` in `setup()`).
- **Light trigger:** free function `ledstring_setConsole(int idx)` (or
  equivalent) called from [src/consoles.cpp](src/consoles.cpp) at the
  commit sink — **`selectConsole(const Console& c)` at
  [consoles.cpp:142](src/consoles.cpp#L142)** — so every commit (rotary
  press, NEXT/PREV buttons, `/next`, `/prev`, WebSocket `console`)
  repaints the strip automatically.
- **Untouched (firm contract):** [src/lighting.h](src/lighting.h)'s ring
  API (`lightSingle`, `lightRing`, `ringLEDNext`, `ringLEDPrevious`,
  `lightCycle*`) and [src/lighting.cpp](src/lighting.cpp)'s `CRGB leds[]`
  buffer on GP20. The ring's per-click `ringLEDNext`/`ringLEDPrevious`
  behavior is unchanged. There is no need to modify the rotary handler
  in [src/controls.cpp](src/controls.cpp).

**Created:** 2026-09-25
**Out of scope for this item:** animations on transition, generic light
shows (rainbow chase / sparkle / breathing), the 6-segment wiring
diagram, and e2e readback tests — those are tracked in
[todo/open/2026-09-25_led-string-light-shows_DRAFT.md](./2026-09-25_led-string-light-shows_DRAFT.md).

---

## What

Wire the second LED string on GP21 to a FastLED controller and light
the LED range physically above whichever console is currently
selected. Nothing more.

## Why

[src/lighting.cpp](src/lighting.cpp) today only paints the 8-pixel
ring on GP20. `GP21 SELECTED_CONSOLE_LED_STRING_DATA` +
`NUM_SELECTED_CONSOLE_LED_STRING_LEDS=64` are already declared in
[src/configuration.h](src/configuration.h) with the comment "Reserved
for the second FastLED strip; wiring lands in a follow-up commit" —
but no `addLeds` call or zoned driver exists for it yet.

`ledPosition` + `ledWidth` are already on the
`retroroom_core::Console` struct (parsed in
[lib/ConsoleConfig/src/ConsoleConfig.cpp:102-103](lib/ConsoleConfig/src/ConsoleConfig.cpp#L102))
and used by [example-configurations/example2.json](example-configurations/example2.json)
— NES `[1,1]`, SMS `[7,5]`, XBOX `[17,5]`, MAME `[27,15]` — but no
renderer reads them. The pin, the buffer size, the schema, and the
JSON are all in place; the missing piece is the driver and the
one-line paint-on-commit.

The user's specific framing for *this* item: "exclusively getting LED
strip light control." That's the narrowest possible slice that
delivers the visible feature (lit LEDs above the selected console) on
the real hardware. Everything else — animation, shows, the wiring
diagram, e2e tests — is a separate sub-item tracked in the draft.

## How (in scope for this item)

### Phase A — driver plumbing on GP21

1. Add a zoned buffer **separate** from the ring's `CRGB leds[NUM_LEDS]`:
   `CRGB selectedLeds[NUM_SELECTED_CONSOLE_LED_STRING_LEDS];`
2. Mirror the `#undef DATA_PIN / #define RR_FASTLED_DATA_PIN` dance
   already in [src/lighting.h:23-37](src/lighting.h#L23) for the new pin
   (suggested name: `RR_FASTLED_STRING_DATA_PIN`). The `#if
   defined(HAS_LEDS)` guard pattern in `lighting.h` covers both.
3. `FastLED.addLeds<WS2812B, RR_FASTLED_STRING_DATA_PIN, GRB>(
   selectedLeds, NUM_SELECTED_CONSOLE_LED_STRING_LEDS);`
4. New `ledstring_init()` in [src/main.cpp](src/main.cpp) `setup()`,
   after `lighting_init()`. Initial paint: `fill_solid(selectedLeds,
   NUM_SELECTED_CONSOLE_LED_STRING_LEDS, CRGB::Black);
   FastLED.show();` — strip powers up dark.

### Phase B — paint the active console's window on commit

5. Add `ledstring_setConsole(int idx)` (free function in the new
   module). It reads `CurrentConsole().ledPosition` and
   `.ledWidth` from [src/consoles.cpp](src/consoles.cpp), clears the
   buffer to black, paints the window `[ledPosition, ledPosition +
   ledWidth)` to a dim warm white (`CRGB(48, 36, 24)` or similar —
   see Open question Q1), and calls `FastLED.show()`.
6. Call `ledstring_setConsole(currentConsoleIndex)` from inside
   `selectConsole(const Console& c)` at
   [consoles.cpp:142](src/consoles.cpp#L142). This single insertion
   point covers **all five** commit triggers (rotary press,
   `NEXT_CONSOLE_BTN`, `PREV_CONSOLE_BTN`, HTTP `/next`, `/prev`,
   WebSocket `console`) because they all funnel through
   `selectConsole()`.
7. **Do not** paint from `advanceConsole()` / `rewindConsole()` before
   the `selectConsole()` call — those funnel through it anyway, and
   double-painting risks races.

### Phase C — host-side unit test

8. Add a test under `test/test_ledstring/` (mirroring the structure
   of [test/test_console_config/](test/test_console_config/) and
   [test/test_console_store/](test/test_console_store/)) that asserts
   `ledstring_setConsole(idx)` produces a buffer where pixels
   `[ledPosition, ledPosition+ledWidth)` are non-zero and all other
   pixels are zero, for at least two example configs (NES 1×1, MAME
   15-wide). Use `pio test -e test_native`.
9. The test owns its own **fake** `selectedLeds[]` buffer for the
   host; no FastLED calls. Mock or stub the `FastLED.show()` symbol
   if needed (the existing tests do this — copy the pattern).

## What this item does NOT do (parked in the draft)

- Knob-spin "pull" animation between zones (~500 ms tween).
- Generic light shows (rainbow chase, sparkle, breathing, etc.).
- Wiring diagram / 6-segment pixel-index map.
- E2E `/leds.json` snapshot readback test.
- Brightness / color tuning on the perfboard.

Those are sub-items in
[todo/open/2026-09-25_led-string-light-shows_DRAFT.md](./2026-09-25_led-string-light-shows_DRAFT.md).

## Open questions (resolve during the first pass, not before)

- **Q1 — Steady-state color.** Dim warm white (`CRGB(48, 36, 24)`)
  is a placeholder — pick whatever looks good on the bench.
- **Q2 — Module location.** `[src/ledstring.{h,cpp}]` separate, or
  appended into `lighting.{h,cpp}` with a `// STRING (GP21)` divider?
  Pick whichever keeps the diff smaller; both are fine.

## Acceptance criteria (what "done" means)

1. Branch builds clean: `./agent-script/pio-upload-monitor.sh
   --no-upload` on `[env:pico2w]` succeeds with zero warnings.
2. `pio test -e test_native` passes including the new
   `test_ledstring` cases.
3. The ring on GP20 animates identically to today for every one of
   the 5 commit triggers (no regression in ring behavior).
4. After a commit, the LEDs on the GP21 strip at
   `[currentConsole.ledPosition, ledPosition+ledWidth)` are lit and
   all other LEDs in the strip are dark (verified by bench-visual
   inspection only — the unit test covers the deterministic case).
5. On a `/next` call, the lit window jumps cleanly from one console's
   range to the next with no animation in between.
6. The todo moves to `todo/done/` with `<sha>_` prepended; the
   `LOG.md` cross-references the commit; the `todo/README.md`
   current-inventory section is updated.
