# Things a human has to test

Everything here is either only observable on hardware, or only
observable by eye. None of it can be settled by `pio test`, and some
of it cannot be settled by the e2e scripts either. Listed in the order
it was done, with what "working" means.

## 1. Serial configuration channel — documentation

**What changed:** the protocol is now written down, in
`plans/serial-protocol.md`, with signposts from `readme.md` and
`example-configurations/README.md`.

**Acceptance:** someone who has never seen the channel can open the
port, send `i`, and get a config off and back on, using only the docs.
`agent-script/e2e-serial.py` is the worked example and should agree
with them.

**Watch out for:** the doc claims things the e2e asserts. If they
disagree, one of them is wrong and the doc is the more likely culprit
because nothing checks it. Specifically check the 8 KB cap, the
"exactly one newline before `CONFIG DONE`" rule, and the two
auto-entry conditions.

## 2. A bool menu row says True/False, not 1/0

**What changed:** a menu item declared `"type": "bool"` shows a word.
The stored value is still 0/1; only the rendering changed.

**Acceptance:** long-press the knob, find `WiFi off`, click into it,
and read `True` or `False` on both rows — the current value and the
one you are turning to. Turn it, confirm, save, and check that
`GET CONFIG` shows `"disable": 1` or `0` as appropriate, matching what
the screen said.

**Watch out for:**
- Both rows. The editor row and the "Current:" confirmation are
  separate code paths and it is easy to change one.
- The confirmation is on screen for only `kMenuSavedMs` — a second and
  a half. Slow readers will see the editor row only.
- A bool has no detent gradient, so the knob gives no sense of how far
  you have turned. Deliberate (it alternates rather than steps) but
  worth confirming you can tell *which* way it went.

## 3. Night mode: double-click the encoder

**What changed:** a brightness *override* in memory, toggled by a
double-click of the encoder with no turning in between, and by
`/lights/*` over HTTP. Not saved, not in the menu, gone on restart.

> **This is not the night mode in
> `todo/open/2026-09-30_night-mode.md`,** which asked for something
> different and is still open. That note wanted a *persisted*
> `led.nightMode`, affecting only the resting selected window, with
> browsing still fully visible. What shipped is a temporary global
> level, chosen later and deliberately simplified to "brightness 0" in
> a dark room. The open question from the original note — should
> browsing stay bright while the selected window is dark? — is
> unanswered and is a real difference, not a detail.

**Acceptance:**
- Double-click with the knob still: the strip and the ring go dark.
  Double-click again: they come back at the brightness the config says.
- Turn the knob at all, then double-click: the lights should **not**
  toggle. That "no turns in between" condition is the whole point of
  the gesture and it is the part most likely to be wrong.
- Select a console while the lights are off: the strip must stay off.
  Turning a knob is a browse gesture, and the override must survive
  it.
- `GET /lights` reports the state, and setting brightness over HTTP
  changes it. Restart: back to the config's value, with no file
  changed and nothing to undo.
- Commit a console with the lights off. **This is the one to watch:**
  the commit animation is a burst of FastLED writes, and if the
  override is applied before them rather than after, the cabinet
  flashes for a moment on every selection.

**Watch out for:**
- The two presses must be within the double-click window *and* the
  encoder must not have moved since the first. A fast browse that ends
  in two quick presses will otherwise look like a double-click.
- Long press is already a gesture (it opens the menu). A long press
  must not also count as a double-click.
- The ring and the strip are driven from different places. Verify
  *both* go dark, and that the on-board status LED does not — it is a
  status indicator, not part of the show.
- A brightness override and a config `brightnessPct` change must not
  fight. Turning a console off and on should return to the config's
  value, not to the override's.

**The web API needs a network.** `GET /lights`, `/lights/off`,
`/lights/on` and `/lights/brightness/<n>` are only reachable once the
cabinet has joined one. The device is currently in factory state with
no credentials, so `SETUP WIFI` (serial) or the captive portal has to
come first. The double-click works with no network at all, which is
worth checking too — the whole point of a runtime override is that it
needs nothing configured.

## 4. Also worth a look, from earlier in this work

- **A save that needs a restart now restarts on its own.** Turn a
  setting that `configFieldNeedsReboot()` covers (`led.totalLeds`, or
  `network.disable`), save, and confirm the board goes down by itself
  after showing the notice. It used to require finding the Reboot row
  by hand.
- **The "not set up yet" pages.** On a cabinet with no config, you
  should see `Learn How To` / `Setup RetroRoom` for about five
  seconds, then `At everlast` / `engineering.com`, then the normal
  live view. They should *keep* appearing on every boot until someone
  puts a real config on it — that is intended, and it is the thing
  most likely to look like a bug.
- **`GET CONFIG` on a cabinet with nothing on flash** should return a
  real config labelled `built-in default`, not a message saying there
  is nothing.
- **The factory-reset page** claims it erases the config, the
  credentials and the selection. It now does all three plus both
  backups. Confirm the page text and the behaviour agree.

## Known gaps in what was built, on purpose

So none of these reads as an oversight discovered later:

- **The LCD backlight is not affected by night mode.** Only the LED
  string and the ring. The backlight is the other thing that lights a
  dark room, and it has its own setting
  (`lcd.backlightOffAfterMs`, which `0` turns off permanently), so
  there is already a config-level answer if you want one.
- **The IR blaster is not affected.** Switching the TV is the
  cabinet's job regardless of what the room looks like.
- **The on-board status LED is not affected.** It is a status
  indicator, and its three cadences already mean three different
  things.
- **Night mode is not in the menu**, by design. A menu row for it
  would mean a save, and the whole point is that it is temporary. It
  is in the API instead, and on the knob as a double-click.
- **The serial channel does not have it.** The user asked for this
  explicitly: serial is for configuration, not runtime operation, and
  a runtime verb there would be the first step towards it becoming a
  second remote control.
