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
| HEAD | `a18e555` — "a console at the end of a shelf still explodes" |
| Tree | clean |
| Host tests | `pio test -d . -e test_native` (run it; do not trust a number here) |
| Firmware | builds clean, and is flashed after each commit — see §9 |

The work is a single feature line. The second block of commits is a
follow-on session on the same effect, and several of its bugs are the
most instructive in the whole line because each looked like a tuning
problem and was not:

```
a18e555  a console at the end of a shelf still explodes
9caf1ba  the commit explodes and ignites, and the twinkle is gone
7e50af2  end the browse before starting the selection effect
811c1a7  paint the twinkle, and start the pulse at its peak
159f4df  let the ring keep spinning while the browse settles
951d6ff  swallow the detents that follow a commit
13c3a7a  give an abandoned run back instead of leaving it pointing
fffdd7d  let the run finish unwinding before the browse gives up
84acb46  the block must enter on the edge it departs by
33c9c86  repaint the fill on the frame clock, not only on a detent
```

And before those:

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
4. **The commit.** Two halves, 200 ms each: the console that was
   pulsing **explodes** outward to twice its width while dimming to
   nothing, then **ignites** back from zero width and zero brightness
   to exactly its own window at full. The second half ends on the
   resting paint, so the strip *arrives* at rest rather than being cut
   to it. Clamped to the console's shelf, per side.
5. **A ring strike on commit.** One flash of the whole ring before it
   fades — the one moment the operator is guaranteed to be looking at
   the knob.
6. **Settle lockout.** Rotary detents are ignored for 250 ms after a
   step commits, so overshooting the fifth detent costs one step
   instead of two. The ring keeps turning throughout; only the browse
   ignores those detents.
7. **Abandoned runs give themselves back.** Stop turning for 3 s and
   the progression run withdraws one LED at a time from its leading
   edge, at 250 ms each, until only the pulsing candidate is left. It
   finishes on the pulse by itself.
8. **Freeze at the ends.** Turning off either end of the list does
   nothing at all — no fill, no travel, no ring spinner.
9. **Colour per role.** Warm amber for what the operator is being
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
- **A step between shelves is not a big gap — it is a different rule.**
  The block enters the destination shelf at the end the string does
  *not* arrive at, and sweeps that whole shelf. Which end that is
  depends on the direction of the crossing: the shelves are one chain,
  so the bridge joins the high end of the lower shelf to the low end of
  the upper one. Stepping up enters at the shelf's high end, stepping
  back down enters at its low end. Taking the high end unconditionally
  looked right going up and collapsed coming back, because the console
  you are returning to sits at the bridge end of its own shelf — two
  pixels from where you were, so the "sweep" was a one-pixel nudge.
  On a two-row layout the hard cases are always the consoles *either
  side of the bridge*, and they are the two nobody tests.

In `computeFillGeometry()` the discriminator is that a within-shelf step
always enters **inside** the stretch of pixels the two consoles occupy
(its entry is the source's trailing edge, which is on the far edge of
that stretch), while a crossing enters outside it. That test has to be
strict for the same reason.

- **The block enters on the edge it departs by, and that is also where
  the fill anchors.** The spark is centred on the entry pixel, so the
  entry has to be the edge of the console being left that the block is
  actually running away from: trailing going forward, leading coming
  back. It was the trailing edge in both directions, which is invisible
  going forward and reads as a mis-start coming back — the block
  appeared on the *right* of the console it was leaving and then ran
  left, so a bare LED to the right of every console flashed for one
  frame. Only in that direction, on every console.

  The fill had already been fixed to anchor on the correct edge, so the
  two halves disagreed: the run began at the console's leading edge and
  the block began three LEDs to the right of the end of its own run. The
  simulator's `browse` scenario showed it the whole time — the block at
  `7..8` with the run at `2..4`.

  The rule now lives in the core as `travelEntryFor(leave, target)`, so
  the host tests can see it. It was in the shell, mirrored by hand in
  the simulator, and **neither copy is covered by a test** — a private
  copy of a direction rule is how a bug like this survives a session in
  which the tests are green and the simulator is run daily.

**Forward-only unit tests will not find any of these.** Use the
simulator's two-row geometry — `ledstring-sim.sh shelfback` is the
crossing that goes right to left, and `browse` is a within-shelf step
going left.

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

### An effect that *is* a console's window has to own that window

This cost three commits and two identical-looking bugs that pointed
opposite ways. Both were the same mistake.

The twinkle never appeared. Its brightness was computed correctly and
then thrown away by the **colour**: it took each pixel's role from the
resting picture, and `resolvePixel()` answers black for `OFF`
*whatever the level says*. With the stack not lit at rest, most of the
strip rests dark, so the twinkle had only ever been visible on top of
the lit stack — and turning the stack off at rest silently deleted it.
It looked like it "went away at some point nobody could date".

