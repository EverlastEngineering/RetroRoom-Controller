#!/usr/bin/env python3
"""End-to-end test for the USB-serial configuration channel.

The sibling of agent-script/e2e-consoles-json.sh, for the channel that
has to work when the HTTP one cannot. Every assertion reads the
device's own answer back and checks the *effect*, not the status: a
config is PUT, then GET, and the bytes are compared.

What this covers that the host tests do not: the shell half. The
parser in lib/SerialCmd has ~26 host tests, but none of them touch
LittleFS, the validator, the backup rotation or the reboot deadlines,
and those are exactly the parts that can be wrong in a way no unit
test would notice.

Scenarios:

  help          `?` prints the command list, and nothing else does
  garbage       an unknown line is one terse line, no echo, no help
  blank         an empty line is silence
  status        STATUS reports config source and network mode
  get           GET CONFIG returns a parseable config
  roundtrip     PUT CONFIG then GET CONFIG returns the same bytes
  reject        a config that cannot parse is refused and changes
                nothing -- the important one, because the failure
                mode looks exactly like the success mode from outside
  truncated     a paste over 8 KB is refused rather than saved
  reset         RESET erases everything and the device reboots into
                setup, with no config on flash afterwards

Note on `reset`: it destroys the device's configuration, so it is not
in the default set. Pass --scenario reset, or --all-with-reset.

Usage:
    ./agent-script/e2e-serial.sh                     # safe scenarios
    ./agent-script/e2e-serial.sh --list
    ./agent-script/e2e-serial.sh --scenario roundtrip
    ./agent-script/e2e-serial.sh --all-with-reset    # destroys the config
    ./agent-script/e2e-serial.sh --port /dev/cu.usbmodem1421
    ./agent-script/e2e-serial.sh --config example-configurations/example6-all-options.json

Exit codes:
    0  every selected scenario passed
    1  bad CLI args
    2  the serial port could not be opened (nothing else can be known)
    3  a scenario failed

Each scenario prints SCENARIO:<name>:PASS or SCENARIO:<name>:FAIL so a
CI runner can grep them out, matching the sibling script.
"""

import argparse
import json
import os
import re
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.stderr.write(
        "FATAL: pyserial is required. Run this through\n"
        "  ./agent-script/e2e-serial.sh\n"
        "which uses the interpreter PlatformIO already installed it in.\n"
    )
    sys.exit(1)

BAUD = 115200
# Long enough for a 6 KB paste to drain and for the device to validate
# and write it, with room to spare. Bounded so a wedged device fails
# the scenario rather than hanging the suite.
REPLY_TIMEOUT = 6.0
# After RESET or a save-and-restart the device reboots, re-runs its
# blocking setup pages and opens the session again. Give it room.
REBOOT_TIMEOUT = 30.0

# The 8 KB cap the firmware enforces. The truncated scenario sends a
# little over this.
MAX_BODY_BYTES = 8 * 1024


def _candidate_ports():
    import glob
    return sorted(glob.glob("/dev/cu.usbmodem*") +
                  glob.glob("/dev/tty.usbmodem*"))


def _other_holders(port):
    """PIDs other than us holding `port`, best effort.

    A tty does not have exclusive access on macOS, so a second reader
    is possible and the failure is silent: the port opens fine, the
    commands go out, and the *replies* go to whichever process reads
    first. Every assertion then fails on an empty string, and the real
    cause -- somebody else's monitor is attached -- appears nowhere in
    the output. It cost two debugging rounds here, so it is named.
    """
    import subprocess
    try:
        out = subprocess.run(["lsof", "-t", port], capture_output=True,
                             timeout=5).stdout
    except Exception:
        return []
    mine = {os.getpid(), os.getppid()}
    pids = []
    for line in out.decode("utf-8", "replace").split():
        try:
            pid = int(line)
        except ValueError:
            continue
        if pid not in mine:
            pids.append(pid)
    return pids


