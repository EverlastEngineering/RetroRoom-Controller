#!/usr/bin/env bash
# agent-script/e2e-serial.sh
#
# Thin wrapper around e2e-serial.py, for the same reason its siblings
# exist: the test body should not have to care where a tool lives.
#
# It resolves an interpreter rather than assuming one, because
# pyserial is a PlatformIO dependency and is therefore already
# installed in PlatformIO's own virtualenv on any machine that can
# build this firmware. Reaching for `python3` from PATH would work
# exactly once -- on the machine where someone happened to have
# pip-installed pyserial -- and fail everywhere else with a message
# about a missing module, which reads as a broken repo.
#
# So: take the interpreter that PlatformIO's resolver already found
# (source pio-env.sh, which exports $PIO), and use *its* python. The
# same file every other wrapper here sources, so there is no new
# place for a machine path to be hardcoded.
#
# Usage:
#   ./agent-script/e2e-serial.sh                     # safe scenarios
#   ./agent-script/e2e-serial.sh --list
#   ./agent-script/e2e-serial.sh --scenario roundtrip
#   ./agent-script/e2e-serial.sh --all-with-reset    # destroys the config
#
# Everything after the script name is passed straight through to the
# python. See e2e-serial.py for the full argument list.
#
# Exit codes are the python's: 0 pass, 1 bad args, 2 port unavailable,
# 3 a scenario failed.

set -u

. "$(cd "$(dirname "$0")" && pwd)/pio-env.sh"

# $PIO is .../penv/bin/pio, so its sibling python is the one that has
# pyserial. If the resolver put PIO somewhere else entirely, fall
# back to the python next to it rather than to PATH.
PY="${PIO%/pio}/python"
if [ ! -x "$PY" ]; then
    echo "FATAL: no python beside $PIO (looked for $PY)" >&2
    echo "       PlatformIO installs pyserial in its own virtualenv, so" >&2
    echo "       this script will not fall back to a PATH python." >&2
    exit 1
fi

exec "$PY" "$(cd "$(dirname "$0")" && pwd)/e2e-serial.py" "$@"