Then the first attempt at explode/ignite had the **mirror** fault: the
effect only wrote where the strike reached, so the resting paint's
full-brightness console showed *through* the ignite and the result was
a bright block with a dim patch growing inside it.

So: a full-frame effect must **own** every pixel it covers, and any
pixel it covers but has gone dark. Do not derive a pixel's role or
level from whatever was there before, and do not guard on "only paint
where it is currently dark" — one of those two is always wrong. The
fix that makes the ignite correct is structural: the ignite's final
range and level *are* the resting paint's, so the effect arrives at
rest rather than being cut to it.

### `ledstring_browseClear()` cancels whatever is animating — order the commit carefully

`selectConsole()` called `ledstring_selectEffect()` and *then*
`controls_browseReset()`, which calls `ledstring_browseClear()`, which
forces the mode to `RESTING` and repaints. The selection effect painted
its first frame and was then cancelled on the next statement, so it
never advanced by a single frame — at **any** value of
`LEDSTRING_SELECT_TWINKLE_MS`, which is why the symptom was
"setting it to 850ms changes nothing" and looked like a tuning problem.

Two competing paints back to back is what the operator actually saw:
a flicker or two, about 40 ms.

The comment in that function already said *"the strip keeps animating,
but the browse that was feeding it is over"*. The code said the
opposite one line later, and the comment was the more convincing of the
two. So: **end the browse first, then start the effect**, and the
ordering is a stated contract on both sides — the call site says why
it is load-bearing, and `ledstring.h` says what `browseClear` cancels.
There is no test for it; it is control flow across three shell
translation units, so the contract is the defence.

### A rule that is defensible alone can be wrong next to another

The first and last console on a shelf only ignited. Neither cause was
the effect, and both were individually reasonable:

- The shelf clamp capped the expansion to the **narrower** of the two
  sides, so a console with no room on one side had none on the other
  and did not expand at all.
- A config with no `shelves` block fell back to deriving each shelf's
  extent **from the consoles on it** — which says the shelf is exactly
  as wide as its contents, with no bare string at either end. So the
  outer sides had zero room by construction.

Two over-strict rules compounding, and the result read as a deliberate
design choice. When one symptom has two plausible causes, look for the
pair rather than fixing the one that explains the symptom most
neatly.

The fixes encode two distinctions worth keeping: **"where the consoles
are" is not "where the shelf ends"** (the fallback is the whole strip,
the only bound we actually know), and **a one-sided expansion is fine,
a dead one is not** — it is the LEDs *beside* a console that pay for
capping to the minimum.

### A cap must never be narrower than the thing it caps

`LEDSTRING_TRAVEL_PEAK_WIDTH` is a cap on how wide the travelling block
may get, and it was silently making the block **narrower than the
console it landed on** whenever that console was wider than 6 LEDs. A
39-LED window got a 6-LED sliver against the far end of it, and which
end depended on the direction of travel.

It was invisible for months because MAME's window was exactly 6 — the
console sat precisely on the clamp boundary, so it showed up as "one
LED too far to the right" and looked like an index bug. The fix is
`max(peak, targetWidth)`. The config comment had it backwards, which is
why the wrong reading was so persuasive.

**Corollary: a value sitting exactly on a threshold is a bug that
reports itself as a different bug.** Check the boundaries.

### A handover must start from whatever the previous phase ended at

The travel arrives at full brightness on the target window, and the
preview pulse then began at `pulseMinPct` — stepping the console down
by most of its range at exactly the moment the movement resolved into
an answer. It read as "jank that matching the colour didn't fix", and
that is what it was: a step in *luminance*, which no amount of colour
matching can touch. Matching the hue is what made it visible that
something else was still wrong.

Same shape as the colour bug it followed: `LEDSTRING_COLOR_TRAVEL_*`
has to equal `LEDSTRING_COLOR_PROPOSAL_*`, because the travel's last
frame *is* the target window and the frame after it is that window
pulsing as a proposal. There is a test asserting the two stay equal,
and there is `-I src` on the host test env specifically so a test can
read `configuration.h` — the invariant is *between* files and is not
checkable otherwise.

### An effect that sweeps an edge needs a fractional edge

`StrikeWindow` is in permille of a LED. A boundary that can only land
on a pixel edge means a new LED either appears at full brightness or
not at all, which reads as a staircase rather than a sweep. The fill's
antialiasing existed the whole time and had **never fired**, because
the knob's position is a whole number of LEDs — it only got used when
something needed sub-LED resolution.

The same maths settles the "round the width down to an even number"
question: the *target* is rounded, never the start. Rounding the start
would make the explosion's first frame one LED narrower than the window
that was pulsing a frame ago — a visible step at the exact moment of
the commit. The half-LED edges that result are exact in permille, and
the brightness is zero by the time they are reached.

### Replace an effect; do not leave it behind with its tests

The twinkle and the per-pixel collapse were deleted rather than
disabled. Dead code with passing tests is how a replaced effect turns
into the next bug: thirteen tests were asserting a `twinkleMs` field
that no longer described anything the device did.

