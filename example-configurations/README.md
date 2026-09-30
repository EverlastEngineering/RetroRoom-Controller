# Example console configurations

Each file is a complete `/consoles.json`, uploadable as-is. Start from
the one closest to your cabinet rather than from an empty file.

| file | what it is for |
|---|---|
| `example1.json` | the smallest thing that boots: one console |
| `example2.json` | several consoles, gaps between their windows |
| `example3-two-rows.json` | two shelves strung as one chain, so a step between them sweeps the shelf |
| `example4-one-shelf.json` | three consoles, one shelf, wide LED gaps — a one-LED error is unmissable |
| `example5-single-shelf.json` | this cabinet: three consoles, a 118-LED string, XBOX at its real 39-wide |
| `example6-all-options.json` | **every `led` option, explicitly set** — the reference |

---

## The `led` block

Everything about how the strip looks and feels, as an optional
top-level object. **Every key is optional**, and a missing key takes the
default below, so a config without a `led` block behaves exactly as it
did before the block existed.

Two rules that are not obvious:

- **A value out of range is clamped, and reported.** The device boots
  and prints `console config: led.<key> = <asked> is outside <lo>..<hi>;
  using <value>` on the serial log. It does not refuse the config —
  a hand-edited file should not stop a cabinet working — but it will
  tell you.
- **One number, one home.** The defaults below live in
  `lib/ConsoleConfig/src/LedFeelDefaults.cpp` and nowhere else, and
  this table is *generated* from them by `agent-script/led-feel-dump.sh`.
  If you change one, re-run that rather than editing this file.

### The strip

| key | default | range | what it does |
|---|---|---|---|
| `totalLeds` | `512` | 1..512 | **How many LEDs are fitted.** The firmware is built for 512; this is how many are really there, and it is what console windows are clamped against. **Err low.** A window past the end gets clamped and the console silently lands in the wrong place; a count too high only leaves tail LEDs unlit, which you can see. |

### The travel — the block moving between consoles

| key | default | range | what it does |
|---|---|---|---|
| `travelMs` | `420` | 0..60000 | How long the move takes. |
| `travelPeakWidth` | `6` | 1..64 | A **cap** on how wide the block may get, not a target. The block always ends exactly the width of the console it lands on, because the cap is floored at the target's width — so a value below your widest window is simply ignored, and lowering it will *not* give you a slimmer block. |
| `travelSparkLeds` | `2` | 1..64 | How wide the block is where it leaves the console. 1 reads as a stray pixel, 2 as an object. |

### The knob

| key | default | range | what it does |
|---|---|---|---|
| `detentsPerStep` | `5` | 1..64 | Detents for one console step when turning deliberately. Fewer and a step happens by accident; more and the knob stops feeling like it is choosing. |
| `fastDetentsPerStep` | `2` | 1..64 | The same, once spinning. Clamped to `detentsPerStep`. |
| `fastSpinWindowMs` | `0` | 0..60000 | A detent arriving within this many ms of the previous one marks a spin. **0 disables the escalation entirely, which is the current setting** — it used to be 1000, which is *shorter than a deliberate human detent*, so every browse escalated. Set it well above your slowest deliberate turn (2000–3000 is a starting guess) if you want it back. |
| `settleLockoutMs` | `250` | 0..60000 | Detents ignored after a step commits, so overshooting the fifth costs one step rather than two. Only the *browse* ignores them — the ring still turns, so the knob never feels stuck. 0 disables. |

### The progression run — the dim fill that walks the gap

| key | default | range | what it does |
|---|---|---|---|
| `fillMinLeds` | `3` | 0..512 | Floor on the run's length. A step *between shelves* is a couple of pixels in index space and a long way round the cabinet, so filling the literal gap would leave the indicator barely moving on exactly the steps hardest to read. |
| `fillRetreatDelayMs` | `3000` | 0..60000 | How long the knob must be quiet before an abandoned run starts giving itself back. 0 disables. |
| `fillRetreatStepMs` | `250` | 0..60000 | How long one LED takes to be withdrawn. This is also that LED's **fade**, so the run reels in rather than strobing. 0 disables. |
| `blobWidth` | `3` | 1..512 | The browse blob's width in LEDs. Wider reads as more mass moving; 1–2 as a cursor. |

### The preview pulse

| key | default | range | what it does |
|---|---|---|---|
| `pulseMs` | `1100` | 1..60000 | One cycle. Shorter reads as a heartbeat, longer as a slow breath. |
| `pulseMinPct` | `30` | 0..100 | The dimmest. Wants to stay clearly non-zero or the pulse strobes. |
| `pulseMaxPct` | `100` | 0..100 | The brightest. The travelling block arrives at exactly this, so a mismatch here is a visible step at the handover. |

### Brightness — a percentage of the role's own colour

