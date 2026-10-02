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
- A **cyclical light show** on the console strip: colour waves driven by
  four phase oscillators, moving through a cross-fading playlist of
  cpt-city gradient palettes. A port of FastLED's
  `colorwaveswithpalettes.ino`; the wave is
  [`lib/LightShow`](lib/LightShow), and it is started from the web API.
  It is **exclusive** — while it runs, the strip is the show, and any
  knob turn or click gives the strip straight back to being a console
  display.
- **Night mode**: a double-click of the encoder paints the *selected*
  console's window black, leaving the browse and the commit animations
  alone. Not saved, gone on a restart. See below.
- 16×2 I²C LCD showing the console name and a per-console tagline,
  with a backlight that dims when the box is idle.
- IR blaster to switch the TV to the matching input.
- Capacitive touch on the encoder knob for proximity effects.
- WiFi setup portal, a web UI, a WebSocket for live state, and a REST
  API for the console list.
- Console list stored in LittleFS with rotating backups, so a bad write
  can't brick the config.
- A configuration channel over the USB serial port, for the cases the
  network cannot cover — see below.

## Development

Build, flash, and test instructions live in
[`AGENT.md`](AGENT.md), which also documents the hardware-permission
policy and the platform gotchas. The helper scripts are in
[`agent-script/`](agent-script/); prefer those over ad-hoc command
lines.

For the API, the device serves its own OpenAPI spec at `/openapi`.

### If you can't reach it over the network

Everything above is HTTP, and HTTP needs a network. If the cabinet is
on a network that is down, has a radio switched off (`network.disable`
in the config), or is sitting in its own setup portal, open the USB
serial port at 115200 and send a single `i`. The device answers with a
list of what it will accept and stops its heartbeat until it is
restarted.

That channel is for configuration only — the config, the WiFi
credentials, a reset, a restart. It is deliberately not a second way to
drive the cabinet at runtime; lights and console selection are the web
API's business.

The protocol is in [`plans/serial-protocol.md`](plans/serial-protocol.md),
and [`agent-script/e2e-serial.py`](agent-script/e2e-serial.py) is a
worked client you can read or run.

## Still to do

Tracked in [`todo/`](todo/README.md) rather than here, so this file
doesn't drift out of date.
