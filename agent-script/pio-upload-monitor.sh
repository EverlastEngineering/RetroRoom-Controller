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
#   ./agent-script/pio-upload-monitor.sh --no-upload           # build only, do NOT flash / monitor
#   ./agent-script/pio-upload-monitor.sh --monitor-only        # attach to serial only; no build, no flash
#   ./agent-script/pio-upload-monitor.sh --keep-heartbeats     # don't strip Heartbeat: lines
#
# Defaults: ENV=pico2w, MONITOR_SECS=25, LOG=/tmp/pio_upload_monitor.log, DO_BUILD=1, DO_UPLOAD=1
#
# NOTE on --no-build vs --no-upload:
#   --no-build   skips the standalone `pio run -e $ENV` pass; upload+monitor still run.
#   --no-upload  skips the `pio run -e $ENV -t upload -t monitor` pass entirely; only
#                `pio run -e $ENV` runs. Use this for a compile-only check (per AGENT.md
#                §4 "building is always fine") without flashing the device (which needs
#                explicit user permission). DO NOT use --no-build as a build-only check --
#                it will still flash the device.
#
# Exit codes:
#   0  pipeline finished cleanly (build green, upload OK if requested, monitor window elapsed)
#   1  bad CLI args
#   2  build failed (caught before the upload+monitor stage)
#   3  upload failed (build OK but picotool didn't reach 100%)
#   4  couldn't open the serial port (likely another monitor session is holding it)

set -u

ENV="pico2w"
MONITOR_SECS=35
LOG="/tmp/pio_upload_monitor.log"
DO_BUILD=1
DO_UPLOAD=1
MONITOR_ONLY=0
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
        --no-upload) DO_UPLOAD=0; shift ;;
        --monitor-only) MONITOR_ONLY=1; DO_BUILD=0; DO_UPLOAD=0; shift ;;
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
#    one file and want to skip this, pass --no-build. (Note: --no-build
#    only skips step 1; the upload+monitor in step 2 still runs. Use
#    --no-upload for a true build-only check.)
if [ "$DO_BUILD" = 1 ]; then
    echo "[pio-upload-monitor] pio run -e $ENV (full build)..." >&2
    if ! pio run -e "$ENV" 2>&1 | tee -a "$LOG" | tail -3 ; then
        echo "[pio-upload-monitor] BUILD FAILED for env=$ENV; see $LOG" >&2
        exit 2
    fi
    echo "[pio-upload-monitor] build OK" >&2
fi

if [ "$MONITOR_ONLY" = 1 ]; then
    # --monitor-only: attach to the already-flashed device and tail
    # serial for $MONITOR_SECS seconds. No build, no upload. The point
    # is to give a clean way to watch the firmware *after* a flash
    # without re-flashing (the previous "use --no-build to monitor"
    # hack was a footgun: --no-build skipped the standalone build but
    # still ran -t upload -t monitor, which re-flashed every time).
    #
    # Heartbeats from the firmware (heartbeatTick() in src/state.cpp)
    # are what make this useful: without them, a stuck radio looks
    # identical to a crashed firmware and a 30-second monitor window
    # shows you nothing. With heartbeats at 2 s, a 30-second window
    # gives you ~15 lines of "Heartbeat: ..." which is enough to
    # confirm the loop is still running.
    echo "[pio-upload-monitor] --monitor-only set; attaching to $ENV for ${MONITOR_SECS}s (no build, no upload)." >&2
    script -q "$LOG" pio device monitor --environment "$ENV" &
    PIO_PID=$!
    sleep "$MONITOR_SECS"
    kill -INT "$PIO_PID" 2>/dev/null || true
    sleep 2
    kill -9  "$PIO_PID" 2>/dev/null || true
    wait "$PIO_PID" 2>/dev/null || true
    # Fall through to the log-printing + status-summary block below.
fi

if [ "$DO_UPLOAD" = 0 ] && [ "$MONITOR_ONLY" = 0 ]; then
    # --no-upload: skip the flash + monitor entirely. The standalone build
    # (above) already proved the firmware compiles; nothing more to do.
    # Per AGENT.md §4, flashing requires explicit user permission.
    echo "[pio-upload-monitor] --no-upload set; skipping flash + monitor."
    exit 0
fi

# 2. Upload + monitor under script(1) so pio monitor sees a pty.
#    `script -q` runs the inner command attached to a pseudo-tty and
#    tees the session transcript to $LOG. `-q` suppresses the
#    "Script started on..." / "Script done on..." banners.
#
#    Skipped when MONITOR_ONLY=1 -- that path is handled above (the
#    monitor runs but no upload happens) and falls through to the
#    log-print + status-summary block below.
if [ "$MONITOR_ONLY" = 1 ]; then
    :  # no-op; the monitor-only block above already ran the monitor
else
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
fi  # close the MONITOR_ONLY guard around step 2

# 4. Print the meaningful part of the log.
#    - Heartbeats stripped by default (turn off with --keep-heartbeats).
#    - ArduinoJson StaticJsonDocument deprecation blocks are stripped
#      ALWAYS (not gated on a flag). Those warnings live in pre-existing
#      code (src/network.cpp had StaticJsonDocument<256> calls before
#      this script was written) and produce three lines of noise per
#      occurrence. The recommended migration is to JsonDocument; that
#      cleanup is a separate task from anything that touches the
#      build / flash / monitor pipeline. Re-introduce the noise by
#      piping through `cat` yourself if you ever need to see it.
echo
echo "================================================================"
echo "[pio-upload-monitor] log lines from $LOG:"
echo "================================================================"
if [ "$FILTER_HEARTBEATS" = 1 ]; then
    # Strip heartbeats. The ArduinoJson deprecation block is removed
    # below via a second pass.
    grep -v "^Heartbeat:" "$LOG" \
        | grep -v "^src/.*warning: '.*StaticJsonDocument" \
        | grep -v "^.*note: declared here" \
        | grep -v "^.*compatibility.hpp:.*$" \
        || true
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
elif [ "$MONITOR_ONLY" = 1 ]; then
    # Monitor-only path: there's no Verifying Flash line and no
    # PlatformIO [SUCCESS] line to look for. Treat the window as
    # successful if we captured any output (heartbeats, log lines,
    # whatever) -- if the log is empty AND the device should have
    # been producing heartbeats, the operator can dig in.
    if [ -s "$LOG" ]; then
        echo
        echo "[pio-upload-monitor] monitor-only window elapsed; captured output above."
        exit 0
    else
        echo
        echo "[pio-upload-monitor] monitor-only window elapsed with no output; check $LOG"
        exit 1
    fi
else
    echo
    echo "[pio-upload-monitor] monitor window ended without a clear success line; check $LOG"
    exit 1
fi
