# RetroRoom

Firmware for a multi-console retro gaming switch box, built around the
[StackSelector](https://github.com/) hardware — a board that adds an
expandable number of analog video inputs to a TV and makes choosing one
feel like turning a knob.

The controller sits between you and the TV: it drives the StackSelector,
lights a ring around the rotary encoder so you can see the selection,
runs an LED strip behind whichever console you're playing, and (once
configured) blaster-switches the TV to the right input so you never have
to reach for the remote.

## Hardware

- **Target:** Raspberry Pi Pico 2 W (RP2350 + on-board CYW43 WiFi).
  This is the only supported build target.
- **Pin map:** [`pin-map-chart.md`](pin-map-chart.md) is the source of
  truth. Wiring gets done from that table — check it before connecting
  anything.

## What it does

- Rotary encoder to browse the console list, click (or the hardware
  next/prev buttons) to commit. A console step takes several detents,
  and the LED strip shows a blob creeping toward the next console as
  they accumulate; the final detent snaps it onto a pulsing preview of
  the console a click would select. Spinning fast drops the detent
  requirement so long runs stay quick.
- FastLED ring around the encoder showing the current selection.
- A second LED strip segment per console, driven from the console's
  declared `ledPosition` / `ledWidth`. Committing twinkles the whole
  strip and settles it on the pixels above the selected console, so the
  strip reads as the stack filled down to your choice. Every timing and
  brightness is a `#define` in [`src/configuration.h`](src/configuration.h)
  — the feel can be retuned without touching the logic.
- 16×2 I²C LCD showing the console name and a per-console tagline,
  with a backlight that dims when the box is idle.
- IR blaster to switch the TV to the matching input.
- Capacitive touch on the encoder knob for proximity effects.
- WiFi setup portal, a web UI, a WebSocket for live state, and a REST
  API for the console list.
- Console list stored in LittleFS with rotating backups, so a bad write
  can't brick the config.

## Development

Build, flash, and test instructions live in
[`AGENT.md`](AGENT.md), which also documents the hardware-permission
policy and the platform gotchas. The helper scripts are in
[`agent-script/`](agent-script/); prefer those over ad-hoc command
lines.

For the API, the device serves its own OpenAPI spec at `/openapi`.

## Still to do

Tracked in [`todo/`](todo/README.md) rather than here, so this file
doesn't drift out of date.
