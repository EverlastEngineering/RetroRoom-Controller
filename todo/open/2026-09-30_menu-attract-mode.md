# Menu item — attract mode

**Status:** open
**File anchor:** `lib/CabinetMenu/` (the item type), `src/consoles.cpp`
(the cycling), `src/ledstring.cpp` (the paint)

## What

A mode where the cabinet cycles through the consoles on its own,
randomly, showing each as if selected — the thing a shop window does.

- A `bool` menu item, so it is the same shape as every other toggle.
- While on, the cabinet picks a random console after a dwell and moves to
  it, with the normal travel animation.
- Any real interaction — turning the knob, pressing next/prev, a commit —
  takes control back immediately and turns the mode off. An attract mode
  that fights the operator is worse than no attract mode.

## Why

Nobody is standing at the cabinet all day and it is on a television in a
dark room. Idle, it is either dark or showing whatever was last chosen.
Cycling is what makes it read as *running*.

## How

- The dwell should be its own `led.attractDwellMs` rather than reusing
  `ringIdleMs` — the ring timeout answers "when does the operator stop
  interacting", and attract mode needs "how long does each console hold".
  They are different questions and will want different numbers.
- Random with a bias: avoid picking the same console twice in a row, and
  make sure every console is reachable in a reasonable window rather than
  leaving the pick to chance. A shuffle bag is the obvious answer and is
  about ten lines.
- The StackSelector has to follow, or the cabinet will be showing a
  console the latch is not on. Check whether `selectStack()` is cheap
  enough to call on the same schedule — the comment in `consoleDefinitions()`
  says the write is debounced off the click path for a reason, and this
  is not the click path, but the reasoning may still apply.
- The dwell must be interruptible *within* a tick, not only between
  consoles, or the cabinet will ignore a hand for a whole cycle.
- Put the pick in `lib/CabinetMenu` so "a real interaction cancels
  attract mode" is a host test rather than a bench observation.

## Not decided

- Does attract mode suppress the IR firing, or is a remote code every
  eight seconds fine? It probably must — a blaster pointed at a television
  on a loop is a genuinely bad idea, and this is the kind of thing that
  is much cheaper to decide now than after it has been on for a week.