def open_port(port, timeout=20.0):
    """Open the port, retrying while it is absent.

    A board that restarts re-enumerates its USB, and on macOS the
    device node briefly does not exist. Every command that restarts
    the device therefore has to be able to get the port back, so this
    is not a nicety -- without it the suite dies on the first reboot,
    which is the first thing roundtrip does.
    """
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        if port is None:
            ports = _candidate_ports()
            if not ports:
                time.sleep(0.3)
                continue
            port = ports[0]
        holders = _other_holders(port)
        if holders:
            raise Failure(
                "%s is held by pid%s %s. Close the other monitor and "
                "re-run -- a shared port splits the replies between two "
                "readers, so every command looks like it was ignored."
                % (port, "s" if len(holders) > 1 else "", holders))
        try:
            return Device(port)
        except (serial.SerialException, OSError) as exc:
            last = exc
            time.sleep(0.5)
    raise Failure("could not open serial port %r: %s" % (port, last))


class Device:
    """A line-and-bytes conversation with the device.

    The protocol is newline-terminated commands, and a length-prefixed
    body for GET CONFIG. `read_until_idle` is the primitive: read until
    nothing has arrived for a quiet period, which is the only framing
    that works when the body is raw JSON that itself contains newlines.
    """

    def __init__(self, port, quiet=0.35):
        self.port = port
        self.ser = serial.Serial(port, BAUD, timeout=0.2)
        # Turn off output post-processing, or the tty rewrites every
        # "\n" the device sends into "\r\n".
        #
        # This is a host artefact, not something the firmware does: its
        # replies are plain "\n". But it matters more than tidiness --
        # a browser using Web Serial reads raw USB bytes and would see
        # the untranslated stream, so a test that lets the tty
        # substitute characters is testing something no real client
        # will ever see. pyserial does not clear OPOST on its own.
        #
        # (The input side needs nothing: the firmware already drops a
        # "\r" on arrival, so a client sending CRLF is fine.)
        try:
            import termios
            iflag, oflag, cflag, lflag, ispeed, ospeed, cc = termios.tcgetattr(
                self.ser.fileno())
            termios.tcsetattr(
                self.ser.fileno(), termios.TCSANOW,
                [iflag, oflag & ~termios.OPOST, cflag, lflag, ispeed, ospeed, cc])
        except Exception:
            # Not fatal: the parsing below tolerates CRLF anyway, so a
            # platform where this fails still gets correct results.
            pass
        self.quiet = quiet
        self._buf = b""

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _pump(self, deadline):
        """Read until `deadline`, or until `quiet` seconds of silence.

        Raises whatever the port raises. A device that restarts takes
        the USB node with it, and this is where that shows up: the
        `in_waiting` ioctl is the one call pyserial does *not* wrap, so
        a vanished port arrives here as a bare OSError rather than a
        SerialException. Every caller that can race a restart catches
        both.
        """
        last = time.time()
        while time.time() < deadline:
            waiting = self.ser.in_waiting
            if waiting:
                chunk = self.ser.read(waiting)
                if chunk:
                    self._buf += chunk
                    last = time.time()
                    continue
            if self._buf and (time.time() - last) >= self.quiet:
                return
            time.sleep(0.02)
        # Fall out on the deadline; whatever arrived is what we got.

    def _take(self):
        out, self._buf = self._buf, b""
        return out

    def drain(self, timeout=1.0):
        """Throw away anything pending. Used before a command so a
        previous reply cannot be mistaken for this one's."""
        self._buf = b""
        self._pump(time.time() + timeout)

    def send_line(self, line):
        self.ser.write((line + "\n").encode())
        self.ser.flush()

    def read_reply(self, timeout=REPLY_TIMEOUT):
        self._pump(time.time() + timeout)
        return self._take()

    def command(self, line, timeout=REPLY_TIMEOUT):
        self.drain(0.3)
        self.send_line(line)
        return self.read_reply(timeout)

    def put_config(self, payload, timeout=REPLY_TIMEOUT):
        """PUT CONFIG, send `payload`, terminate. Returns (ack, reply).

        The terminator is *a line*, so exactly one newline has to
        separate the last line of the payload from it. A file that
        already ends in a newline -- which is most of them -- therefore
        needs no extra one, and adding one anyway puts an empty line
        in the paste, which the device faithfully reassembles and which
        comes back as a config one byte longer than the one sent.

        That is worth being precise about, because the alternative is
        worse: the device cannot tell an empty line the host meant
        from one the host added by accident, and silently choosing
        either would make byte-exact round trips impossible. It adds the
        newline and the JSON is still valid -- trailing whitespace is
        ignored -- so the worst outcome of getting this wrong is a
        length difference, never a corrupt config.

        So the rule lives here, once, rather than at three call sites.
        """
        self.drain(0.3)
        self.send_line("PUT CONFIG")
        ack = self.read_reply(2.0)
        self.ser.write(payload)
        if not payload.endswith(b"\n"):
            self.ser.write(b"\n")
        self.ser.write(b"CONFIG DONE\n")
        self.ser.flush()
        return ack, self.read_reply(timeout)

    def open_session(self):
        """Send the `i` that opens the interactive session.

        Only honoured as the very first byte the host sends, so a
        stray `i` in a later paste cannot reopen it -- which means
        this has to come before anything else, and that sending it to a
        device whose session is *already* open puts a junk character in
        the line buffer.

        So this is re-runnable on purpose. A device that is already in
        a session answers with an error instead of the screen, and the
        `i` is then sitting in the parser waiting for a newline; send
        one to flush it. Without this the script could only ever be
        run once after a reboot, which for a test script is close to
        useless.
        """
        self.drain(1.0)
        self.ser.write(b"i")
        self.ser.flush()
        out = self.read_reply(REPLY_TIMEOUT)
        if b"interactive mode" not in out:
            # Already open. Throw away the character we just added.
            self.ser.write(b"\n")
            self.ser.flush()
            self.drain(0.5)
            return b""
        return out


