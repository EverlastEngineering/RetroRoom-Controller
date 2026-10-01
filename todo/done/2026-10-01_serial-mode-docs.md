# Document the serial configuration channel

**Status:** open
**File anchor:** `lib/SerialCmd/src/SerialCmd.h`, `src/serialconfig.h`,
`src/serialconfig.cpp`

## What

The USB-serial channel is implemented and working, and documented
nowhere. The instruction screen inside the firmware is the only
description of it, and that is the one place nobody looks when they
have lost the device and found the menu.

Needs writing, in this order of who is stuck:

- **`readme.md`** — a short section: the cable is a configuration
  channel, here is the one thing to type to find out more, and here is
  when you would need it. Not a protocol reference; a signpost.
- **`example-configurations/README.md`** — what an operator should do
  with it, which is: get a config off the device before changing it, and
  put one on when they are done. The `GET CONFIG` / `PUT CONFIG` pair is
  a backup-and-restore and should be described as one.
- **A reference for the protocol itself** — the commands, the framing,
  the terminator, the bounds, and the errors. Probably
  `plans/serial-protocol.md`, alongside the other design notes in
  `plans/`.

## Why it needs writing at all

Because the channel exists for the case where nothing else works, and
the person in that case is the person who does not have the
documentation open. A recovery path whose only description is a screen
that scrolls past in two seconds is not a documented recovery path.

The Web Serial page the operator is building makes this more urgent
rather than less: it is a *client* of this protocol, and two
implementations of a protocol with no written specification is how they
disagree about where a `CONFIG DONE` line has to be.

## How

- The protocol is words, not paths, and case-insensitive:
  `?`/`help`, `STATUS`, `GET CONFIG`, `PUT CONFIG`, `SETUP WIFI`,
  `RESET`, `REBOOT`. Document that case does not matter, and that
  surrounding whitespace is ignored, because both are true and both are
  surprising otherwise.
- `PUT CONFIG` is terminated by a line reading exactly `CONFIG DONE`,
  and that is *not* a thing that can collide with a config. Worth
  stating rather than leaving as folklore: every key and every string
  value in JSON is quoted, so no bare token in a valid document can
  equal it. A tagline of `CONFIG DONE` arrives as
  `"tagline": "CONFIG DONE",` and does not match.
- `GET CONFIG` is length-prefixed, then raw bytes. Say why: a config
  contains newlines, so it cannot be framed by them, and a client that
  read to a terminator would have to know the document's shape.
- The session opens on `i` as the very first byte the host sends, or
  by itself on a cabinet whose web API is unusable. There is no way out
  but a reboot, which is deliberate and is the main thing a reader will
  want to argue with.
- The 8 KB cap and the paste-stall timeout are the two behaviours worth
  stating, because they are the two a client will hit.
- Note the one trap: a shell header and a library header must not differ
  only by case, or on macOS and Windows the compiler resolves one to
  the other with no error. That is in the file already; the protocol doc
  should repeat it, because a client author will not read this repo's
  source.

## Not decided

- Whether the Web Serial page on the operator's site becomes the
  primary documentation and the firmware's instruction screen becomes a
  fallback. If so, the protocol reference belongs there and this repo
  only needs a signpost. Worth settling with whoever owns the site
  before writing either.
- Whether `SETUP WIFI`'s visible password echo is acceptable long-term,
  or whether the channel should grow a no-echo mode. It is a local USB
  cable, so it probably is -- but it should be a decision rather than
  something nobody mentioned.
