# Lock-down mode

**Status:** open
**File anchor:** `lib/ConsoleConfig/` (the `menu` array), the openapi spec
in `src/html/openapi.yaml`, `src/network.cpp`

**Note:** the original note read "lock down mode that requires" and
stopped. What follows is my best reading of the intent, and it is a
guess — correct it before working on this.

## What

A mode in which the cabinet cannot be changed by anyone who walks up to
it.

The premise is already in the design and is worth stating: the menu is
**defined in JSON**, so the operator can leave out anything they do not
want adjustable. Lock-down is what you get when the operator leaves out
everything.

So the question is what is left:

- With an empty `menu` array, there is no long-press target at all. That
  may be the entire feature.
- Or: a separate `locked` flag that hides the menu and rejects a
  double-click, leaving the hardware endpoints open to whoever has the
  WiFi password.

## Why

The cabinet lives in a room, and the menu is a way for someone in that
room to change how the cabinet behaves — including to set a light show
running. If the menu is a fixed list of firmware features, it is a list
of things somebody can always reach. If it is a list the operator wrote,
then locking it down is deleting it, and there is no separate lock to
get wrong.

This is the strongest argument for the data-driven menu that motivated
it, and it is worth writing down before the menu gets comfortable,
because a comfortable menu is a menu that grows a hardcoded escape hatch.

## How

- If lock-down is "the menu is empty", the work is: make an empty or
  absent `menu` array mean *no long-press target* rather than *an empty
  menu that still opens*. That is a small, testable rule in
  `lib/CabinetMenu`.
- If it is a separate flag, decide whether it is itself settable from the
  menu. It must not be, or it is not a lock.
- The HTTP surface is a separate question and probably the harder one.
  `/consoles.json` can be posted by anyone who knows the SoftAP password,
  and a posted file with a `menu` array in it is a posted file that adds
  menu items. Lock-down has to cover the upload endpoint, not just the
  knob, or it is a lock on one of two doors.
- Document in `example-configurations/README.md` what an empty `menu`
  means, since it is now a security-relevant setting.

## Not decided

- Whether lock-down should be a *pin*, a physical switch on the
  perfboard, or a JSON key. All three are defensible and they have very
  different recovery stories — a forgotten PIN on a cabinet in a room
  somebody else can walk into is a problem.
