# The LCD backlight stays fully lit while the menu is open

**Status:** open
**File anchor:** `src/display.cpp` (`display_wake()`,
`lcdBacklightOffAfterMs`), `src/controls.cpp` (the menu open/close)

## What

While the menu is on screen, the LCD backlight ignores
`lcd.backlightOffAfterMs` and stays at full.

## Why

The backlight timeout exists so a cabinet nobody is using does not glow
in a dark room. A menu is the definition of using it. The timeout firing
halfway through reading a value is the single most obvious way for a
menu to feel broken, and the operator's reaction will be to think the
cabinet has frozen rather than that it tidied up after itself.

The stated requirement is *fully* lit, not "reset the timer" — so a
reset is probably the wrong implementation even though it is the
obvious one. A menu left open by someone who walked away should not be a
new glow-in-the-dark problem, and a long menu interaction is a long time
to have overridden the room.

## How

- Do not special-case the menu inside the backlight timer. Make the
  timer ask the question it should be asking — "is anything asking for
  the backlight to stay on?" — and have the menu be one of the things
  that can ask. `display_wake()` is already the entry point and is
  called from `setup()` for exactly this kind of reason, so the seam
  probably already exists.
- The thing to get right on exit: the override must be *released*, and
  the post-menu timeout must start from the moment the menu closed rather
  than inheriting the time already served. Otherwise opening and closing
  a menu repeatedly keeps the backlight on indefinitely.
- If the operator walks away with the menu open, the menu needs its own
  timeout. An open-ended override on a cabinet in a dark room is a
  regression, not a feature — and it is the same failure
  `2026-09-30_led-brightness-config.md` would have if brightness were
  left turned up.
- Decide whether "fully lit" means the backlight PWM goes to its maximum
  or just that the normal auto-off is suspended. The display has a
  backlight-off PWM path — `display.cpp` references it — so "full" may
  be a distinct state rather than the absence of a dim.

## Not decided

- The menu's own idle timeout. Long enough to read a value and change
  it, short enough that a walked-away cabinet settles. Related to
  `led.attractDwellMs` in spirit but a different question: attract mode
  is a feature, this is a courtesy.
