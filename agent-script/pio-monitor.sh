#!/usr/bin/env bash
# pio-monitor.sh — start/stop a PlatformIO serial monitor in background or
# foreground. Output is always captured to a per-session log file in the repo
# root so the agent can read_file/grep_search it without holding a TTY.
#
# Subcommands:
#   bg --port <path> [--baud N] [--env NAME] [--log <path>]
#       Detach `pio device monitor` and return the PID + log path.
#   fg --port <path> [--baud N] [--env NAME] [--log <path>]
#       Stream to TTY (also save to log). Blocks until Ctrl+C / device disconnect.
#   list
#       Show active background monitors.
#   stop [--pid N]
#       Kill a specific monitor (or all when --pid is omitted).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
STATE_FILE="$SCRIPT_DIR/.pio-monitors.state"

# Resolve `pio` with a sensible fallback.
if ! command -v pio >/dev/null 2>&1; then
  if [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    export PATH="$HOME/.platformio/penv/bin:$PATH"
  fi
fi
if ! command -v pio >/dev/null 2>&1; then
  echo "pio not found on PATH or in ~/.platformio/penv/bin" >&2
  exit 127
fi

usage() {
  sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

case "${1:-}" in
  list)
    if [ ! -s "$STATE_FILE" ]; then
      echo "(no active background monitors)"
      exit 0
    fi
    cat "$STATE_FILE"
    exit 0
    ;;
  bg|fg)
    MODE="$1"; shift
    PORT=""
    BAUD=76800
    ENV_NAME="nodemcuv2"
    LOG=""
    while [ $# -gt 0 ]; do
      case "$1" in
        --port)            PORT="$2"; shift 2 ;;
        --baud)            BAUD="$2"; shift 2 ;;
        --env|--environment) ENV_NAME="$2"; shift 2 ;;
        --log|--log-path)  LOG="$2"; shift 2 ;;
        -h|--help)         usage; exit 0 ;;
        *)                 echo "unknown arg: $1" >&2; exit 1 ;;
      esac
    done
    if [ -z "$PORT" ]; then
      echo "--port is required (e.g. /dev/cu.usbserial-11330)" >&2
      exit 1
    fi

    SAFE_PORT=$(basename "$PORT" | tr '/.:' '___')
    STAMP=$(date -u +%Y%m%dT%H%M%SZ)
    if [ -z "$LOG" ]; then
      LOG="$REPO_ROOT/serial-log-${SAFE_PORT}-${STAMP}.txt"
    fi

    TS_HEADER="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    {
      echo "=== serial monitor @ $TS_HEADER ==="
      echo "mode: $MODE"
      echo "port: $PORT"
      echo "baud: $BAUD"
      echo "env:  $ENV_NAME"
      echo "log:  $LOG"
      echo "---"
    } > "$LOG"

    CMD=(pio device monitor --port "$PORT" --baud "$BAUD" --environment "$ENV_NAME")
    echo "cmd: ${CMD[*]}" >> "$LOG"

    if [ "$MODE" = "fg" ]; then
      # Foreground: stream to TTY and append to log. Block until device disconnects / Ctrl+C.
      "${CMD[@]}" 2>&1 | tee -a "$LOG"
      echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] foreground session ended" >> "$LOG"
      exit 0
    fi

    # Background: detach, save PID, return immediately.
    nohup "${CMD[@]}" >> "$LOG" 2>&1 &
    PID=$!
    disown "$PID" 2>/dev/null || true

    STARTED="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'pid=%s port=%s baud=%s env=%s started=%s log=%s mode=%s\n' \
      "$PID" "$PORT" "$BAUD" "$ENV_NAME" "$STARTED" "$LOG" "$MODE" \
      >> "$STATE_FILE"

    echo "started"
    echo "pid:    $PID"
    echo "port:   $PORT"
    echo "baud:   $BAUD"
    echo "env:    $ENV_NAME"
    echo "log:    $LOG"
    echo "stop:   pio-monitor.sh stop --pid $PID"
    exit 0
    ;;
  stop)
    shift
    TARGET_PID=""
    while [ $# -gt 0 ]; do
      case "$1" in
        --pid) TARGET_PID="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)      echo "unknown arg: $1" >&2; exit 1 ;;
      esac
    done

    if [ ! -s "$STATE_FILE" ]; then
      echo "(no active background monitors)"
      exit 0
    fi

    TMP=$(mktemp)
    STOPPED=0
    REMAINING=0
    while IFS= read -r line; do
      [ -z "$line" ] && continue
      this_pid=$(printf '%s\n' "$line" | sed -n 's/^pid=\([0-9]*\).*/\1/p')
      # If a target was specified and this line isn't it, keep it.
      if [ -n "$TARGET_PID" ] && [ "$this_pid" != "$TARGET_PID" ]; then
        echo "$line" >> "$TMP"
        REMAINING=$((REMAINING + 1))
        continue
      fi
      # Try to kill it.
      if kill -0 "$this_pid" 2>/dev/null; then
        if kill -TERM "$this_pid" 2>/dev/null; then
          echo "stopped pid=$this_pid"
          STOPPED=$((STOPPED + 1))
        else
          echo "perm denied pid=$this_pid" >&2
          echo "$line" >> "$TMP"
          REMAINING=$((REMAINING + 1))
        fi
      else
        echo "stopped pid=$this_pid (already dead)"
        STOPPED=$((STOPPED + 1))
      fi
    done < "$STATE_FILE"
    mv "$TMP" "$STATE_FILE"
    echo "summary: stopped=$STOPPED remaining=$REMAINING"
    exit 0
    ;;
  -h|--help|"")
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac
