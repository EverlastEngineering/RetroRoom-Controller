# AGENT.md — Working with RetroRoom-Controller

> This file is for AI coding agents (and humans driving tooling on the
> agent's behalf). It captures the rules, conventions, and gotchas that
> have already been paid for in past sessions. Read it before doing
> anything in this repo. Update it when a new lesson is learned.

## 0. What this project is

Firmware for a microcontroller that controls a multi-console retro
gaming switch box. Currently targeted at the **Raspberry Pi Pico 2 W**
(`[env:pico2w]`, RP2350 + on-board CYW43 WiFi). ESP8266 support was
dropped on `session/merge-pico-json`. Legacy `session/pico-migration`
kept the wired-only RP2040 path alive (envs `pico_base`, `picow`,
`pico_yd`); the active branch is `session/pico-2-wireless` which adds
the WiFi + AsyncWebServer bring-up.

Layout: `src/` for shell code, `lib/ConsoleConfig/` for the pure
functional core (console JSON parser + selection math), `test/`
for host-side Unity tests, `todo/` for per-item open/done/deferred
files, `pinouts/` for hardware notes, `pico-pin-mapping.md` for the
authoritative per-role pin table.

## 1. Build / Flash / Monitor (PlatformIO)

This is the **only** correct workflow. Do not invent shell wrappers,
agent-script helpers, or MCP tool servers. The prior session tried
that and deleted it (commit `5f6a554`) — the user does not want
workarounds, they want the standard PlatformIO commands used
correctly.

```sh
# 1. Build only (no flash) — headless-safe, runs anywhere
pio run -e pico2w

# 2. Build + flash (headless-safe). Uses picotool over /dev/cu.usbmodem*
#    by default; auto-detects the port.
pio run -e pico2w -t upload --upload-port /dev/cu.usbmodem11101

# 3. Build + flash + monitor — this is what you want when debugging a
#    boot sequence or watching the WiFi SoftAP come up. Use the repo's
#    wrapper (agent-script/pio-upload-monitor.sh) so the monitor
#    auto-closes after a timeout instead of running forever:
./agent-script/pio-upload-monitor.sh                     # default: pico2w, 25s window
./agent-script/pio-upload-monitor.sh -e pico_base -t 60  # different env / longer window
./agent-script/pio-upload-monitor.sh --no-build         # skip the standalone build step
./agent-script/pio-upload-monitor.sh --keep-heartbeats  # don't strip Heartbeat: lines

# 4. Monitor only — split step. Run in a separate terminal:
pio device monitor -p /dev/cu.usbmodem11101 -b 115200
```

The wrapper exists because `pio device monitor` requires a real TTY on
stdin (miniterm reads line-editing input), which means it gets
suspended by the OS if you run it as a background job in a non-TTY
shell. The wrapper uses `script(1)` to give the monitor a pseudo-tty
and SIGINTs it on a timer. Don't try to inline the `script(1) ... &`
pattern yourself every time — that's what the script is for.

`monitor_filters = direct` is set in `platformio.ini` so every byte
the firmware writes to `Serial` shows up unprocessed. Do **not** pipe
build or monitor output through `tail`, `grep`, or any other filter —
the user wants to see everything (LOG entry 2026-09-07, "Policy #6:
never hide user-facing command output").

The serial device is auto-detected at `/dev/cu.usbmodem*` on macOS. If
the wrong port is picked, pass `--upload-port /dev/cu.usbmodemXXXX`
explicitly.

## 2. RP2350 (Pico 2 W) gotchas

These are the rules that look like magic but are just how the chip
behaves:

- **BOOTSEL does NOT auto-eject on RP2350.** Unlike the RP2040 (where
  writing a UF2 unmounts `/Volumes/RPI-RP2/` and reboots into
  firmware), the RP2350 keeps `/Volumes/RP2350/` mounted after the
  flash write. The firmware boots only after one of: physical BOOTSEL
  button press, USB cable unplug + replug, or a 1200-baud reset
  sequence over USB-CDC from the host.
- **CDC-ACM buffer drops pre-host writes.** The Earle Philhower
  `rpipico2w` board target's USB-CDC ACM silently discards writes
  issued before the host opens the port. Boot logs printed before the
  user connects `pio device monitor` are lost. The current
  `setup()` (src/main.cpp) handles this with a 1 s `delay(1000)`
  before `Serial.begin()` plus a bounded `while (!Serial ... < 3000)`
  wait after. Don't remove those delays without a replacement.
- **LED_BUILTIN is GP64 on Pico 2 W** (was GP25 on the Pico / Pico-W
  / YD-RP2040). The framework resolves the right pin per board, so
  `pinMode(LED_BUILTIN, OUTPUT)` works on every target without
  conditional code.
- **On-board WS2812 is NOT present on the Pico 2 W** (that was a
  YD-RP2040 special). External WS2812 ring must be wired to
  `DATA_PIN` (currently GP4 per `src/configuration.h`).
- **1 Hz heartbeat blink on `LED_BUILTIN`** is the canonical "is the
  firmware alive?" signal. If the LED isn't blinking, `setup()` did
  not reach `pinMode(LED_BUILTIN, OUTPUT)` and the firmware wedged
  earlier — inspect `/Volumes/RP2350/INFO_UF2.TXT` to confirm the
  bootloader banner, or check the serial output for the last
  breadcrumb.

## 3. PlatformIO keys that aren't what they look like

Things that burned time and will burn yours if you don't know:

- **Flash / FS partition override.** The override is
  `board_build.filesystem_size = 1MB` (or `64KB`, `128KB`, etc.).
  This is read by the platform's
  `~/.platformio/platforms/raspberrypi/builder/main.py` via
  `board.get("build.filesystem_size")`. The keys
  `board_build.flash_length`, `board_build.flash_total`,
  `board_build.fs_start`, `board_build.fs_end`,
  `board_build.eeprom_start` are silently dropped by PlatformIO's
  menu-merge logic and don't reach the linker. The build prints
  `Filesystem size: 0.00MB` if you got it wrong, regardless of what
  you put in those keys.
- **CYW43 WiFi on Pico 2 W.** `khoih-prog/AsyncTCP_RP2040W` and
  `khoih-prog/AsyncWebServer_RP2040W` gate on
  `#if defined(ARDUINO_RASPBERRY_PI_PICO_W)` and reject
  `ARDUINO_RASPBERRY_PI_PICO_2W`. Use
  `ayushsharma82/RPAsyncTCP@^1.3.2` + `esp32async/ESPAsyncWebServer@^3.7.2`
  instead — those explicitly support both RP2040+W and RP2350+W on
  Earle Philhower's core.
- **AsyncWebServer C++ overloads.** `AsyncWebSocket::textAll` takes a
  `const char*` + `size_t`. Don't pass a `std::string` directly; the
  compiler picks a wrong overload and rejects it. Use
  `textAll(msg.c_str(), msg.size())`. Same for
  `AsyncWebSocketClient::text(...)`.
- **`monitor_filters = direct`** is required on `pico2w` to see the
  boot logs without miniterm munging line endings or adding
  timestamps. Already set in `platformio.ini` for `[env:pico_base]`
  (was added in commit `c6005c7`) and inherited by every env.

## 4. Hardware-permission policy

`pio ... -t upload` and `pio device monitor` may only run when the
user has **explicitly asked** for that action in the current
conversation. Building (`pio run -e pico2w`) is always fine; flashing
and serial attach are not. When in doubt, ask before uploading or
attaching the monitor. (LOG entry 2026-09-07, "Policy #5".)

## 5. Logging / decision records

Significant architectural decisions get logged at the top of `LOG.md`
in [MADR](https://adr.github.io/mdr/) format. The existing `AGENT.md`
references in `LOG.md` are historical — there is no live `AGENT.md`
elsewhere. Keep entries short and concrete: **Context** (what forced
the choice), **Decision** (what we picked and why), **Consequences**
(what changed and what to verify). Use UTC timestamps.

For routine TODO tracking, the `todo/` directory is the source of
truth: `open/` for active items, `done/` for finished ones (rename the
file to `<sha1-prefix>_<slug>.md` on completion so the filename itself
records the commit), `deferred/` for items parked indefinitely.

## 6. Tests

Unity tests for the functional core live in `test/test_console_config/`.
Run them with:

```sh
pio test -d . -e test_native
```

24/24 currently pass. The host environment is `[env:test_native]`
declared in `platformio.ini` — no Arduino toolchain required.

## 7. When in doubt

Read `readme.md` (high-level feature checklist), `pico-pin-mapping.md`
(authoritative pin table), `LOG.md` (recent decisions), and the
relevant file in `todo/open/` (current work). Don't read
`todo/done/` unless you're trying to understand history; everything
there is already shipped.
