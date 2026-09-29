# Handoff — LED string browse & selection work

Written 2026-09-29, at the user's request, before compacting. Read this
first; it is the state of play and the traps, not a history of how it
got there.

The bench verdict at the time of writing: *"the UX on this lighting is
excellent... I have more bugs and features but it's very usable."*

---

## 1. Where things stand

| | |
|---|---|
| Branch | `session/pico-2-wireless` |
| HEAD | `fb518a6` — "commit only what has been shown, and keep the candidate lit" |
| Tree | clean |
| Host tests | 139 passing (`pio test -d . -e test_native`) |
| Firmware | builds clean; the user had just uploaded `fb518a6` themselves |

The work is a single feature line, landed as nine commits:

```
fb518a6  commit only what has been shown, keep the candidate lit
df7e176  give each state its own colour
0704b59  fix five faults in the browse fill, rest and preview
3475635  scripted travel, knob-turn fill, and shelf crossings
58e28be  freeze at the ends of the list, and measure the frame rate
c580019  todo: file the console-browse-stops-at-ends regression
6ac97ed  plans: add the LED string animation proposal
a497f0c  light the consoles above, not a contiguous prefix
b15d20a  disable fast-spin escalation, stop it latching
```

The design brief the user wrote is
[`plans/2026-09-29-animation-proposal.md`](2026-09-29-animation-proposal.md).
It is committed as-written, deliberately: the implementation has to
reproduce those frames, so they belong in the history rather than in
someone's scrollback.

---

## 2. What the feature does

The GP21 LED string is a second FastLED strip behind the console
shelves. The rotary knob drives it.

1. **Gated browse.** A console step takes `LEDSTRING_DETENTS_PER_STEP`
   (5) detents, not one. Four of them walk a dim *fill* across the gap
   between two consoles; the fifth triggers the travel.
2. **Scripted travel.** A block of light leaves the console, stretches
   across the gap and lands exactly on the target window, then hands
   over to a pulsing preview. Time-driven — see §4.
3. **Shelf crossings.** A step that crosses shelves enters at the far
   end of the *destination* shelf and sweeps back to the target, so the
   light travels the width of the cabinet while the knob went forward
   one console. Odd, and deliberate.
4. **Selection effect.** Committing twinkles the whole strip and
   collapses it to the selected console, in well under a second.
5. **Freeze at the ends.** Turning off either end of the list does
   nothing at all — no fill, no travel, no ring spinner.
6. **Colour per role.** Warm amber for what the operator is being
   *offered*; cool blue for the context they are choosing against.

---

## 3. Architecture — and why it is this way

**`lib/LedStringPaint` owns every decision. `src/ledstring.cpp` only
converts to CRGB and calls `FastLED.show()`.**

That split is not stylistic. It is what makes
`agent-script/ledstring-sim.sh` possible, and the simulator has caught
roughly a dozen bugs that no unit test could see — including a
backwards travel that *inverted* the block, and a fill that only ran
forwards. **Before writing any paint logic in the shell, check it
belongs in the core.** A second copy of animation logic is one nobody
ever runs.

### The data model

A frame (`retroroom_core::StripFrame`) carries the strip size, the
console windows, the browse geometry and a colour palette.
`computeStripFrame()` resolves it into a `StripPixel` per LED — a
**role** plus a **level** — and `resolvePixel()` turns that into an
RGB triple. The core is the only thing that knows what a pixel is *for*,
so it is the only place that can decide.

**Roles:** `OFF`, `STACK`, `LEAVING`, `FILL`, `TRAVEL`, `PROPOSAL`,
`SELECTED`. Each maps to one `LEDSTRING_COLOR_*` triple.

### The three indices in `src/controls.cpp`

| name | meaning |
|---|---|
| `currentConsoleIndex` | the console actually selected |
| `browsedConsoleIndex` | the console being *approached* |
| `browseAnchorIndex` | the console the browse *snapped onto* — the pulsing candidate |

They are all different and the distinction is load-bearing:

