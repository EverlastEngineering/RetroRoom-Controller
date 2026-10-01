# Menu item — light show mode, behind a secret sequence

**Status:** open
**File anchor:** `lib/LedStringPaint/` (the show), `src/ledstring.cpp`
(the dispatch), `src/network.cpp` (the HTTP entry)

## What

A mode that plays an animated light show across the whole GP21 string,
ignoring the console layout.

- Entered by a **secret sequence on the knob**, or over HTTP. Not a menu
  item — that is the point of "secret".
- Plays until interrupted.

## Why

It is a show, not a setting. Putting it in the menu would mean anyone
who learns to use the menu can leave the cabinet running an animation
instead of a console indicator, which is exactly the "locked down mode"
problem in `2026-09-30_menu-lock-down-mode.md`.

## How

- **The secret sequence needs to be actually secret and actually
  rememberable.** A fixed sequence nobody but the author knows is not
  usable six months later. Consider a short deliberate pattern — the
  existing dead `sequenceElapsed()` in `src/controls.cpp` is a
  single-double-click recogniser, not a pattern matcher, so this is new
  work. Define what "pattern" means for a rotary that can turn, click and
  hold before writing it.
- Keep the show itself in `lib/LedStringPaint`, which is where the
  browse and selection paint already live and where
  `computeStripFrame()` decides what a frame *is*. A show is another
  `StripEffect`, not a special case in the shell.
- `agent-script/ledstring-sim.sh` replays frames as ASCII using the same
  `computeStripFrame()` call, so a show built this way is reviewable
  without hardware. Use it — see `/memories/repo/ledstring-paint.md`.
- IR and the StackSelector must not fire during a show. A show that
  drives the latch through every position would wear it out.
- The HTTP entry is a second, easier door to the same thing. If the knob
  sequence is meant to be secret, the endpoint has to be behind the same
  thought — and that thought is probably "is this cabinet on a network
  anyone else can reach".

## Not decided

- What the show actually looks like. Rainbow cycle, chase, sparkle,
  something driven by the console names? This is the fun part and it
  should not be decided by whoever writes the dispatcher.
- Whether a show can be *configured* — a name in the JSON selecting one
  of several — which would make it a menu item again and undo the
  secrecy. Probably keep the catalogue compiled in and the choice
  unexposed.
