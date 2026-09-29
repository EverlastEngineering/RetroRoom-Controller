#!/usr/bin/env bash
# agent-script/ledstring-sim.sh
#
# Build and run the offline LED-string animation simulator
# (ledstring-sim.cpp). Replays the exact frame math the firmware uses
# and prints each frame as an ASCII strip, so the "feel" can be
# iterated on without flashing and walking around the cabinet.
#
# Every LEDSTRING_* value it uses is #included from
# src/configuration.h, and every frame comes from
# retroroom_core::computeStripFrame() -- the same call src/ledstring.cpp
# makes. Change a define, re-run this, see the result.
#
# Usage:
#   ./agent-script/ledstring-sim.sh              # every scenario
#   ./agent-script/ledstring-sim.sh browse       # one scenario
#   ./agent-script/ledstring-sim.sh fastspin|reverse|select|frames
#
# Exit codes:
#   0  simulator ran
#   1  bad CLI args, or the build failed
#   2  no host C++ compiler found
#   3  the simulator exited non-zero

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"

if ! command -v c++ >/dev/null 2>&1; then
    cat >&2 <<'EOF'
[agent-script] No host C++ compiler found.

The simulator is plain C++ and needs only a compiler and the standard
library -- no PlatformIO, no Arduino, no FastLED. On macOS the Xcode
command line tools provide it:

    xcode-select --install
EOF
    exit 2
fi

scenario="${1:-all}"
out="$(mktemp -t ledstringsim)"
trap 'rm -f "$out"' EXIT

# -I on both the lib and src: the simulator includes <LedStringPaint.h>
# and "configuration.h". src/configuration.h guards its board-specific
# blocks behind ARDUINO_RASPBERRY_PI_* symbols, so a host build gets
# only the LEDSTRING_* block.
c++ -std=c++11 -O0 -Wall -Wextra \
    -I "$root/lib/LedStringPaint/src" \
    -I "$root/src" \
    -o "$out" \
    "$here/ledstring-sim.cpp" \
    "$root/lib/LedStringPaint/src/LedStringPaint.cpp"

"$out" "$scenario"
