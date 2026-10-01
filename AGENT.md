# AGENT.md — Working with RetroRoom-Controller

> Read this before doing anything in this repo. It records **rules and
> gotchas that cost real time**, not a copy of anybody's documentation.
> Update it when a new lesson is learned.

## 0. Documentation policy — read this first

This file is deliberately **not** a reference manual.

- **Do not restate platform or tool documentation.** How to invoke
  PlatformIO, what a build flag does, how the Arduino core or the CYW43
  driver behaves — look it up in the tool's own docs, or read the
  installed source. If a fact here conflicts with the tool, the tool is
  right and this file is stale.
- **Do not record facts that drift.** No test counts, no pass rates, no
  library versions, no LED blink cadences, no port names, no pin numbers
  outside `pin-map-chart.md`. Anything that changes when someone merges a
  commit belongs in the code, not here.
- **Record symptoms and where to look.** The durable content here is
  "you will see X, the cause is Y, go check Z" — not a transcription of
  what Z says.

If you find a stale statement in this file or `readme.md`, fix it in the
same commit as whatever you were doing. Do not add new ones.

## 1. The pin map is the source of truth

[`pin-map-chart.md`](pin-map-chart.md) is authoritative for every physical
pin. Hardware gets wired from it.

- **`src/configuration.h` must match it.** Every pin in the chart table
  gets exactly one `#define` in `configuration.h`, and nothing in
  `configuration.h` may claim a pin the chart doesn't list. If a pin is
  needed but isn't in the table, the table is the thing that's wrong.
- **If code and the chart disagree, the code is wrong.** Fix the code.
  Never "fix" the chart to match the code.
- **If the chart is missing a pin the code genuinely needs — or a pin
  collides, or a role is ambiguous — stop and raise it with the user
  before writing any code.** This is a blocking, high-priority question
  because the answer changes the physical wiring and can't be inferred
  from the source. Guessing here risks a mis-wired board.

Verify with a grep for the pin macros before and after touching
`configuration.h`.

## 2. Build / flash / monitor

Use the wrappers in [`agent-script/`](agent-script/). They exist because
the naive invocations have real failure modes, and they document the exit
codes.

| Script | Does |
|---|---|
| `pio-build.sh` | Compile only. No hardware touched. Always safe. |
| `pio-upload-monitor.sh` | Build + flash + serial capture on a timer. |
| `e2e-consoles-json.sh` | Drives `/consoles.json` against a running device. |
| `serial-snapshot.sh` | Non-interactive serial capture. |
| `ledstring-sim.sh` | Replays the GP21 string animations as ASCII, no hardware. |

`agent-script/pio-env.sh` is sourced by the others; it resolves the
PlatformIO CLI from `PATH` or `~/.platformio/penv/bin` and fails with
install instructions if it's genuinely absent. **If a wrapper says
"command not found", the fix is usually just that PlatformIO isn't on
`PATH` in non-interactive shells** — add it to `~/.zprofile` (macOS) or
`~/.bashrc`. Do not work around it by hardcoding paths into the scripts.

`ledstring-sim.sh` is the exception: it needs only a host C++ compiler,
and builds `lib/LedStringPaint` plus `agent-script/ledstring-sim.cpp`
directly. It replays the *same* `computeStripFrame()` call the firmware
makes, with the same `LEDSTRING_*` values, so the animations can be
inspected and retuned without flashing. Use it before changing a timing
or brightness in `src/configuration.h` — a wrong frame is much easier to
see here than on a strip behind a console.

### Build and flash as two separate steps

Never combine the build into the flash-and-monitor invocation. Do this
instead:

1. `./agent-script/pio-build.sh` — build, then **read the output and
   confirm it succeeded** before going any further.
2. `./agent-script/pio-upload-monitor.sh -e <env> -t 45` — flash and
   capture, with a window of **at least 30 seconds**.

The monitor window opens before the upload finishes, so a short window
expires mid-`Loading into Flash`, kills the pipeline, and leaves the
device in an indeterminate state with a truncated log — a failure that
doesn't announce itself. Building first means the window only has to
cover the flash, which makes the timing predictable. The wrapper clamps
anything under 30s, but that clamp is a backstop, not a licence to
combine the two steps.

Don't hand-roll `pio ... | tail` one-liners. The wrappers already handle
exit-code propagation and the monitor's TTY problem, and AGENT.md's
history records that inventing shell wrappers around this was explicitly
rejected before.

### Never filter the user's output

Do not pipe build or monitor output through `tail`, `grep`, `head`, or
any other filter. The user wants to see everything. (LOG entry
2026-09-07, "Policy #6".) If a wrapper truncates for you, it does so
deliberately and logs the full text to a file it tells you about.

## 3. Hardware permission policy

Flashing, attaching a monitor, rebooting the device, POSTing to a running
device, factory reset, or any other action that changes **hardware state**
may only run when the user has asked for it in the current conversation.
Two patterns count as explicit permission:

1. **Direct instruction** — "flash it", "upload and monitor", "POST the
   JSON to the device".
2. **Task-scoped delegation** — assigning a task that inherently requires
   hardware (e.g. "run the e2e suite against the device") and saying it's
   OK to perform those steps. Once granted, it covers the whole task; do
   not re-ask before each flash within the same task.

Building, running the host-side tests, and any host-side code change are
always fine. When in doubt, ask. (LOG entry 2026-09-07, "Policy #5".)

