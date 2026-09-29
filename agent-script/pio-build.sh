#!/usr/bin/env bash
# agent-script/pio-build.sh
#
# Compile-only check for the active Pico 2 W firmware. Runs `pio run`
# in the foreground (no flash, no monitor) and surfaces only the
# errors and warnings -- not the full build chatter.
#
# Rationale: AGENT.md §4 says "building is always fine" so this script
# needs no per-invocation permission. It exists so a build-only
# iteration is a one-liner that exits cleanly with the build's real
# exit code, instead of an ad-hoc `pio run -e pico2w 2>&1 | grep ...`
# where grep masking can hide a non-zero exit.
#
# Why not just `pio run -e pico2w`?
#   - `pio run` exits 0 on a clean build, non-zero on errors. That's
#     all you need. This script's value-add is:
#       1. Locking the env to pico2w (per AGENT.md §0/§4: "we are
#          ONLY concerned with running on pico2w from now on").
#       2. Filtering ArduinoJson's StaticJsonDocument deprecation
#          warnings (pre-existing noise per TODO) so a real warning
#          stands out in the output.
#       3. Exiting with the same code `pio run` did -- so callers
#          (CI, scripts, future agents) can use it as a black box.
#
# Usage:
#   ./agent-script/pio-build.sh                  # default env=pico2w
#   ./agent-script/pio-build.sh -e pico_base     # explicit env (rare)
#   ./agent-script/pio-build.sh --verbose        # show full pio output
#
# Exit codes:
#   0  build succeeded
#   1  bad CLI args
#   2  build failed (pio run exit code)
#
# Trust the exit code. Quiet mode used to append `|| true` to the
# filtering pipeline, which reset PIPESTATUS and made every failing
# build report "build OK" -- so check `$?`, not the last line printed.
#
# Note: this does NOT flash. The upload+monitor script
# (agent-script/pio-upload-monitor.sh) is the only correct way to
# flash, and per AGENT.md §4 that needs explicit user permission.

set -u

. "$(dirname "$0")/pio-env.sh"

ENV="pico2w"
VERBOSE=0

print_usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        -e) ENV="$2"; shift 2 ;;
        --env=*) ENV="${1#*=}"; shift ;;
        --verbose|-v) VERBOSE=1; shift ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; print_usage >&2; exit 1 ;;
    esac
done

LOG="$(mktemp -t pio-build.XXXXXX.log)"
trap 'rm -f "$LOG"' EXIT

if [ "$VERBOSE" = 1 ]; then
    # Pass-through mode: full output, full exit code.
    "$PIO" run -e "$ENV" 2>&1 | tee "$LOG"
    rc=${PIPESTATUS[0]}
else
    # Quiet mode: filter the ArduinoJson StaticJsonDocument deprecation
    # noise (pre-existing per TODO), show everything else.
    #
    # PIPESTATUS must be read on the line immediately after the
    # pipeline. Do NOT append `|| true` to it: that runs a second
    # command, which resets PIPESTATUS, so ${PIPESTATUS[0]} would then
    # report `true`'s status and every failing build would be reported
    # as "build OK". `set -e` is not enabled, so a non-zero grep (no
    # lines matched) does not abort the script before the read.
    set -o pipefail
    "$PIO" run -e "$ENV" 2>&1 \
        | tee "$LOG" \
        | grep -v -E "StaticJsonDocument.*deprecated.*JsonDocument"
    rc=${PIPESTATUS[0]}
fi

if [ "$rc" -ne 0 ]; then
    echo "[pio-build] BUILD FAILED for env=$ENV (exit $rc)" >&2
    echo "[pio-build] full log: $LOG" >&2
    # In verbose mode the user already saw everything. In quiet mode,
    # echo the error/warning lines from the log so they don't have to
    # open the file to triage.
    if [ "$VERBOSE" = 0 ]; then
        grep -E '(error:|warning:|Error|FAILED)' "$LOG" | head -40 >&2 || true
    fi
    # Re-disable pipefail so the cleanup trap can rm the log without
    # tripping on its own exit status.
    set +o pipefail
    exit 2
fi

echo "[pio-build] $ENV: build OK"
exit 0
