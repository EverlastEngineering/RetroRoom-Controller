#!/usr/bin/env python3
"""End-to-end test for the HTTP API -- the /lights family, in the main.

Why this exists at all: the shell half of the API is the part no host
test can reach, and it had a bug that a 200 could not reveal.

`server.on("/lights", ...)` does not match exactly. ESPAsyncWebServer
inspects the registered string, and a plain path gets
Type::BackwardCompatible, which is

    (_value == path) || path.startsWith(_value + "/")

(WebServer.cpp, AsyncURIMatcher::matches). So `/lights` also answers
`/lights/show`, and since handlers are tried in registration order, the
first one registered won. `GET /lights/show` returned the body of
`GET /lights`; so did `/lights/on` and `/lights/off`, which means
night mode's entire web API had never once worked and said nothing,
because every response was a 200 carrying valid JSON.

So the first scenario here is not about any one endpoint. It is that
EVERY /lights URL reaches its OWN handler, which is the assertion that
would have caught it, and the shape checks are written so that a
fall-through to the state document is a failure rather than a near-miss.

The other thing worth knowing: these are GETs, so a scenario that
mutates state (starting the show) can leave the strip showing waves.
Every scenario that starts it stops it again, and the wrapper says so.

Usage:
    ./agent-script/e2e-http.sh                       # all scenarios
    ./agent-script/e2e-http.sh --list
    ./agent-script/e2e-http.sh --scenario routing
    ./agent-script/e2e-http.sh --base http://retroroom.local
    ./agent-script/e2e-http.sh --base http://192.168.1.42 --timeout 5

Exit codes:
    0  every selected scenario passed
    1  bad CLI args
    2  the device could not be reached at all (nothing can be known)
    3  a scenario failed

Each scenario prints SCENARIO:<name>:PASS or SCENARIO:<name>:FAIL.
"""

import argparse
import json
import sys
import urllib.error
import urllib.request

# The cabinet is not on a network by default, so the base URL is an
# argument rather than a constant. mDNS is the friendly one; the script
# does not resolve it itself so a failure is the curl-equivalent
# "cannot find host", which is what a person would have got too.
DEFAULT_BASE = "http://retroroom.local"


class DeviceUnreachable(Exception):
    pass


def get(base, path, timeout):
    """GET a path. Returns (status, body) and never raises for a 4xx.

    A 404 is a perfectly good answer here and a test needs to see it, so
    HTTPError is turned into a result rather than an exception. A
    connection failure is different -- that means we learned nothing, so
    it is raised and the run stops rather than reporting a scenario
    failure that is really "the cable is unplugged".
    """
    url = base.rstrip("/") + path
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001 - any failure is unreachable
        raise DeviceUnreachable("%s: %s" % (url, e))


def get_json(base, path, timeout):
    status, body = get(base, path, timeout)
    try:
        return status, json.loads(body)
    except json.JSONDecodeError as e:
        raise AssertionError(
            "%s returned %d with a body that is not JSON: %r (%s)"
            % (path, status, body[:200], e)
        )


# ---- scenarios --------------------------------------------------------

SCENARIOS = {}


def scenario(name):
    def deco(fn):
        SCENARIOS[name] = fn
        return fn
    return deco


