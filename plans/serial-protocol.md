# The serial configuration channel

How to talk to a cabinet over the USB cable. The firmware is
`lib/SerialCmd/` (the protocol, pure and host-testable) and
`src/serialconfig.cpp` (the I/O); this is what a client needs to know.

## When you need this

The device is configured over HTTP: `POST /consoles.json`, or the
config menu on the knob. Both need a network. This channel does not,
so it is what you want when:

- `network.disable` is set, so the radio is off and there is no HTTP.
- the radio is dead, so no SoftAP ever came up.
- a saved network is wrong and the cabinet keeps dropping to SoftAP,
  where `/consoles.json` is not even routed.
- you want a backup before experimenting, and getting at
  `/consoles.json` means a join first.

## Getting in

Open the port at 115200 8N1 and press `i` **as the first thing you
send**:

```
> i
```

The device answers with the instruction screen and the heartbeat stops.
There is no way back to the heartbeat except a restart, which is
deliberate — the session is for configuring, and a session that
interrupts itself mid-transfer is worse than one that has to be
restarted.

Two automatic ways in, neither of which need the `i`:

- **Nothing configured.** The cabinet shows the "not set up yet" pages
  at boot and opens the session by itself.
- **No usable network.** No config on flash, the radio switched off, or
  the SoftAP up for want of credentials.

A cabinet that is configured *and* joined keeps its heartbeat and waits
to be asked. The `i` is honoured only as the first byte after boot, so
an `i` inside a pasted config cannot switch the heartbeat off on a
device whose only way back is a restart.

## Commands

Case-insensitive. Leading and trailing spaces are ignored; spaces
*inside* a command are not, because that is what separates the words.
A blank line produces no output at all — pressing Enter twice is not an
error.

| command | what it does |
|---|---|
| `?` or `help` | the instruction screen, on the device |
| `STATUS` | one line: console count, selection, config source, network mode, uptime |
| `GET CONFIG` | the config the cabinet is running, as bytes (below) |
| `PUT CONFIG` | replace it; ends at a line reading exactly `CONFIG DONE` |
| `SETUP WIFI` | prompts for the network name, then the password |
| `RESET` | erase everything and restart |
| `REBOOT` | change nothing, restart |

An unrecognised line gets one short line of output and nothing else —
no echo of what you typed, and never the whole command list. That is
deliberate: a device that dumps its vocabulary at anything you send is
unusable when you are holding down a key.

`SETUP WIFI` trims the network name at both ends, because a trailing
space is invisible at a prompt and produces a network that will not
join. The password is taken exactly as typed, spaces and all, and is
**shown as you type** — a raw line protocol has no way to suppress
terminal echo. On a local USB cable that seemed the right trade; it is
a decision rather than an oversight.

`REBOOT` does not save anything. The operator has a Save row on the
knob and there is no `save` verb here on purpose: a `REBOOT` that
quietly committed pending changes would be a second, invisible way to
write flash. Forgetting to save costs a lost change, which is the
recoverable direction.

## Framing, which is the part to get right

### Sending a config

```
PUT CONFIG
<the config, as text>
CONFIG DONE
```

The device re-prints the instruction screen in reply to `PUT CONFIG`,
so the terminator is in front of you rather than three lines up in a
scrollback you have read past. **One newline separates the last line of
the config from `CONFIG DONE`.** If your config already ends in a
newline, do not add another: that inserts an empty line, the device
reassembles it faithfully, and you get back a config one byte longer
than the one you sent. The device cannot tell an empty line you meant
from one you added by accident, and cannot do better — but it is
harmless, because trailing whitespace is valid JSON. The worst outcome
of getting this wrong is a length difference, never a corrupt config.

The paste may contain newlines freely. The terminator is matched as a
whole line, and that match cannot collide with the contents of a
config: every key and every string value in JSON is quoted, so no bare
token in a valid document can equal `CONFIG DONE`. A tagline reading
`CONFIG DONE` arrives as `"tagline": "CONFIG DONE",`.

`\r` is dropped wherever it arrives, so a config pasted from Windows
lands on flash byte-identical to the same config pasted from anywhere
else.

### Reading a config back

```
# config 6213 bytes (from flash):
<the bytes>
ok
```

Length-prefixed, then raw bytes. A config contains newlines so it
cannot be framed by them, and reading to a terminator instead would
require knowing the document's shape. The parenthesised origin says
where it came from:

- `from flash` — the operator's own config, byte for byte as written.
- `built-in default, nothing on flash` — no config on this device; what
  follows is the shipped one, which is a starting point to edit and
  `PUT` back.

So `GET CONFIG` on a fresh device is not a dead end. Take it, change
it, send it back.

## Limits

| | |
|---|---|
| config size | 8 KB, the same cap as `POST /consoles.json`. Over it, the paste is refused and nothing is written — and you are told it was too big, not that it failed to parse. |
| command line | 64 characters. Longer is junk, reported once, and discarded rather than interpreted. |
| paste stall | 5 seconds without a completed line and the transfer is abandoned and the parser goes back to listening. A client that dies mid-paste must not be able to wedge the one channel the device might need. |
| network name / password | 64 characters each. Longer is refused rather than silently truncated into a credential that will not connect. |

## Reading a reply

Read until the output goes quiet, with a timeout. There is no
terminator on the line-oriented replies — the device does not echo
prompts, because a prompt fights with the boot log and the heartbeat
for the same screen.

If you are reading through a tty rather than raw USB, clear `OPOST`
first (`stty -opost`): otherwise the tty rewrites every `\n` the device
sends into `\r\n`, and the length-prefixed header will not parse. A
browser using Web Serial reads raw USB bytes and never sees this.

## What the firmware guarantees

- A config is validated before anything is written. A bad paste is
  refused with the parser's own reason, and the running config is
  untouched — see `reject` and `truncated` in
  `agent-script/e2e-serial.py`, which assert that by reading the config
  back afterwards rather than trusting the reply.
- A successful write rotates the two backups and then restarts, about
  1.5 s later. **The restart takes the USB node with it**, so a client
  holding a serial handle will find it dead; that is expected, and the
  reply you may have been waiting for is one you will not get. Assert
  on the state after you reconnect.
- `RESET` erases the live config, both backups, the saved selection and
  the credentials. The backups matter: the boot path prefers
  `/consoles.json` then `.bak1` then `.bak2`, so leaving them would mean
  a "reset" cabinet came straight back up running the config it was
  supposed to have forgotten.
- A rejected or oversized paste does **not** restart anything, and does
  not disturb the selection.

## Two traps for anyone implementing a client

**A shell header and a library header must not differ only by case.**
On macOS and Windows, `serialconfig.h` and `SerialCmd.h` are the same
file to the compiler, and `#include <SerialCmd.h>` from inside the
shell's own translation unit silently resolves to the wrong one. It
cost an afternoon here and presents as a cascade of "does not name a
type" with no missing-include error anywhere.

**Only those commands exist.** `STATUS`, `GET`/`PUT CONFIG`,
`SETUP WIFI`, `RESET`, `REBOOT`. This channel is for configuration, not
runtime operation — lights, console selection and the rest are the web
API's business and are not being added here.

## Related

- `example-configurations/README.md` — the config format, and the
  `menu` array that decides what the knob can reach.
- `agent-script/e2e-serial.py` — a worked client, and the test suite
  that holds all of the above. It is the executable version of this
  document; if the two disagree, one of them is a bug.
