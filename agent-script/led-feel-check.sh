#!/usr/bin/env bash
# agent-script/led-feel-check.sh
#
# Compare the `led` block of every checked-in config against
# defaultLedFeel(), which is what led-feel-dump.sh prints.
#
# The request behind this was "I keep forgetting how to dump the JSON
# and I want to see the diff when a task finishes". So this is the
# ritual as a command: run it before committing anything that touched
# LedFeel, a default, or a config file, and it tells you which file is
# now disagreeing with the code and about which field.
#
# It exists because the alternative is three copies of the same thirty
# numbers -- defaultLedFeel(), src/factory-config.json, and the example
# configs -- and copies drift silently. They had: ringOffDelayMs,
# ringFadeMs and brightnessPct were missing from *both* config files,
# in files whose whole claim is that they are complete, and nothing
# noticed until a test asked.
#
# Two kinds of finding, treated differently:
#
#   MISSING / EXTRA   a field that is in the code and not in the file,
#                     or vice versa. These are errors. A config that
#                     omits a field is not wrong at runtime -- the
#                     default is used -- which is exactly why it is
#                     invisible.
#
#   DIFFERS           the file states a value that is not the default.
#                     Reported, not failed: led.totalLeds is 118 in the
#                     configs because that is how long the fitted
#                     string is, while the default is 512 because that
#                     is the build capacity. That difference is
#                     deliberate and documented, and an exceptions list
#                     is a list that grows, so a difference is
#                     something a human reads rather than something a
#                     script fails on.
#
# Usage:
#   ./agent-script/led-feel-check.sh            # check every config
#   ./agent-script/led-feel-check.sh --verbose  # also print every value
#
# Exit codes:
#   0  no config is missing a field, and none states one the code does
#      not have
#   1  a config is missing a field, or states one the code does not
#      know about
#   2  led-feel-dump.sh failed, so nothing could be compared

set -u

cd "$(cd "$(dirname "$0")" && pwd)/.." || exit 2

VERBOSE=0
[ "${1:-}" = "--verbose" ] && VERBOSE=1

# The configs that carry a `led` block. src/factory-config.json is
# first and is wrapped in a C++ raw-string marker, so it is unwrapped
# here; the rest are plain JSON.
CONFIGS=(
  "src/factory-config.json:wrapped"
  "example-configurations/example6-all-options.json:plain"
  "example-configurations/example1.json:plain"
  "example-configurations/example2.json:plain"
  "example-configurations/example3-two-rows.json:plain"
  "example-configurations/example4-one-shelf.json:plain"
  "example-configurations/example5-single-shelf.json:plain"
)

DUMP="$(mktemp -t ledfeel.XXXXXX)"
trap 'rm -f "$DUMP"' EXIT

if ! ./agent-script/led-feel-dump.sh -o "$DUMP" >/dev/null 2>&1; then
  echo "FATAL: led-feel-dump.sh failed; cannot compare" >&2
  exit 2
fi

python3 - "$DUMP" "$VERBOSE" "${CONFIGS[@]}" <<'PYEOF'
import json, sys

dump_path, verbose = sys.argv[1], sys.argv[2] == "1"
specs = sys.argv[3:]

# led-feel-dump.sh writes a *fragment*: a bare `"led": { ... }` key with
# no enclosing braces, so it can be pasted into a config by hand. It is
# not a document and json.load() will not read it -- which is exactly
# why the dump is convenient to copy and awkward to consume. Wrapping it
# is the whole adaptation.
with open(dump_path) as f:
    code = json.loads("{" + f.read().rstrip().rstrip(",") + "}").get("led", {})

# Configs that claim to state every option. Anything else may
# deliberately say less and take the defaults -- that is the whole
# point of a config format where a block is optional -- so a missing
# field there is not a finding, it is the design.
#
# Only these two make the claim: the one a fresh cabinet runs, and the
# one whose name is "all options". Listing them is a statement of
# intent, and it is the reason the output is short enough to read.
STRICT = {
    "src/factory-config.json",
    "example-configurations/example6-all-options.json",
}

failures = 0

def unwrap(raw, kind):
    if kind == "plain":
        return json.loads(raw)
    # R""""( ... )"""" -- a C++ raw string, so the payload between the
    # markers is the JSON and nothing else.
    body = raw[len('R""""(\n'):] if raw.startswith('R""""(\n') else raw
    return json.loads(body[:body.rfind(')')])

for spec in specs:
    path, _, kind = spec.rpartition(":")
    strict = path in STRICT
    try:
        with open(path) as f:
            cfg = unwrap(f.read(), kind)
    except Exception as exc:
        print("SKIP  %s (%s)" % (path, exc))
        continue

    led = cfg.get("led")
    if led is None:
        # A config that says nothing about the strip is a legitimate
        # thing to be. Not a finding.
        print("ok    %s (no led block; every default)" % path)
        continue

    missing = sorted(k for k in code if k not in led)
    extra = sorted(k for k in led if k not in code)
    differs = sorted(k for k in led if k in code and led[k] != code[k])

    # A key the code does not know is always wrong: the parser ignores
    # it, so a typo is a setting the operator believes they set.
    if extra:
        failures += 1
        print("FAIL  %s" % path)
        for k in extra:
            print("        UNKNOWN   %s = %s -- the code has no such field, "
                  "so it is silently ignored" % (k, led[k]))
    elif strict and missing:
        failures += 1
        print("FAIL  %s (claims to be complete)" % path)
        for k in missing:
            print("        MISSING   %s (the code says %s)" % (k, code[k]))
    else:
        print("ok    %s (%d field%s%s)" % (path, len(led),
                                          "" if len(led) == 1 else "s",
                                          ", partial" if missing else ""))

    # A differing value is reported and never failed. led.totalLeds is
    # 118 in the configs because that is how long the fitted string is,
    # while the default is 512 because that is the build capacity. That
    # difference is deliberate, and an exceptions list is a list that
    # grows.
    for k in differs:
        print("        DIFFERS   %s: file %s, code %s" % (k, led[k], code[k]))
    if differs:
        print("                 (reported, not failed -- totalLeds is meant to differ)")

    if verbose:
        for k in sorted(led):
            mark = "" if k in code else "   <-- not in the code"
            print("        %-22s %s%s" % (k, led[k], mark))

print()
print("led fields in the code: %d" % len(code))
print("configs checked for completeness: %s" % ", ".join(sorted(STRICT)))
print("configs with a problem: %d" % failures)
sys.exit(1 if failures else 0)
PYEOF
exit $?
