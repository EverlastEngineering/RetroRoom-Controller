#!/usr/bin/env bash
# pio.sh — invoke PlatformIO via the same wrapper whether triggered by MCP,
# the user, or a CI job. Streams everything to <repo>/pio-log.txt (append) and
# prints a one-line summary to stdout. Exit code reflects pio's exit code.
#
# Usage: pio.sh <pio-args...>
# Examples:
#   pio.sh device list
#   pio.sh run
#   pio.sh run --target upload --upload-port /dev/cu.usbserial-11330

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_FILE="$REPO_ROOT/pio-log.txt"

# Resolve `pio` with a sensible fallback to the homebrew venv install.
if ! command -v pio >/dev/null 2>&1; then
  if [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    export PATH="$HOME/.platformio/penv/bin:$PATH"
  fi
fi
if ! command -v pio >/dev/null 2>&1; then
  echo "pio not found on PATH or in ~/.platformio/penv/bin" >&2
  exit 127
fi

TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
{
  echo "=== pio @ $TS ==="
  echo "cmd: pio $*"
  echo "cwd: $REPO_ROOT"
  echo "---"
} >> "$LOG_FILE"

pio "$@" >> "$LOG_FILE" 2>&1
EXIT=$?

# Pull a short summary from the tail of the log for terminal feedback.
SUMMARY=$(tail -n 80 "$LOG_FILE" \
  | grep -E '\[SUCCESS\]|\[FAILED\]|Error:|\bFAILED\b|Hash of data verified|Wrote [0-9]+ bytes|Leaving' \
  | tail -n 3 \
  | tr '\n' ';' \
  | sed 's/;$//')

if [ "$EXIT" -eq 0 ]; then
  printf 'OK   pio %s   ::   %s\n' "$*" "${SUMMARY:-no summary line found}"
else
  printf 'ERR(%d)  pio %s   ::   %s\n   full log: %s\n' \
    "$EXIT" "$*" "${SUMMARY:-no summary line found}" "$LOG_FILE"
fi

exit "$EXIT"