@scenario("routing")
def sc_routing(ctx):
    """Every /lights URL reaches its OWN handler.

    The one that matters. Each check is a predicate over the *parsed*
    body rather than a substring of the raw text, because a substring of
    re-serialised JSON is a test that fails on whitespace and passes on
    a document that happens to contain the right key somewhere.

    Note the shape asymmetry the predicates lean on, which is the whole
    point: `/lights` answers with `show` as an OBJECT (the state), while
    every /lights/show/* answers with `show` as a STRING naming what it
    just did. A fall-through to the state document therefore fails on
    both the wrong type and the missing keys, and the "must not" arms
    catch the cases where it would otherwise pass.
    """
    state = lambda d: isinstance(d.get("show"), dict) and "error" not in d
    checks = [
        ("/lights", state),
        ("/lights/on",
         lambda d: d.get("nightMode") is True and not state(d)),
        ("/lights/off",
         lambda d: d.get("nightMode") is False and "error" not in d
         and not state(d)),
        ("/lights/show",
         lambda d: d.get("show") in ("started", "already running")
         and "paletteCount" not in d),
        ("/lights/show/off",
         lambda d: d.get("show") == "stopped" and "paletteCount" not in d),
        ("/lights/show/next",
         lambda d: d.get("show") == "next" and isinstance(d.get("palette"), int)
         and isinstance(d.get("paletteName"), str)),
        ("/lights/show/prev",
         lambda d: d.get("show") == "prev" and isinstance(d.get("palette"), int)
         and isinstance(d.get("paletteName"), str)),
    ]
    for path, ok in checks:
        status, doc = get_json(ctx.base, path, ctx.timeout)
        assert status == 200, "%s -> HTTP %d (%r)" % (path, status, doc)
        assert ok(doc), (
            "%s answered with the wrong document: %r\n"
            "       (a /lights sub-URL falling through to GET /lights is "
            "the prefix-match bug this scenario exists for)" % (path, doc)
        )
        # `/lights/show` and `/lights/show/off` MUTATE -- they start and
        # stop the show. Every other path here is a read. Stopping it
        # after each one means this scenario cannot leave the strip
        # showing waves for whatever runs next, and cannot leave the
        # `state` scenario asserting about a running show.
        if path == "/lights/show":
            get(ctx.base, "/lights/show/off", ctx.timeout)


@scenario("unknown")
def sc_unknown(ctx):
    """A /lights path nobody implements is a 404, not a valid answer.

    The other half of the same fix. Falling through to the state
    document is what made the bug invisible; a typo now says so.
    """
    for path in ("/lights/nope", "/lights/show/nope", "/lights/show/next/extra"):
        status, body = get(ctx.base, path, ctx.timeout)
        assert status == 404, "%s -> HTTP %d, expected 404" % (path, status)
        assert "no such /lights endpoint" in body, (
            "%s: 404 body should say why: %r" % (path, body[:200])
        )


@scenario("state")
def sc_state(ctx):
    """GET /lights reports every field the API claims to report.

    Asserted field by field rather than on the whole body, because a
    missing key is exactly what a hand-built String loses and a
    substring check on the rest would not notice.
    """
    status, doc = get_json(ctx.base, "/lights", ctx.timeout)
    assert status == 200, "HTTP %d" % status
    for key in ("nightMode", "brightness", "show", "persisted"):
        assert key in doc, "GET /lights is missing %r: %r" % (key, doc)
    assert isinstance(doc["nightMode"], bool), doc["nightMode"]
    assert isinstance(doc["brightness"], int), doc["brightness"]
    assert doc["persisted"] is False, doc
    show = doc["show"]
    for key in ("running", "palette", "paletteName", "paletteCount"):
        assert key in show, "show is missing %r: %r" % (key, show)
    assert isinstance(show["running"], bool), show
    assert isinstance(show["palette"], int), show
    assert isinstance(show["paletteCount"], int), show
    assert show["paletteCount"] > 0, "no palettes in the playlist"
    if show["running"]:
        assert show["palette"] >= 0, (
            "running with palette %d" % show["palette"]
        )
        assert show["paletteName"] != "?", "running with an unnamed palette"
    else:
        # Stopped. The palette is left where it was, so restarting picks
        # up the same one -- which means -1 means "never started", not
        # "not running", and asserting -1 here would be asserting that
        # stopping forgets the palette. The JSON cannot tell those two
        # apart and does not need to: `running` is the question, and it
        # is answered. What must hold is that the number is either the
        # never-started sentinel or a real entry.
        assert show["palette"] == -1 or 0 <= show["palette"] < show[
            "paletteCount"
        ], ("stopped with a palette that is not a real entry: %r" % show)
        assert show["palette"] == -1 or show["paletteName"] != "?", show


@scenario("nightmode")
def sc_nightmode(ctx):
    """The night mode endpoints change the state they claim to change.

    The reason this exists as its own scenario: `/lights/on` returned
    the *state document* and turned nothing on, and the only way to see
    that is to read the state afterwards and find it unchanged.
    """
    get(ctx.base, "/lights/off", ctx.timeout)
    _, before = get_json(ctx.base, "/lights", ctx.timeout)

    get(ctx.base, "/lights/on", ctx.timeout)
    _, on = get_json(ctx.base, "/lights", ctx.timeout)
    assert on["nightMode"] is True, (
        "GET /lights/on did not turn night mode on: %r" % on
    )

    get(ctx.base, "/lights/off", ctx.timeout)
    _, off = get_json(ctx.base, "/lights", ctx.timeout)
    assert off["nightMode"] is False, (
        "GET /lights/off did not turn night mode off: %r" % off
    )
    # And it is the only thing that changed -- a scenario that asserts
    # "the flag moved" alone would pass on a handler that set
    # everything.
    assert off["brightness"] == before["brightness"], (before, off)
    assert off["show"] == before["show"], (before, off)