## 4. Platform gotchas worth remembering

The durable lessons. For the current API surface, read the tool.

- **RP2350 BOOTSEL does not auto-eject.** After a UF2 write the volume
  stays mounted and the board does not boot on its own. It needs a
  BOOTSEL press, a cable replug, or a 1200-baud USB reset.
  → Symptom: "flash succeeded, nothing happened." Check the mount name
  against your chip (RP2350 and RP2040 differ).
- **The USB CDC buffer drops writes made before the host opens the port.**
  Boot logs printed too early vanish. The bounded wait in `setup()`
  exists for this — don't delete it without a replacement.
- **`LED_BUILTIN` is not the same pin across boards.** The framework
  resolves the right one per board target, so always use the symbol.
  For what the blink *means*, read `src/state.cpp`.
- **Some AsyncWebServer dependencies are gated on older board macros and
  silently refuse newer ones.** If a build breaks on a board bump, check
  the library's `#if defined(...)` guards against the board symbol the
  new target actually defines.
- **`AsyncWebSocket::textAll` has overloads that don't accept a
  `std::string` directly.** Pass `.c_str()` and the size explicitly, or
  the compiler picks the wrong overload and rejects it.

## 5. Task tracking

`todo/` is the source of truth for outstanding work — `open/` for active,
`deferred/` for parked, `done/` for shipped. Read `todo/README.md` for
the conventions and the completion ritual.

`LOG.md` holds architectural decisions in MADR format, newest at the top.

## 6. Tests

Host-side Unity tests run through the `test_native` environment. Run them
and read the result — **don't rely on any number written in this file**,
including one you remember from a previous session.

```sh
./agent-script/pio-build.sh          # firmware compile check
./agent-script/ledstring-sim.sh      # replay the LED string animations
./agent-script/led-feel-check.sh     # configs still agree with the defaults
pio test -d . -e test_native         # host unit tests
```

**Run `led-feel-check.sh` before committing anything that touched
`LedFeel`, a default, or a config file.** It is the ritual that was
asked for and then forgotten: the `led` block exists in
`defaultLedFeel()`, in the config a fresh cabinet runs, and in seven
example configs, and copies drift silently. They had — three fields
were missing from the two configs that claim to be complete, and
nothing noticed until a test asked.

It fails on a config missing a field or naming one the code does not
have, and only *reports* a differing value, because
`led.totalLeds` is 118 in the configs (the fitted length) against 512
in the code (the build capacity) and that difference is deliberate.
`--verbose` prints every value.

Pure decision logic belongs in `lib/` (no Arduino/FastLED dependencies)
precisely so it is testable on the host. If you write logic that touches
hardware, push the *decision* down into a lib and unit-test it there.
Some `lib/` code is deliberately untested on the host — check the file's
header before assuming either way.

## 7. Adding a `led.*` setting

A new option in the config's `led` block touches **seven** places. Missing
one is how a setting ends up parsing and doing nothing — which happened
three times in a single session (a static initialiser, a `setup()` order,
a cached `configure()`), and each looked exactly like a working feature
that ignored its config.

1. **`lib/ConsoleConfig/src/ConsoleConfig.h`** — the field on `LedFeel`,
   with the comment saying what it trades off. This is the type's
   contract: *one number, one home*.
2. **`lib/ConsoleConfig/src/LedFieldTable.cpp`** — one row: key, member
   pointer, range. This is the **only** place a range is written, and the
   parser, the runtime setter and the menu all read it from here.
3. **`lib/ConsoleConfig/src/LedFeelDefaults.cpp`** — the default, beside
   the reasoning. The parser uses the struct's *current* value as the
   default for an absent key, so this file is the only place one is
   stated; put a literal in the table or the parser and you have two that
   can disagree with nothing to catch it.
4. **Wherever it is used** — and read `ledFeel` **live**, at the point
   of use, never into a variable that outlives the call. The rule is not
   "be careful when" but "do not keep a copy": the config menu changes
   these at runtime, and anything cached is a value the operator cannot
   change.
5. **`agent-script/led-feel-dump.cpp`** — one `FIELD(...)` row. It is
   generated from `defaultLedFeel()`, so it cannot drift; the one thing
   it *can* do is print the wrong number, so add the row.
6. **`example-configurations/README.md`** — the options table.
7. **`test/test_console_config/`** — that it parses, that an absent key
   takes the default, and that an out-of-range value is clamped **and**
   reported.

Then verify:

```sh
./agent-script/led-feel-dump.sh      # the new key appears, with the right default
./agent-script/pio-build.sh
pio test -d . -e test_native
```

**A setting the menu can reach also needs** a row in the config's `menu`
array — a line of JSON, because the menu resolves `set:` through the same
field table, not a per-key switch in the firmware.

Settings bound at init (`led.totalLeds` goes into `FastLED.addLeds()`)
must also be listed in `configFieldNeedsReboot()` in
`LedFieldTable.cpp`, so the menu prompt, the API and the docs cannot
disagree about which changes need a restart.

## 8. When in doubt

- Hardware question → [`pin-map-chart.md`](pin-map-chart.md), then
  `src/configuration.h`.
- Current work → `todo/open/`.
- Recent decisions and their reasoning → `LOG.md`.
- Adding a config setting → §7, which lists every file it touches.
- Project scope and feature state → `readme.md`.
