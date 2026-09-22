#!/usr/bin/env bash
# agent-script/serial-snapshot.sh
#
# Capture a bounded snapshot of bytes coming out of the Pico 2 W's
# USB-CDC serial port. Sidesteps every wart of the long-running
# `pio device monitor` approach:
#
#   - No `script(1)` pseudo-tty dance -- straight `cat /dev/cu.usbmodem*`.
#   - No SIGINT/SIGKILL teardown -- gtimeout(1) sends SIGTERM itself
#     and exits cleanly, so the TTY is released without a half-closed
#     pty hanging around.
#   - No buffer flush issues -- `head -n N` exits as soon as N lines
#     arrive, terminating the read and closing the fd cleanly.
#   - Bounded output -- you get exactly the bytes you asked for.
#
# Useful for:
#   - Confirming the firmware is alive: run with `-n 5 -t 8` and look
#     for `Heartbeat:` lines.
#   - Capturing the response to a single-shot command: in one terminal
#     run `serial-snapshot.sh -n 30 -t 20`; in another hit
#     `curl ... /ledOn` or `curl ... /next`; watch the snapshot print
#     the device's reaction.
#   - Sanity-checking boot: `-n 30 -t 15` after a fresh power-cycle
#     should give you scan results, WiFi join, `Setup Complete.`,
#     and ~5 heartbeats.
#
# Usage:
#   ./agent-script/serial-snapshot.sh                       # 10 lines, 5 s
#   ./agent-script/serial-snapshot.sh -n 30 -t 20           # 30 lines or 20 s
#   ./agent-script/serial-snapshot.sh -p /dev/cu.usbmodem42  # override port
#   ./agent-script/serial-snapshot.sh -o /tmp/snap.log      # tee to a file too
#
# Exit codes:
#   0  captured at least one line before the timeout/line-cap fired
#   1  bad CLI args
#   2  serial port busy or unavailable (lsof says something else owns it)
#   3  no bytes captured (timeout fired with empty output -- device may be wedged)
#
# Dependencies:
#   gtimeout  -- GNU timeout(1) from coreutils. On macOS:
#                `brew install coreutils` provides `gtimeout`. This
#                script does NOT have a portable fallback -- if
#                gtimeout isn't on PATH the script exits with a clear
#                message. The whole point of using gtimeout is that
#                the bash-builtin alternatives are subtly wrong
#                (don't forward SIGTERM correctly to nested
#                pipelines); we'd rather fail loudly than ship a
#                known-broken fallback.

set -u

PORT=""
LINES=10
SECS=5
OUT=""

print_usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        -p|--port) PORT="$2"; shift 2 ;;
        -n|--lines) LINES="$2"; shift 2 ;;
        -t|--timeout) SECS="$2"; shift 2 ;;
        -o|--output) OUT="$2"; shift 2 ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; print_usage >&2; exit 1 ;;
    esac
done

if ! command -v gtimeout >/dev/null 2>&1; then
    echo "FATAL: gtimeout not found. Install with: brew install coreutils" >&2
    exit 1
fi

# Auto-detect port if not given. Prefer cu.usbmodem* (callout unit,
# DCD not asserted -- correct for talking to a Pico in firmware mode).
if [ -z "$PORT" ]; then
    PORT="$(ls -1 /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    if [ -z "$PORT" ]; then
        echo "FATAL: no /dev/cu.usbmodem* device found; pass --port" >&2
        exit 1
    fi
fi

if [ ! -c "$PORT" ]; then
    echo "FATAL: $PORT is not a character device" >&2
    exit 1
fi

# Precheck: refuse to run if something else is holding the port.
# Without this we'd silently capture zero bytes and exit 3, which
# looks identical to a wedged device.
if lsof "$PORT" >/dev/null 2>&1 ; then
    echo "FATAL: $PORT is busy:" >&2
    lsof "$PORT" >&2 || true
    echo "       (kill the holder, e.g. \`pkill -f 'pio device monitor'\`)" >&2
    exit 2
fi

# Validate -o early so we don't capture and then lose the bytes.
if [ -n "$OUT" ] && [ ! -w "$(dirname -- "$OUT")" ]; then
    echo "FATAL: cannot write to $OUT (parent dir not writable)" >&2
    exit 1
fi
if [ -n "$OUT" ] && [ -e "$OUT" ] && [ ! -w "$OUT" ]; then
    echo "FATAL: cannot write to $OUT (file not writable)" >&2
    exit 1
fi

# The pipeline:
#   gtimeout $SECS bash -c "cat '$PORT'" 2>&1 | head -n $LINES > "$TMP"
#     - gtimeout caps total wall time at $SECS; sends SIGTERM, waits
#       for clean exit, then SIGKILL.
#     - cat reads from the serial port until EOF or signal.
#     - head -n $LINES exits as soon as N lines have been read; its
#       exit closes the upstream pipe, which makes cat exit on EOF.
#       This is what makes the read "bounded" -- no half-open fd.
#
# We stage the captured bytes into $TMP first, then either print
# them or tee them to stdout + $OUT. This avoids the awkward
# `tee >(...)` pattern and keeps the count step trivial.
TMP="$(mktemp -t serial-snapshot.XXXXXX)"
trap 'rm -f "$TMP"' EXIT

gtimeout --preserve-status "$SECS" bash -c "cat '$PORT'" 2>&1 \
    | head -n "$LINES" > "$TMP"

CAPTURED_LINES=$(wc -l < "$TMP" | tr -d ' ')

# Tee to stdout (operator sees the captured bytes) and optionally
# to $OUT (file accumulates across re-runs).
if [ -n "$OUT" ]; then
    cat "$TMP" | tee -a "$OUT" >/dev/null
else
    cat "$TMP"
fi

# Distinguish "captured 0 lines" (gtimeout fired with no bytes --
# device may be wedged) from "captured some lines" (gtimeout fired
# mid-line-cap OR the line cap fired first -- both fine).
if [ "${CAPTURED_LINES:-0}" -eq 0 ]; then
    echo "[serial-snapshot] no bytes captured in ${SECS}s -- device may be wedged (try power-cycle)" >&2
    exit 3
fi
exit 0