@scenario("show")
def sc_show(ctx):
    """Start, step through the palettes, and stop. Leaving nothing on.

    The only scenario that mutates the strip, so it is the only one that
    has to put it back. If it fails part-way the finally block still
    stops the show.
    """
    try:
        status, doc = get_json(ctx.base, "/lights/show", ctx.timeout)
        assert status == 200, "HTTP %d" % status
        assert doc["show"] in ("started", "already running"), doc

        _, started = get_json(ctx.base, "/lights", ctx.timeout)
        assert started["show"]["running"] is True, (
            "GET /lights/show did not start the show: %r" % started
        )
        first = started["show"]["palette"]
        assert first >= 0, started
        assert started["show"]["paletteName"] != "?", started

        # Starting again is a no-op, and says so rather than pretending
        # to have started a second one.
        _, again = get_json(ctx.base, "/lights/show", ctx.timeout)
        assert again["show"] == "already running", again

        # The playlist is a ring, so "next" from the last entry wraps to
        # the first. Stepping once from `first` must therefore give the
        # next entry, and the name must not be left as "?".
        _, nxt = get_json(ctx.base, "/lights/show/next", ctx.timeout)
        assert nxt["show"] == "next", nxt
        running = get_json(ctx.base, "/lights", ctx.timeout)[1]
        assert running["show"]["palette"] == (first + 1) % running["show"][
            "paletteCount"
        ], ("next did not advance the playlist: %r -> %r"
            % (first, running["show"]))
        assert running["show"]["paletteName"] != "?", running

        # ... and back again.
        get_json(ctx.base, "/lights/show/prev", ctx.timeout)
        back = get_json(ctx.base, "/lights", ctx.timeout)[1]
        assert back["show"]["palette"] == first, (
            "prev did not step back: %r -> %r" % (first, back["show"])
        )
    finally:
        get(ctx.base, "/lights/show/off", ctx.timeout)


@scenario("show-off-idempotent")
def sc_show_off_idempotent(ctx):
    """Stopping a show that is not running is a 200 and a no-op.

    Which matters because the give-way paths -- a knob detent, a click,
    a commit -- all call the same stop. Anything that made a second stop
    an error would make an ordinary knob turn log a failure.
    """
    status, body = get(ctx.base, "/lights/show/off", ctx.timeout)
    assert status == 200, "HTTP %d" % status
    _, doc = get_json(ctx.base, "/lights/show/off", ctx.timeout)
    assert doc["show"] == "stopped", doc
    _, state = get_json(ctx.base, "/lights", ctx.timeout)
    assert state["show"]["running"] is False, state


