# The config backups exist and are never used

**Status:** open
**File anchor:** `src/consoles.cpp` (`consoleDefinitions()`),
`src/consoleconfig_store.{h,cpp}`, `lib/ConsoleConfig/src/ConsoleConfig.cpp`
(the writer)

## What

A cabinet that boots with a broken `/consoles.json` currently boots with
**no console list at all**. It should instead fall back — and say so.

```
/consoles.json      -> /consoles.bak1 -> /consoles.bak2 -> PROGMEM default
```

each candidate validated by *parsing* it, not merely by opening it, and
the choice reported on serial and on the LCD.

## Why

The two backup slots already exist and are already written. They are
**write-only**: `saveConsoleConfigWithBackups()` rotates them on every
upload and every menu save, `loadBackupSlots()` can read them, and
nothing calls it at boot. So the cabinet has been diligently keeping
two copies of its configuration and has never once restored one.

That gap only shows up when something goes wrong, which is the worst
time to discover it. The two ways that happens:

1. **A power cut during a write.** LittleFS writes are not atomic, so
   the file on flash can be truncated part way through. The write-side
   check (`writeFileFrom` comparing bytes written) never runs, because
   the power is off. The cabinet then boots to a file that will not
   parse, prints `Console config load failed`, and has nothing.

2. **An upload that writes a bad document.** `POST /consoles.json`
   writes what it is given. A truncated POST body, a partial paste, a
   laptop that dropped the connection — and a bad document is now the
   *live* one, with the previous good one sitting unused in `.bak1`.

Both leave the cabinet with no consoles: no ring, no strip, no LCD name,
nothing to fix it with. From across a room that is indistinguishable
from a dead board.

## How

**The fallback chain belongs in `consoleDefinitions()`**, which is
already the place that decides where the config comes from. It reads
the live file, falls back to the embedded PROGMEM default, and should
try the backup slots in between.

- **Validate by parsing, per candidate.** A slot that exists but holds
  a truncated document must not be used just because it opened. This is
  the whole point: the failure mode being defended against is a file
  that is present and unreadable.
- **Keep the PROGMEM default last.** It is the only candidate that
  cannot be corrupted, and a cabinet running the example config beats a
  cabinet with nothing. It is also the *wrong* config for a real
  cabinet, which is why falling through to it deserves a loud message
  rather than silence.
- **Report which one was used**, on serial and on the LCD. A cabinet
  quietly running a two-week-old config is worse than one that says so,
  because the operator has no way to tell that the change they made
  last week is not in effect. The LCD line matters more than the serial
  one: the serial is for the person with a laptop, the LCD is for the
  person standing in the room.

**Do not auto-repair.** If boot falls back to `.bak1`, the live file is
still broken, and writing the recovered copy over it destroys the only
evidence of what went wrong. Falling back on every boot is a nuisance;
losing the ability to diagnose it is permanent. Report it loudly and
let a re-upload fix it.

**Close the other half of the hole.** The menu's save path is safe by
construction — `applyConfigEdits()` parses before it writes, so what
lands on flash is valid by definition. `POST /consoles.json` is not: it
writes whatever arrived. Validating the upload the same way is a small
change and removes the second of the two ways to get here.

**Consider temp-file-plus-rename** so a save is atomic rather than
truncating-then-completing. Only worth it if LittleFS's rename is
atomic on this partition — check before promising it. The fallback
chain above is worth having regardless, and is the part that actually
protects the cabinet.

## Open questions

- **Does the LCD have a state for this?** The display owns its own
  welcome → live progression. "Using a backup config" is not a line of
  a console's tagline; it is a condition, and it may want a banner or a
  phase of its own. Worth deciding before the fallback lands rather
  than bolting a string onto the live view afterwards.
- **Should a fallback be sticky across a reboot?** It will be, as long
  as the live file stays broken. That argues for the operator being
  told on *every* boot rather than only the first, which is the opposite
  of the usual "only show it once" instinct.
- **Are the backups reachable over the web UI?** They would make a
  "restore the config I had last week" button almost free, and they are
  the natural answer to the operator whose upload just went wrong. But
  it is also a second door onto the config, and
  `2026-09-30_menu-lock-down-mode.md` is about how many doors there
  should be. Decide them together, not one at a time.
- **How far back is far enough?** Two slots is what exists. A third
  costs flash this part may not want, and buys a case nobody has hit
  yet. Leave it at two unless there is a reason.

## Not decided

Whether a fallback should also apply to a config that *parses* but is
wrong — a valid document naming consoles that are not in the cabinet, or
one whose `totalLeds` is nonsense. That is a different failure with a
different fix (validation, not recovery) and conflating them would make
both worse.