# ---------- assertions ----------

class Failure(Exception):
    pass


def assert_true(cond, message):
    if not cond:
        raise Failure(message)


def assert_eq(expected, actual, label):
    if expected != actual:
        raise Failure("%s: expected %r, got %r" % (label, expected, actual))
    print("  ok    %s = %r" % (label, actual))


def assert_in(needle, haystack, label):
    if needle not in haystack:
        raise Failure("%s: %r not found in:\n    %s"
                      % (label, needle, haystack.decode("utf-8", "replace")))


# The header is the device's own, and \r is tolerated on the off
# chance a client is reading through something that still does output
# post-processing. The *body* offset is taken from the match, so a
# tolerated \r does not shift a single byte of the config.
HEADER_RE = re.compile(rb"^# config (\d+) bytes \((.*)\):\r?\n")


def as_text(raw):
    """Decode a line-oriented reply for substring assertions.

    Strips \r so a host that post-processes output does not change the
    answer. Never use this on a GET CONFIG body -- the bytes there are
    data, not text.
    """
    return raw.decode("utf-8", "replace").replace("\r", "")


def parse_config_reply(raw):
    """Split a GET CONFIG reply into (origin, body_bytes).

    The header carries the length and where the config came from; the
    body is exactly that many bytes, and whatever follows is the
    `ok` terminator. Reading to a terminator instead would require
    knowing the document cannot contain it, which is true for CONFIG
    DONE but not for `ok`.
    """
    m = HEADER_RE.match(raw)
    if not m:
        raise Failure("malformed config header in:\n    %s"
                      % raw[:200].decode("utf-8", "replace"))
    length = int(m.group(1))
    origin = m.group(2).decode()
    body = raw[m.end():m.end() + length]
    if len(body) != length:
        raise Failure("config truncated: header said %d, got %d"
                      % (length, len(body)))
    assert_in(b"\nok", raw[length:], "terminator after config body")
    return origin, body


# ---------- scenarios ----------

def sc_help(dev, args):
    out = dev.command("?")
    for word in (b"GET CONFIG", b"PUT CONFIG", b"SETUP WIFI", b"RESET",
                 b"REBOOT", b"STATUS", b"CONFIG DONE"):
        assert_in(word, out, "help mentions %s" % word.decode())
    # Help is on demand and nowhere else. This is the anti-flood
    # property: a stream of junk must not produce the command list.
    junk = dev.command("FLARGLE")
    assert_in(b"unknown command", junk, "unknown command is terse")
    assert_true(b"GET CONFIG" not in junk,
                "an error must not dump the whole vocabulary")
    assert_true(b"FLARGLE" not in junk, "input is not echoed back")
    blank = dev.command("")
    # Not "the port is silent". A connected web client produces its own
    # lines here -- `net: ws rx: healthcheck` from a browser polling the
    # UI, for instance -- and the port is shared output, so silence is
    # not a property this channel can have.
    #
    # The property that matters is that a blank line is not a *command*:
    # no error, and above all not the command list. Pressing Enter twice
    # is not a mistake and must not be answered like one.
    assert_true(b"err:" not in blank,
                "a blank line must not be answered with an error, got %r"
                % blank[:200])
    assert_true(b"GET CONFIG" not in blank,
                "a blank line must not print the vocabulary")
    # And the next real command still works, which is the half that
    # would actually break if a blank line had been mishandled.
    TEST_STATUS_OK = dev.command("STATUS")
    assert_true(b"status:" in TEST_STATUS_OK,
                "the parser is still answering after a blank line")
    # Nothing here restarts the device, so the same handle stays good.
    return dev