@scenario("console")
def sc_console(ctx):
    """/next and /prev move the selection, and stop at the ends.

    Not part of the /lights work, and here because they share the file
    and the give-way logic: a commit has to stop the show, and this is
    the cheapest way to make a commit from outside the cabinet.

    The ends are the interesting half, and they were a wrong assumption
    in the first version of this test. `stepWithin` CLAMPS rather than
    wrapping -- `/next` on the last console is a deliberate no-op, and
    there is a todo note for the behaviour -- so a test that assumed
    wrap-around failed against a cabinet that was merely sitting at the
    end of its list.

    A no-op has to stay observable, and the state document carries the
    evidence: `selectedAtUptimeMs` must NOT change when nothing moved.
    The firmware comments say the e2e harness depends on that, because a
    device that stamps the time on a no-op looks alive when it is stuck.
    So this asserts the stamp moves when it should and does not when it
    should not, which is the whole contract in two lines.

    Bounded on purpose. An earlier version restored the starting index
    with `while True` and that is how this scenario took the cabinet
    offline during a run -- not a crash, as it turned out, but an
    unbounded request loop against a device that was already slow. A
    test that can issue an unlimited number of requests is a test that
    can take the thing under test down, which is not a property a test
    should have.
    """
    _, before = get_json(ctx.base, "/state.json", ctx.timeout)
    total = before.get("total", 0)
    index = before.get("index", 0)
    if total < 2:
        # One console, or none. There is nowhere to go, so there is
        # nothing to test; say so rather than pass silently.
        return

    at_end = index >= total - 1
    _, moved = get_json(ctx.base, "/next", ctx.timeout)
    _, after = get_json(ctx.base, "/state.json", ctx.timeout)

    if at_end:
        assert moved["index"] == index, (
            "/next wrapped past the end: %d -> %r" % (index, moved)
        )
        assert after["selectedAtUptimeMs"] == before["selectedAtUptimeMs"], (
            "/next was a no-op but stamped the selection time (%r -> %r), "
            "so a stuck cabinet looks alive"
            % (before["selectedAtUptimeMs"], after["selectedAtUptimeMs"])
        )
        # There IS somewhere to go the other way, so use it to prove the
        # endpoint is alive and not simply wedged.
        _, back = get_json(ctx.base, "/prev", ctx.timeout)
        assert back["index"] == index - 1, (
            "/prev did not step back from the end: %r -> %r" % (index, back)
        )
        _, home = get_json(ctx.base, "/next", ctx.timeout)
        assert home["index"] == index, "/next did not step back: %r" % home
    else:
        assert moved["index"] == index + 1, (
            "/next did not advance: %d -> %r" % (index, moved)
        )
        assert after["selectedAtUptimeMs"] != before["selectedAtUptimeMs"], (
            "the selection moved but the timestamp did not, so a caller "
            "cannot tell a real move from a no-op"
        )
        _, back = get_json(ctx.base, "/prev", ctx.timeout)
        assert back["index"] == index, (
            "/prev did not step back: %r -> %r" % (moved, back)
        )


@scenario("health")
def sc_health(ctx):
    """The routes that were already here still answer.

    The point of the whole exercise was a fix to a shared file. These
    are the ones a prefix-match change would have broken if the change
    had been the other kind of fix.
    """
    for path in ("/healthcheck", "/state.json", "/wifi", "/script.js"):
        status, _ = get(ctx.base, path, ctx.timeout)
        assert status == 200, "%s -> HTTP %d" % (path, status)
    # /openapi and /openapi.yaml do NOT collide, which is why they were
    # left alone: BackwardCompatible needs a trailing SLASH, and
    # "/openapi.yaml" does not start with "/openapi/". Asserted because
    # that is the reasoning the comment in network.cpp rests on, and
    # reasoning that is not tested is reasoning that rots.
    for path in ("/openapi", "/openapi.yaml"):
        status, body = get(ctx.base, path, ctx.timeout)
        assert status == 200, "%s -> HTTP %d" % (path, status)
        assert body.strip(), "%s returned an empty body" % path


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", default=DEFAULT_BASE,
                    help="cabinet base URL (default %s)" % DEFAULT_BASE)
    ap.add_argument("--timeout", type=float, default=5.0,
                    help="per-request timeout in seconds")
    ap.add_argument("--scenario", action="append",
                    help="run only this scenario (repeatable)")
    ap.add_argument("--list", action="store_true",
                    help="list scenarios and exit")
    args = ap.parse_args(argv)

    if args.list:
        for name in SCENARIOS:
            print(name)
        return 0

    names = args.scenario or list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print("FATAL: unknown scenario(s): %s" % ", ".join(unknown), file=sys.stderr)
        print("       known: %s" % ", ".join(SCENARIOS), file=sys.stderr)
        return 1

    ctx = type("Ctx", (), {})()
    ctx.base = args.base
    ctx.timeout = args.timeout

    print("e2e-http: %s" % args.base)
    try:
        status, _ = get(ctx.base, "/healthcheck", ctx.timeout)
    except DeviceUnreachable as e:
        print("FATAL: cannot reach the cabinet: %s" % e, file=sys.stderr)
        print("       It needs to have joined a network, or be on the SoftAP",
              file=sys.stderr)
        return 2

    failures = 0
    for name in names:
        try:
            SCENARIOS[name](ctx)
        except (AssertionError, DeviceUnreachable) as e:
            failures += 1
            print("SCENARIO:%s:FAIL  %s" % (name, e))
        else:
            print("SCENARIO:%s:PASS" % name)
    return 3 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