- **The press commits `browseAnchorIndex`, never `browsedConsoleIndex`.**
  The anchor is what has been shown; the browsed index is what you are
  merely heading toward, and after one detent it is already the next
  console along.
- `LEDSTRING_ABOVE_PCT` is the *resting* stack and defaults to **0** —
  only the selection is lit at rest. The cumulative reading was
  reported from the bench as "the whole string is lit".

---

## 4. Traps that cost real time

Read these before touching the code. Most of them are my own mistakes.

### Position, not detent count

`DetentGate` tracks a **continuous position in permille of one console
step**, not a counter. Reversing must walk the blob back the way it
came; a counter cannot do that without a second undo state. The
`targetDirection` is the sign of the *position*, not of the detent, and
has to be recomputed *after* the position moves.

### Direction-dependent geometry

The browse goes both ways, and several things silently assumed forward:

- The leading edge is whichever edge moves into new territory — the
  **left** one on a backwards travel. Easing the right edge as leading
  unconditionally made the two cross and the block vanish for the
  middle of every shelf crossing.
- "The fill the block has not reached yet" depends on the direction of
  travel.
- The fill run is the **gap** between two windows, anchored on the side
  the block starts from. It used to be measured from the source's
  trailing edge in both directions, so a backwards run lay *inside* the
  console being left and the indicator crept the wrong way.

**Forward-only unit tests will not find any of these.** Use the
simulator's two-row geometry.

### The last *drawn* detent is 100%, not the last detent

`fractionPermille` only reaches 1000 on the detent that *commits* the
step, by which point the indicator is no longer being drawn. Hence
`DetentEvent::stepPermille`, the same position remapped by
`N / (N-1)`. Without it the pixel nearest the target console is still
dark on the fourth of five detents.

### Value-initialise every `LedRange` member of `StripFrame`

`LedRange` is a plain aggregate with no initialisers of its own, so
`LedRange activeWindow;` holds whatever was on the stack. This showed
up as a frame drawing a proposal over a random span of the strip.

### `Console.shelf` has no default member initialiser — on purpose

Under C++11 a default member initialiser stops `Console` being an
aggregate and breaks every brace-initialised `Console` in the tests.
The parser always sets it.

### `PIPESTATUS` and `|| true`

`agent-script/pio-build.sh` used to end its filtering pipeline with
`|| true`, which resets `PIPESTATUS`, so `${PIPESTATUS[0]}` reported
`true`'s status and **every failing build printed "build OK"**. Fixed
in `c76a666`. Never append `|| true` to a pipeline you are reading
`PIPESTATUS` from.

---

## 5. The simulator is the primary tool here

```sh
./agent-script/ledstring-sim.sh            # every scenario
./agent-script/ledstring-sim.sh carry      # a candidate that keeps pulsing
./agent-script/ledstring-sim.sh shelf      # a step between shelves
./agent-script/ledstring-sim.sh browse     # fill + travel
./agent-script/ledstring-sim.sh select     # the commit twinkle
./agent-script/ledstring-sim.sh frames     # the resting stack
./agent-script/ledstring-sim.sh above      # the readings of "above", side by side
```

It replays the real `computeStripFrame()` with the real
`LEDSTRING_*` values and prints two views per frame:

- **brightness**, a ten-step ramp
- **role**, a glyph per LED — `S` selected, `P` proposal, `T` travel,
  `f` fill, `l` leaving, `s` stack, `.` off

**The role view exists because brightness alone hid real bugs.** During
the fill, the console being left was tagged as progress; during the
travel, the block, the console behind it and the fill it was consuming
were three identical pixels. Both rendered correctly by brightness and
were invisible until the roles separated them. Use it.

Reading its output: 64-pixel rows get re-wrapped by the terminal. To
read them, `tr -d '\n'` then split on the caption and `cut` the strip
columns, or grep the caption plus a character class.

---

## 6. Verifying

```sh
./agent-script/pio-build.sh                    # compile only, always safe
pio test -d . -e test_native                   # host tests
./agent-script/ledstring-sim.sh                # animations, no hardware
```

