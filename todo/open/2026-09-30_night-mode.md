# Night mode — the selected console's LEDs stay dark

**Status:** open — the *behaviour* is done, the *config surface* is not
**File anchor:** `src/controls.cpp` (the double-click),
`lib/LedStringPaint/src/LedStringPaint.h` (`StripFrame::nightMode`),
`src/ledstring.cpp` (`baseFrame`), `src/consoles.cpp` (the flag),
`src/network.cpp` (`/lights/*`)

## What shipped, and what is left

**Shipped.** A double-click of the knob toggles night mode, and
`/lights/on` and `/lights/off` do the same over HTTP. It paints the
*selected* role black — `StripFrame::nightMode`, honoured in
`resolvePixel()` — and nothing else. The browse, the travel and the
commit animation stay visible. It is RAM-only: not a config field, not
a menu row, gone on a restart.

**That answers the question this note left open** ("should browsing
stay bright while the selected window is dark?"): yes. It was also what
a brightness override got wrong — scaling every pixel to zero turned
the cabinet off exactly when it was being used.

**Left, deliberately.** Everything here describes a *config* feature
that is not built:

- `led.nightMode` as a persisted boolean, so a cabinet that is dark at
  2am is still dark after a power cut. The shipped version is not,
  and that is a judgement rather than an oversight: a room condition is
  not a cabinet preference, and a glowing cabinet after a power cut is
  the problem being solved. If that reasoning is wrong, this is the
  part to revisit.
- A menu row. It follows from persistence — a menu row that cannot be
  saved is a knob you have to remember.
- `led.nightMode` in the field registry and the OpenAPI schema.

## What

A toggle that makes the selected console's window on the GP21 string stay
dark instead of lit, so a cabinet left in a dark room does not glow.

- Double-click the rotary knob while a console is already selected →
  toggles night mode.
- While night mode is on, **every** selection ends with that console's
  window dark. Not "the double-click hides it once" — the mode persists,
  and the next selection stays dark too.
- Browsing still shows the blob and the preview, so the operator can
  still see what they are choosing. Only the *resting* selected window
  is affected. A browse that is abandoned must also settle dark.

**Config:** `led.nightMode`, boolean, default `false`.

## Why

The strip sits next to a television in a dark room. When nothing is
playing, a fully lit window is a glare problem — this is the same
reasoning already behind the conservative default colours in
`LedFeelDefaults.cpp`.

The trigger is a double-click on the knob rather than a menu item
because it is the one gesture that is already in the muscle memory: the
knob has two acts (turn to browse, click to commit) and a third that
only exists in this state is hard to discover and easy to hit by
accident while browsing.

## How

- Done: `EasyButton::onSequence(2, <ms>, cb)` on `rotarySelector`,
  registered in `controls_init()`. The detection is the library's; the
  only thing done by hand is refusing to let the pair's second press
  commit a console, which EasyButton cannot do — it calls the press
  callback before the sequence callback, both from the same release.
- Done: the suppression is in the core, in `resolvePixel()`, so it is
  host-testable. It was a palette edit in the shell first, which needed
  hardware to check.
- The blink rate in night mode should be distinguishable from the normal
  resting paint without being *slower* than the idle timeout — the ring
  fades in `ringFadeMs` and a slow blink would read as a fault.
- Decide whether night mode survives a reboot. See "Left, deliberately"
  above; the shipped answer is no.

## Not decided

- Whether the ring should also dim in night mode. Shipped answer: no —
  it follows `led.brightnessPct`, on the grounds that a hand reaching
  for the knob in the dark wants the ring to come on. Still worth the
  operator saying.
- Whether night mode should be a menu item as well as a double-click.