def sc_status(dev, args):
    out = dev.command("STATUS")
    text = as_text(out)
    # One field at a time rather than one anchored pattern. The status
    # line is allowed to grow -- it already gained `uptime` -- and an
    # assertion that breaks because a field was *added* is an
    # assertion about the order of the fields, which is not what it
    # claims to be about.
    def field(name):
        m = re.search(r"\b%s=(\S+)" % name, text)
        if not m:
            raise Failure("STATUS has no %s in %r" % (name, text))
        return m.group(1)

    consoles = int(field("consoles"))
    assert_true(consoles > 0, "there should be consoles loaded")
    config = field("config")
    assert_true(config in ("uploaded", "shipped-default"),
                "config source is one of two named states, got %r" % config)
    net = field("net")
    assert_true(net in ("sta", "ap", "off"),
                "network mode is one of three, got %r" % net)
    print("  ok    config=%s net=%s consoles=%d" % (config, net, consoles))
    return dev


def sc_get(dev, args):
    out = dev.command("GET CONFIG")
    origin, body = parse_config_reply(out)
    # Whatever the device is running, it must be a document we can
    # actually parse. This is the assertion that matters: a config
    # nobody can load is a cabinet that boots empty.
    try:
        doc = json.loads(body.decode("utf-8"))
    except Exception as exc:
        raise Failure("GET CONFIG returned something unparseable: %s" % exc)
    for key in ("irCodes", "consoleNames", "consoles"):
        assert_true(key in doc, "config has %s" % key)
    assert_true(len(doc["consoles"]) > 0, "config has at least one console")
    # The origin in the header has to agree with what the body is. A
    # device with nothing on flash must say so rather than passing off
    # the factory config as the operator's.
    if "nothing on flash" in origin:
        assert_eq("shipped-default", "shipped-default",
                  "no-file device labels its config as the default")
    print("  ok    %d bytes from %s" % (len(body), origin))
    return dev


def _paste_config(dev, port, payload):
    """PUT CONFIG, send `payload`, and return a reconnected Device.

    A successful save restarts the board about a second and a half
    later, and a restart re-enumerates the USB -- so the port can die
    while we are still reading the reply. That is not a failure and it
    is not even unusual; it is what a save *is* here. A SerialException
    or OSError after the paste is swallowed, and the assertion that
    matters is the one made after the reconnect: what the device is
    running now.

    Asserting on the reply we might never see would be asserting on
    timing rather than on behaviour. When the device *does* answer --
    which it does whenever it refuses the config, because a refusal
    restarts nothing -- the reply is returned so the refusal can be
    checked as a refusal.

    Returns (reconnected_device, reply_or_empty).
    """
    reply = b""
    try:
        ack, reply = dev.put_config(payload)
        # The acknowledgement IS the instruction screen, by design: the
        # person is about to paste something and the terminator has to
        # be in front of them rather than three lines up in a scrollback
        # they have read past. Asserting on "ok" would be asserting
        # against a deliberate decision; assert on what the screen is
        # for.
        assert_in(b"CONFIG DONE", ack,
                  "PUT CONFIG re-prints the instructions, terminator included")
    except (serial.SerialException, OSError):
        pass
    return _reconnect(dev, port), reply


def sc_roundtrip(dev, args):
    port = dev.port
    payload = args.config_bytes
    _origin, before = parse_config_reply(dev.command("GET CONFIG"))

    dev, res = _paste_config(dev, port, payload)
    # A refusal is still visible, because a device that rejected the
    # paste does *not* restart and its reply does arrive.
    if res:
        assert_in(b"accepted", res, "device accepted the config")

    _origin2, after = parse_config_reply(dev.command("GET CONFIG"))
    # Byte equality, not a re-parse. The HTTP GET has the same
    # property and the same reason: the point is that what was PUT is
    # what is on flash, and a parse-and-compare would pass even if the
    # device had reformatted the document on the way through.
    assert_eq(len(payload), len(after), "round-tripped length")
    if payload != after:
        for i, (a, b) in enumerate(zip(payload, after)):
            if a != b:
                raise Failure("round-trip differs at byte %d: %r vs %r"
                              % (i, payload[max(0, i-40):i+40],
                                 after[max(0, i-40):i+40]))
        raise Failure("round-trip differs in length but not in prefix")
    print("  ok    %d bytes survived the round trip" % len(payload))
    return dev