| key | default | range | what it does |
|---|---|---|---|
| `abovePct` | `0` | 0..100 | The resting stack — the consoles *above* the selection. **0 by default:** the resting strip shows the selection and nothing else, because a cumulative reading was reported from the bench as "the whole string is lit". |
| `selfPct` | `100` | 0..100 | The selected console's own window. The selection is the brightest thing on the strip. |
| `dimPct` | `22` | 0..100 | Everything shown *around* what you are looking at during a browse. Deliberately distinct from `abovePct`: one is a resting-state choice, the other is part of the animation. |
| `fillPct` | `45` | 0..100 | The progression run. A third level on purpose — sharing a level with the stack would make "where the stack ends" and "how far I have got" the same fact. |
| `blobPct` | `100` | 0..100 | The browse blob. |
| `browseFromPct` | `25` | 0..100 | The window being left. |
| `browseToPct` | `45` | 0..100 | The window being approached. |

### The commit

| key | default | range | what it does |
|---|---|---|---|
| `explodeMs` | `400` | 0..60000 | The window dissolving outward to twice its width while dimming to nothing. |
| `igniteMs` | `200` | 0..60000 | The window coming back from zero width and zero brightness, ending on exactly the resting paint. |

They are the whole effect — the total is their sum, not a third
number, so the two cannot disagree about where the end is. They meet at
zero brightness, which is the gap between the old console going and the
new one arriving.

### The ring

| key | default | range | what it does |
|---|---|---|---|
| `ringIdleMs` | `5000` | 0..600000 | How long the ring stays lit before an abandoned browse reverts to the selected console. Suppressed while a hand is at the proximity pad, and **held** while the progression run is still unwinding, so it never cuts a retreat short. 0 disables. |
| `ringFlashMs` | `120` | 0..60000 | One strike of the whole ring on a commit, before it fades. 0 disables. |

### The strip as a whole

| key | default | range | what it does |
|---|---|---|---|
| `frameIntervalMs` | `8` | 1..100 | How often an in-flight frame is pushed to the wire. A **sampling rate, not a step count** — every frame is computed from elapsed time, so raising it plays the same animation more smoothly rather than faster. The floor is how long the driver takes to clock the strip out plus whatever the rest of the loop needs; it measures that and prints `[ledstring] N frames, mean work Nus, worst frame Nus` when an animation ends, so set this from that line rather than by guessing. |

### Colours

`"colors": { "role": [r, g, b] }` for each of `stack`, `leaving`,
`fill`, `travel`, `proposal`, `selected`. Values 0..255.

| role | default | what it is for |
|---|---|---|
| `stack` | `[12, 28, 40]` | consoles above the selection at rest. Cool and dark. |
| `leaving` | `[20, 34, 48]` | the console being turned away from. |
| `fill` | `[16, 40, 56]` | the progression run. Cool, so it never reads as a console that is on. |
| `travel` | `[64, 40, 8]` | the block in flight. |
| `proposal` | `[64, 40, 8]` | the console a click would select, pulsing. |
| `selected` | `[48, 36, 24]` | the resting selection. |

The warm ambers are for what the operator is being *offered*; the cool
blues are the context it is contrasted against. That is the one
distinction worth having by eye alone — if it reads as one colour in a
dark room, the context group wants to be *dimmer*, not a different hue.
Values are deliberately conservative: this strip sits next to a
television in a dark room, and a misconfigured colour here is a glare
problem.

> **`travel` and `proposal` must be the same colour.** The travel's last
> frame *is* the target window, and the frame after it is that window
> pulsing as a proposal — so any difference is a flash of a different
> hue at exactly the moment the movement resolves into an answer. You
> can set them apart, but you have to change both.

---

## Regenerating the reference

```sh
./agent-script/led-feel-dump.sh              # print the `led` block
./agent-script/led-feel-dump.sh -o out.json  # write it to a file
```

It reads the same `defaultLedFeel()` the firmware and the simulator
read, so it cannot disagree with the device. `example6-all-options.json`
is that output with `totalLeds` set to 118.

If a new option is added to `LedFeel`, add it to
`agent-script/led-feel-dump.cpp` too — and to the table above. Nothing
enforces that; C++ has no reflection to count the fields. The failure
mode is a new option simply missing from the reference, which is a much
milder problem than a reference carrying a stale value.

---

## The other blocks, briefly

- **`consoles[]`** — `id`, `tvInput`, `selectorPosition`, `ledPosition`,
  `ledWidth`, `shelf`, optional `tagline`. `ledPosition` is 0-based: a
  console at `ledPosition: 4` with `ledWidth: 8` owns LEDs 4..11.
- **`shelves[]`** — optional. `{id, fromLed, toLed}`, inclusive, keyed
  by the `shelf` its consoles carry. The commit's explosion is clamped to
  it. Absent, the bound is the whole strip.
- **`irCodes`**, **`consoleNames`**, **`lcd`** — unchanged.
