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

`agent-script/pio-env.sh` is sourced by the others; it resolves the
PlatformIO CLI from `PATH` or `~/.platformio/penv/bin` and fails with
install instructions if it's genuinely absent. **If a wrapper says
"command not found", the fix is usually just that PlatformIO isn't on
`PATH` in non-interactive shells** — add it to `~/.zprofile` (macOS) or
`~/.bashrc`. Do not work around it by hardcoding paths into the scripts.

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
- **FastLED's RP2040/RP2350 backend uses `DATA_PIN` as a template
  parameter name,** which collides with our own `DATA_PIN` macro. The
  save/`#undef`/restore dance in `lighting.h` and `ledstring.h` is
  load-bearing — leave it alone.
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
pio test -d . -e test_native         # host unit tests
```

Pure decision logic belongs in `lib/` (no Arduino/FastLED dependencies)
precisely so it is testable on the host. If you write logic that touches
hardware, push the *decision* down into a lib and unit-test it there.
Some `lib/` code is deliberately untested on the host — check the file's
header before assuming either way.

## 7. When in doubt

- Hardware question → [`pin-map-chart.md`](pin-map-chart.md), then
  `src/configuration.h`.
- Current work → `todo/open/`.
- Recent decisions and their reasoning → `LOG.md`.
- Project scope and feature state → `readme.md`.