def sc_reject(dev, args):
    _origin, before = parse_config_reply(dev.command("GET CONFIG"))
    broken = b'{"irCodes": {"Video": "0x430"}, "consoles": [ this is not json'
    # A rejected config does not restart anything, so this one does not
    # need the reconnect -- and saying so is the point: a rejection has
    # to leave the device *running*, which is what makes it safe.
    ack, res = dev.put_config(broken)
    assert_in(b"CONFIG DONE", ack, "PUT CONFIG acknowledged the paste")
    assert_in(b"rejected", res, "device rejected the broken config")
    assert_in(b"nothing saved", res, "device said nothing was saved")
    assert_true(b"accepted" not in res, "a rejected config was not accepted")

    # The half that matters, and the half nobody would notice by
    # looking at the log: the running config must be *untouched*.
    # A device that rejects a paste and then boots the truncated
    # document looks fine until you go to use it.
    _origin2, after = parse_config_reply(dev.command("GET CONFIG"))
    assert_eq(before, after, "rejected config left the live one alone")
    print("  ok    a broken paste changed nothing")
    return dev


def sc_truncated(dev, args):
    _origin, before = parse_config_reply(dev.command("GET CONFIG"))
    # Legally-sized lines, so the *paste* is over the cap rather than
    # one line being junk. A single over-long line would be dropped by
    # the line buffer and never reach the cap check at all.
    filler = b"x" * 50
    over = b"\n".join([filler] * 260)
    ack, res = dev.put_config(over, timeout=REPLY_TIMEOUT + 4)
    assert_in(b"CONFIG DONE", ack, "PUT CONFIG acknowledged the paste")
    assert_true(b"longer than 8 KB" in res or b"not saved" in res,
                "an over-sized paste is refused, got %r" % res[:200])
    _origin2, after = parse_config_reply(dev.command("GET CONFIG"))
    assert_eq(before, after, "over-sized paste left the live config alone")
    print("  ok    an over-sized paste changed nothing")
    return dev


def sc_reset(dev, args):
    # Destructive, hence not in the default set. Asserts the whole
    # point of the change: after a reset the cabinet is in the state a
    # factory-fresh board is in, not a cabinet whose "reset" left the
    # old config in a backup slot for the boot path to find.
    port = dev.port
    _origin, _before = parse_config_reply(dev.command("GET CONFIG"))
    res = dev.command("RESET")
    assert_in(b"ok", res, "RESET acknowledged")
    assert_in(b"erased", res, "RESET reported erasing")
    print("  ok    reset accepted; waiting for the device to come back")
    dev = _reconnect(dev, port)
    origin, body = parse_config_reply(dev.command("GET CONFIG"))
    assert_true("nothing on flash" in origin,
                "after a reset there is nothing on flash, header said %r"
                % origin)
    # And what it serves instead is a real config, not an apology.
    doc = json.loads(body.decode("utf-8"))
    assert_true(len(doc["consoles"]) > 0,
                "the built-in default has consoles in it")
    print("  ok    nothing on flash; serving the built-in default")


SCENARIOS = {
    "help": sc_help,
    "status": sc_status,
    "get": sc_get,
    "roundtrip": sc_roundtrip,
    "reject": sc_reject,
    "truncated": sc_truncated,
    "reset": sc_reset,
}

# Everything except `reset`, which destroys the device's config.
SAFE = ["help", "status", "get", "roundtrip", "reject", "truncated"]


