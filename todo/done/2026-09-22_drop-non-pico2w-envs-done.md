# Drop non-`pico2w` build envs from `platformio.ini`

## Status

DONE 2026-09-22. `[env:pico_base]`, `[env:picow]`, `[env:pico_yd]`
removed from `platformio.ini`. Leading comment rewritten to state
the single-target policy and point at this file for historical
context. `[env:pico2w]` and `[env:test_native]` are the only
remaining envs.

Verification:

- `./agent-script/pio-build.sh` → SUCCESS (RAM 14.5% / Flash 14.2%,
  identical to pre-change numbers — confirms no source changes
  leaked in).
- `pio test -e test_native` → 35/35 tests pass.
- `pio run` (no env arg) now iterates only `pico2w` (test_native is
  test-only and stays out of the build matrix).

Originally added 2026-09-22 after the user said "We no longer need
to build for anything but the pico2w! PUT that in a todo, to remove
the esp32 and esp8266."

## Why

`platformio.ini` currently declares four target environments:

- `[env:pico_base]` — original Pico (RP2040), wired-only.
- `[env:picow]`     — Pico W (RP2040 + CYW43), wired-only (HAS_WIFI
                      intentionally not set on this env).
- `[env:pico_yd]`   — VCC-GND YD-RP2040 (used for on-board WS2812
                      smoke testing).
- `[env:pico2w]`    — Pico 2 W (RP2350 + CYW43). The active target.

Per AGENT.md §0/§4 (and the user memory `retroroom-build-workflow.md`):
**only `[env:pico2w]` matters.** The others are historical context
for ESP8266 (already dropped on `session/merge-pico-json`) and the
pre-CYW43 wired-only bring-up. Keeping them in `platformio.ini` has
real costs:

1. They are a tax on every `pio` invocation: PIO enumerates every
   `[env:*]` block even when `-e` is passed (slight but real).
2. New contributors (and new agents) waste time wondering "is
   pico_base the right env? what about pico_yd?" — AGENT.md already
   answers this but the platformio.ini clutter undermines that.
3. Pre-existing breakages on those envs are not my problem to fix
   (per the user memory) but they show up as `pio run -e pico_base`
   "sanity checks" that fail and tempt future agents into
   chasing them. Removing the envs kills the temptation.

## ESP32 / ESP8266

There is **no ESP32 `[env:*]` block** today — the platformio.ini
header comment already notes ESP8266 (`[env:nodemcuv2]`) was dropped
on `session/merge-pico-json`. The only ESP-flavored code path that
remains is `lib_deps = esp32async/ESPAsyncWebServer@^3.7.2` on
`[env:pico2w]` (the library happens to be ESP-tangential in name but
runs on RPAsyncTCP). Nothing to remove there — just confirming no
ESP32 env is hiding in the file.

## What to do

1. Delete the `[env:pico_base]`, `[env:picow]`, `[env:pico_yd]`
   blocks from `platformio.ini`.
2. Update the file's leading comment to say "single target:
   `[env:pico2w]`. Other envs have been removed; see
   git history for the previous multi-env structure."
3. Sanity check: `./agent-script/pio-build.sh` (pico2w) still
   succeeds. `pio test -e test_native` still passes (test_native is
   a separate env, keep it).
4. Commit: `Drop non-pico2w build envs from platformio.ini`.

## Out of scope

- Migrating `[env:pico2w]` to be the only env in a brand-new
  `platformio.ini` (overkill; just delete the three blocks).
- Removing `test_native` (it's the host-side test runner, separate
  concern).
- Touching any source file to remove `#if !defined(HAS_WIFI)`-style
  conditionals that the pico_base build used to need — if those
  guards still help pico2w (they do, since pico2w has HAS_WIFI set),
  they stay.

## Notes for whoever picks this up

The user's exact words: *"We no longer need to build for anything
but the pico2w!"* — so this is high-priority cleanup, not a
deferred nit. A 5-minute diff that I (or another agent) can ship
on the next session.
