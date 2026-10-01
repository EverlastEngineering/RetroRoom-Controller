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

**What changed:** a flag in memory that paints the *selected* console's
window black. Toggled by a double-click of the encoder with no turning
in between, and by `/lights/on` / `/lights/off` over HTTP. Not saved,
not in the menu, gone on restart.

**It used to be a brightness override and that was wrong.** Scaling
every pixel to zero also scaled away the browse, the travel and the
commit animation — the parts of the cabinet you need to see when you
are using it. Setting `led.colors.selected` to `[0,0,0]` by hand does
exactly the right thing, so that is what the gesture does now, in
memory. It is also the question `todo/open/2026-09-30_night-mode.md`
left open: **yes, browsing stays bright while the selected window is
dark.** That note is still open for the other half of its idea —
persistence, a `led.nightMode` config field and a menu row.

**Acceptance:**
- Double-click with the knob still: the resting strip goes dark.
  Double-click again: it comes back at the colours the config says.
- Turn the knob at all, then double-click: the lights should **not**
  toggle. That "no turns in between" condition is the whole point of
  the gesture and it is the part most likely to be wrong.
- With the strip dark, browse to another console: the browse and the
  preview **must still light up**. That is the whole difference from
  the old override, so it is the thing to check first.
- Restart: back to the config's colours, with no file changed and
  nothing to undo.
- A double-click must **not** change the selected console. Only the
  first press of the pair commits; the second does nothing except
  toggle. If the cabinet re-fires IR and plays the commit animation on
  the second press, the guard in `rotarySelectorPressed()` is wrong.

**Watch out for:**
- **The double-click itself was broken and the cause was not the
  gesture.** The rotary selector was on EasyButton's interrupt mode,
  and in that mode `read()` throws away any pin change that arrives
  within the debounce window of the previous one — and because the ISR
  is what triggers `read()`, a thrown-away edge is never seen again.
  Losing the *release* leaves the library believing the knob is still
  down, so `update()` fires the 900 ms long-press callback with the
  knob up: the menu opened by itself. The selector is now polled every
  loop tick, which loses nothing. So: **try the double-click fast,
  slowly, and with a very short gap between the two presses** — the
  fast case is the one that used to fail.
- Long press is already a gesture (it opens the menu). A long press
  must not also count as a double-click, and a double-click must not
  open the menu.
- The ring is **not** affected by night mode. It follows
  `led.brightnessPct` like everything else. Reach for the knob in the
  dark and the ring still comes on — which is intentional, since the
  ring is the affordance that says the knob is there. Flag it if you
  disagree.
- The on-board status LED does not change either.

**The web API needs a network.** `GET /lights`, `/lights/off` and
`/lights/on` are only reachable once the cabinet has joined one. The
device is currently in factory state with no credentials, so
`SETUP WIFI` (serial) or the captive portal has to come first. The
double-click works with no network at all, which is worth checking
too — the whole point of a runtime mode is that it needs nothing
configured.

`GET /lights/brightness/<n>` is **gone**, deliberately. A level is a
config setting (`led.brightnessPct`); a URL that can change it behind
the config menu's back is a second writer for one number. It was
removed with the override it came with, so if anything still calls it,
it will 404.

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
  string. The backlight is the other thing that lights a dark room,
  and it has its own setting (`lcd.backlightOffAfterMs`, which `0`
  turns off permanently), so there is already a config-level answer if
  you want one.
- **The ring is not affected by night mode.** It follows
  `led.brightnessPct`, on the grounds that a hand reaching for the
  knob in the dark wants the ring to come on. The original note asked
  whether it should; this is the answer, and it is a judgement call
  rather than a fact.
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