def _reconnect(dev, port, timeout=REBOOT_TIMEOUT):
    """Come back from a restart and return a usable, open session.

    Three things happen in order and all three have to be waited for:
    the port disappears, it comes back as a *new* device node (the old
    handle is dead and pyserial cannot survive it), and then the device
    boots -- which on an unconfigured cabinet is ten seconds of
    blocking setup pages before anything can be sent to it.

    The marker is the last line of setup(), so it arrives after the
    pages and after the radio has been started, which is the first
    moment at which sending is safe. Waiting on a quiet port instead
    would race it.

    Returns a *new* Device. The caller's is closed, and its port handle
    is no longer valid.
    """
    dev.close()
    time.sleep(1.0)  # let the old node go away before looking for a new one
    fresh = open_port(port, timeout=timeout)
    # A healthy device restarts with the heartbeat running; an
    # unconfigured one opens its own session. Both are handled by
    # waiting for whichever marker appears, and both end with a session
    # that is definitely open.
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            out = fresh.read_reply(1.5)
        except (serial.SerialException, OSError):
            # The port bounced again. Reopen and keep waiting.
            fresh.close()
            fresh = open_port(port, timeout=timeout)
            continue
        if b"Setup Complete." in out or b"interactive mode" in out:
            fresh.open_session()
            return fresh
        # Still booting. Nothing to do but let it.
    fresh.close()
    raise Failure("device did not finish booting within %ds" % timeout)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--port", default=None)
    ap.add_argument("--config", default=None)
    ap.add_argument("--scenario", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--all-with-reset", action="store_true")
    args = ap.parse_args()

    if args.list:
        for name in SCENARIOS:
            print(name)
        return 0

    port = args.port
    if not port:
        import glob
        ports = sorted(glob.glob("/dev/cu.usbmodem*") +
                       glob.glob("/dev/tty.usbmodem*"))
        if not ports:
            sys.stderr.write("FATAL: no /dev/cu.usbmodem* found; "
                             "pass --port\n")
            return 2
        port = ports[0]

    config_path = args.config
    if not config_path:
        import os
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        config_path = os.path.join(root, "src", "factory-config.json")
    try:
        raw = open(config_path, "rb").read()
    except OSError as exc:
        sys.stderr.write("FATAL: cannot read %s: %s\n" % (config_path, exc))
        return 1
    # The shipped config is wrapped in a C++ raw-string marker so the
    # preprocessor can include it. Strip it before sending.
    for prefix in (b'R""""(\n', b'R"rr(\n', b'R"('):
        if raw.startswith(prefix):
            raw = raw[len(prefix):]
            break
    for suffix in (b')""""', b')rr"', b')"'):
        if raw.rstrip().endswith(suffix):
            raw = raw.rstrip()[:-len(suffix)]
            break
    args.config_bytes = raw.lstrip(b"\n")

    if args.scenario:
        names = [args.scenario]
    elif args.all_with_reset:
        names = list(SCENARIOS)
    elif args.all:
        names = list(SAFE)
    else:
        names = list(SAFE)

    for name in names:
        if name not in SCENARIOS:
            sys.stderr.write("unknown scenario %r; try --list\n" % name)
            return 1

    try:
        dev = open_port(port)
    except (Failure, serial.SerialException, OSError) as exc:
        sys.stderr.write("FATAL: %s\n" % exc)
        sys.stderr.write("Is another serial monitor holding the port?\n")
        return 2

    # Let the boot finish talking before asserting anything. A device
    # that has just been flashed is still printing -- the radio join
    # alone can take six seconds -- and a line of boot output arriving
    # where a reply was expected is the most confusing possible way for
    # this suite to fail.
    dev.drain(2.0)
    if not dev.open_session():
        print("note: no instruction screen on entry; assuming a healthy "
              "device and continuing")

    failed = 0
    for name in names:
        print("== scenario %s" % name)
        try:
            # A scenario that restarted the device hands back a *new*
            # Device, because a restart re-enumerates the USB and the
            # old handle is dead. Everything after it has to use that
            # one, so the return value is not advisory.
            dev = SCENARIOS[name](dev, args) or dev
            print("SCENARIO:%s:PASS" % name)
        except Failure as exc:
            print("  FAIL  %s" % exc, file=sys.stderr)
            print("SCENARIO:%s:FAIL" % name)
            failed += 1
        except Exception as exc:  # a crash is a failure, not a stack trace
            print("  ERROR %s: %s" % (type(exc).__name__, exc), file=sys.stderr)
            print("SCENARIO:%s:FAIL" % name)
            failed += 1
        try:
            dev.drain(0.5)
        except (serial.SerialException, OSError):
            # The board went away between scenarios. Reopen rather than
            # failing the rest of the suite on a bookkeeping detail.
            try:
                dev = open_port(dev.port)
            except Exception:
                pass

    dev.close()
    return 3 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
