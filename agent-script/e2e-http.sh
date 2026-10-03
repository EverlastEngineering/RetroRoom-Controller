#!/usr/bin/env bash
# agent-script/e2e-http.sh
#
# Thin wrapper around e2e-http.py, for the same reason its siblings
# exist: the test body should not have to care where python lives.
#
# It resolves the interpreter the way e2e-serial.sh does -- from $PIO,
# which pio-env.sh exports -- but with one difference. e2e-http needs
# nothing from PlatformIO's virtualenv: it is urllib, which is in every
# python, and deliberately NOT requests, so this script would work with
# a system python if one were installed. It still prefers $PIO's, for
# the same reason: reaching for `python3` from PATH works exactly once
# on exactly one machine.
#
# If $PIO is not set, fall back to python3 rather than failing. There is
# no pyserial to be missing here, so unlike the serial sibling there is
# nothing to lose by trying.
#
# Usage:
#   ./agent-script/e2e-http.sh                      # all scenarios
#   ./agent-script/e2e-http.sh --list
#   ./agent-script/e2e-http.sh --scenario routing
#   ./agent-script/e2e-http.sh --base http://192.168.1.42
#
# Everything after the script name goes straight through to the python.
# See e2e-http.py for the full argument list and the exit codes:
# 0 pass, 1 bad args, 2 cabinet unreachable, 3 a scenario failed.

set -u

PY=""
if [ -n "${PIO:-}" ]; then
    CANDIDATE="${PIO%/pio}/python"
    [ -x "$CANDIDATE" ] && PY="$CANDIDATE"
fi
if [ -z "$PY" ]; then
    PY="$(command -v python3 || true)"
fi
if [ -z "$PY" ]; then
    echo "FATAL: no python found (looked beside \$PIO, then on PATH)" >&2
    exit 1
fi

exec "$PY" "$(cd "$(dirname "$0")" && pwd)/e2e-http.py" "$@"