Flashing needs explicit permission each time. The user has said "you are
free to flash" for this line of work but has also uploaded on their own
mid-session — **check before uploading.**

**Frame rate is measured, not guessed.** `pushFrame()` times the work
and `ledstring_loop()` prints frame count, mean work and worst frame
when an animation hands back to rest. That line appears on the device's
own serial, which the user watches in a Monitor terminal. Read it there
and set `LEDSTRING_FRAME_INTERVAL_MS` from it. If the worst frame is
near the interval, the limit is us and lowering it further only starves
`loop()`.

The animations are **time-based, not frame-based** — every frame is
computed from elapsed time. A higher frame rate is the same animation
sampled more finely, not played faster.

---

## 7. Open work

| Todo | What |
|---|---|
| `todo/open/2026-09-25_led-string-wiring-diagram.md` | The 6-segment serpentine layout. **Unverified assumption underneath the travel:** the strip is treated as indexed top-of-cabinet downward. If the real cabinet is numbered the other way, `computeKeepEnd()` and `paintResting()` change and the brightnesses need re-tuning. |
| `todo/open/2026-09-25_led-string-e2e-readback.md` | A `/leds.json` snapshot so the frames can be asserted from the host. Decided *not* to add a rotary-injection test hook — the math is covered on the host, and a new API surface needs a concrete reason. |
| `todo/open/2026-09-29_console-browse-stops-at-ends.md` | Filed and **implemented** in `58e28be`; the todo still needs closing out. The open question it records — what a detent *at* the end should do — was answered on the bench: freeze entirely, ring spinner included. |

`LEDSTRING_FAST_SPIN_WINDOW_MS` is **0 (disabled)**, and the reason is
worth keeping: 1000 ms is *shorter than a deliberate human detent*, so
every browse escalated. The latch that compounded it is fixed
independently, so the escalation now tracks the gap before each detent.
Re-enable only once someone has timed their own slowest deliberate
turn; a starting guess is 2000–3000 ms.

---

## 8. Where the tuning lives

Everything is a `#define` in `src/configuration.h`, under the *"LED
string (GP21) browse + selection feel"* block, grouped by which
behaviour it shapes. Nothing in the logic or the core hard-codes a
timing.

The colour split to try before anything else: **amber for what is being
offered** (`LEDSTRING_COLOR_SELECTED_*`, `PROPOSAL_*`, `TRAVEL_*`) and
**blue for the context it is contrasted against** (`STACK_*`,
`LEAVING_*`, `FILL_*`). That is the one distinction worth having by eye
alone. If it reads as one colour in a dark room, the context group
wants to be *dimmer*, not a different hue. Values are deliberately
conservative — this strip sits next to a television in a dark room.

---

## 9. Repo conventions worth not re-learning

- **Never restate tool documentation or record drift-prone numbers**
  in `AGENT.md`/`readme.md`. Symptoms and where to look, not a
  transcription. `AGENT.md` §0.
- **`pin-map-chart.md` is the hardware source of truth.** If code
  disagrees with it, the code is wrong. A missing or ambiguous pin is a
  blocking question, not a guess.
- **Never filter build or monitor output** through `tail`/`grep`/`head`.
  The user wants to see everything. Prefer the `agent-script/`
  wrappers. (Filtering *my own* simulator output to read a 64-column
  table is fine and is not what that rule is about.)
- **Never edit a committed file with blind `python` string surgery.**
  Several times during this work a replace silently no-op'd — the
  file uses tabs, my patterns used spaces — and I spent whole rounds
  chasing a "fix" that had never applied. `awk` to print the line
  numbers and the exact text, edit by index, assert the line you
  expected. Verify the build after *every* step in a refactor; the
  error message names the line.
- **Task completion ritual:** land the code, record the actual
  implementation in the todo body, `git mv` to `done/<sha7>_<slug>.md`.
- **`LOG.md`** is MADR, newest first, UTC timestamps.
