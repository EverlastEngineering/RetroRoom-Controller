#!/usr/bin/env bash
# agent-script/pio-upload-monitor.sh
#
# Build + flash + monitor a PlatformIO environment, then auto-close the
# monitor after a configurable timeout. Solves two problems that bite
# every fresh session on this repo:
#
#   1. `pio device monitor` requires a real TTY on stdin -- a plain
#      `&` background job in a non-TTY shell gets suspended by the OS
#      and produces no output. `script(1)` wraps the pipeline in a
#      pseudo-tty which is enough to satisfy miniterm.
#   2. The monitor never exits on its own; you have to Ctrl+C it from
#      a real terminal. This script sends SIGINT (then SIGKILL as a
#      fallback) on a timer so the pipeline is fully self-contained
#      and can run from any shell.
#
# Flow:
#   1. (Optional) `pio run -e $ENV` to a fresh build cache so a
#      build error doesn't waste upload + monitor time.
#   2. `pio run -e $ENV -t upload -t monitor` under `script(1)` so the
#      monitor sees a pty as stdin.
#   3. After `$MONITOR_SECS` seconds, SIGINT the wrapper, wait 2 s,
#      SIGKILL if still alive.
#   4. Filter heartbeats out of the captured log and print the rest.
#
# Usage:
#   ./agent-script/pio-upload-monitor.sh                       # default env=pico2w, 25s
#   ./agent-script/pio-upload-monitor.sh -e pico_base           # different env
#   ./agent-script/pio-upload-monitor.sh -e pico2w -t 60       # 60s monitor window
#   ./agent-script/pio-upload-monitor.sh --no-build            # skip the standalone build
#   ./agent-script/pio-upload-monitor.sh --keep-heartbeats     # don't strip Heartbeat: lines
#
# Defaults: ENV=pico2w, MONITOR_SECS=25, LOG=/tmp/pio_upload_monitor.log
#
# Exit codes:
#   0  pipeline finished cleanly (build green, upload OK, monitor window elapsed)
#   1  bad CLI args
#   2  build failed (caught before the upload+monitor stage)
#   3  upload failed (build OK but picotool didn't reach 100%)
#   4  couldn't open the serial port (likely another monitor session is holding it)

set -u

ENV="pico2w"
MONITOR_SECS=35
LOG="/tmp/pio_upload_monitor.log"
DO_BUILD=1
FILTER_HEARTBEATS=1

print_usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        -e) ENV="$2"; shift 2 ;;
        --env=*) ENV="${1#*=}"; shift ;;
        -t) MONITOR_SECS="$2"; shift 2 ;;
        --timeout=*) MONITOR_SECS="${1#*=}"; shift ;;
        --no-build) DO_BUILD=0; shift ;;
        --keep-heartbeats) FILTER_HEARTBEATS=0; shift ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

# Truncate any stale log so the post-run grep is unambiguous.
: > "$LOG"

# 1. Optional standalone build. We do this *before* the upload+monitor so
#    that a compile error doesn't waste a flash cycle or a 25 s monitor
#    window waiting for output that will never come. If you only changed
#    one file and want to skip this, pass --no-build.
if [ "$DO_BUILD" = 1 ]; then
    echo "[pio-upload-monitor] pio run -e $ENV (full build)..." >&2
    if ! pio run -e "$ENV" 2>&1 | tee -a "$LOG" | tail -3 ; then
        echo "[pio-upload-monitor] BUILD FAILED for env=$ENV; see $LOG" >&2
        exit 2
    fi
    echo "[pio-upload-monitor] build OK" >&2
fi

# 2. Upload + monitor under script(1) so pio monitor sees a pty.
#    `script -q` runs the inner command attached to a pseudo-tty and
#    tees the session transcript to $LOG. `-q` suppresses the
#    "Script started on..." / "Script done on..." banners.
echo "[pio-upload-monitor] pio run -e $ENV -t upload -t monitor (capped at ${MONITOR_SECS}s)..." >&2
script -q "$LOG" pio run -e "$ENV" -t upload -t monitor &
PIO_PID=$!

# 3. Wait, then SIGINT (miniterm's "Ctrl+C" handler exits cleanly),
#    then SIGKILL after a short grace if anything is still hanging on
#    to /dev/cu.usbmodem*.
sleep "$MONITOR_SECS"
kill -INT "$PIO_PID" 2>/dev/null || true
sleep 2
kill -9  "$PIO_PID" 2>/dev/null || true
wait "$PIO_PID" 2>/dev/null || true

# 4. Print the meaningful part of the log (heartbeats stripped by default).
echo
echo "================================================================"
echo "[pio-upload-monitor] non-heartbeat log lines from $LOG:"
echo "================================================================"
if [ "$FILTER_HEARTBEATS" = 1 ]; then
    grep -v "^Heartbeat:" "$LOG" || true
else
    cat "$LOG"
fi

# 5. Status summary based on what we saw in the log. We treat the run
#    as successful if the verify reached 100% AND the firmware booted
#    far enough to print the `net:` setup portal lines (or any
#    post-Setup-Complete heartbeat). The PlatformIO `[SUCCESS]` line
#    often gets truncated by the SIGINT, so don't rely on it alone.
if grep -q "Verifying Flash:.*100%" "$LOG" ; then
    if grep -q "^Setup Complete\." "$LOG" || grep -q "^net: setup portal running" "$LOG" ; then
        echo
        echo "[pio-upload-monitor] upload OK, firmware booted; monitor window elapsed."
        exit 0
    else
        echo
        echo "[pio-upload-monitor] upload OK but post-boot logs missing; check $LOG"
        exit 0
    fi
elif grep -q "Resource busy" "$LOG" || grep -q "could not open" "$LOG" ; then
    echo
    echo "[pio-upload-monitor] serial port busy or unavailable; check $LOG"
    exit 4
elif grep -q "FAILED" "$LOG" ; then
    echo
    echo "[pio-upload-monitor] build or upload FAILED; check $LOG"
    exit 3
else
    echo
    echo "[pio-upload-monitor] monitor window ended without a clear success line; check $LOG"
    exit 1
fi