### A mode in `ledstring_loop()` can become time-dependent without anyone noticing

`ledstring_loop()` decides what to repaint per mode. `FILLING` was
documented as a deliberate no-op, because the fill's *length* is the
detent gate's position and only changes on a click.

It stopped being true when the candidate behind the fill started
pulsing — that level is a function of `elapsedMs` — but the comment was
never revisited, so the pulse froze at whatever level the last click
landed on. Symptom: the console stops breathing the moment you touch
the knob and starts again when you stop.

There is no host test for this; the loop is in the shell. What *is*
testable, and is the thing to check, is the premise: does anything in
this frame move with time? `test_a_fill_frame_depends_on_the_clock`
asserts that exactly one thing does. If a mode's frame ever gains
another clock-driven element, that test is where it shows up — and if
the answer ever becomes "nothing", the no-op becomes correct again.

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
./agent-script/ledstring-sim.sh shelf      # a step between shelves, up
./agent-script/ledstring-sim.sh shelfback  # the same crossing, back down
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

Carried over from the first session, still open:

- **The black beat between the two halves of the commit.** The explode
  ends at zero brightness and the ignite starts at zero width *and*
  zero brightness, so there is a couple of frames of total darkness.
  That is the gap between the old console going and the new one
  arriving, and the user has been asked whether it reads as a pause or
  a blink. If it blinks: overlap the phases slightly, or start the
  ignite's brightness above zero. Both are one-line changes.
- **Shelf extents for the real cabinet.** `example5-single-shelf.json`
  has a placeholder. The user needs to say where each shelf's bare
  string starts and ends.
- **Point the simulator at the live geometry.** It still replays the
  two-row example, so a 3-LED console appears to explode by half a
  LED. The wide-spacing case is the one worth being able to look at.
- **The simulator never modelled the loop's repaint policy**, which is
  where the frozen pulse and the cancelled commit both lived. It
  replays frame maths, not frame *scheduling*.

`LEDSTRING_FAST_SPIN_WINDOW_MS` is **0 (disabled)**, and the reason is
worth keeping: 1000 ms is *shorter than a deliberate human detent*, so
every browse escalated. The latch that compounded it is fixed
independently, so the escalation now tracks the gap before each detent.
Re-enable only once someone has timed their own slowest deliberate
turn; a starting guess is 2000–3000 ms.

---

## 8. The config schema, and what is in it now

`/consoles.json` is the only operator-facing config. Two additions this
session, both **optional** and both defaulting to current behaviour, so
every config written before them still loads identically:

| field | where | default | why |
|---|---|---|---|
| `shelf` (int) | per console | `0` | Which physical shelf. Not derivable: the shelves are one chain, so pixel order says nothing about where one ends. |
| `shelves` (array) | top level | empty | `{id, fromLed, toLed}` — each shelf's **physical** extent, keyed by the `shelf` its consoles carry. `fromLed`/`toLed` inclusive. |

`shelves` is what the commit's explosion is clamped to. Absent, the
bound is the **whole strip** — deliberately not the span of the
consoles on the shelf, because that says the shelf is exactly as wide
as its contents and leaves the outer consoles no room to grow (see
§4). Declaring real extents beats the fallback, because then the bound
is the shelf rather than the strip.

**Known problem in the live config, found by the new boot log:** XBOX
is declared `ledWidth: 39` at `ledPosition: 55` on a 64-LED strip, so
it has silently been clamped to **9** for its whole life. The old boot
log printed the *declared* end (`55..93`) and read as though the
console really were that wide. It now prints the clamped end and says
when the two differ. If that console is meant to be a large block it
needs a wider strip or an earlier `ledPosition`.

---

## 9. Where the tuning lives

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

## 10. Repo conventions worth not re-learning

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
- **Check `git diff --stat` after every scripted multi-function edit.**
  Twice in the last session a slice from "delete this function" to
  "up to the next one named X" swallowed forty-five unrelated tests,
  because the assumed neighbour was not the actual neighbour — and
  `configuredFrame`, the shared fixture, went with them, which is what
  the compiler noticed rather than the test run. The stat line is the
  cheapest possible check and it is the *only* one that catches this.
  Prefer deleting by exact function-name boundary, and restore with
  `git checkout --` rather than trying to patch the damage back.
- **After fixing a bug, check the two things you changed together.**
  The end-consoles bug was two independent over-strict rules; the
  frozen pulse was a stale comment next to a stale mode case. Both were
  found by looking for the *second* cause, not by fixing the first
  plausible one.
- **Flash after every commit, not at the end of a batch.** The user
  checks the hardware as each change lands, and most of the bugs in
  the last session were only visible on the strip. They will also
  flash independently and interrupt without warning — check
  `git status` before resuming and expect a half-finished tree.
- **Never use the `vscode_askQuestions` tool.** It breaks the agent's
  automode on this machine. Ask in ordinary prose.
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
