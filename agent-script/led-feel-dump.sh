#!/usr/bin/env bash
# agent-script/led-feel-dump.sh
#
# Print every `led` option as JSON, at the value the firmware uses when a
# config does not set it.
#
# This is the reference for /consoles.json's `led` block, generated from
# defaultLedFeel() rather than typed by hand -- see the note in
# led-feel-dump.cpp for why a hand-written copy is the thing to avoid.
#
# Usage:
#   ./agent-script/led-feel-dump.sh              # print the block
#   ./agent-script/led-feel-dump.sh -o file      # write it to a file
#
# The output is a JSON *fragment* -- `"led": { ... }` with two tabs of
# indentation -- so it can be pasted straight into a config that already
# has its other keys at one tab. Dropping it in whole is a syntax error
# if the file already has a top-level `led`; that is deliberate, because
# a config should have one.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"

if ! command -v c++ >/dev/null 2>&1; then
    cat >&2 <<'EOF'
[agent-script] No host C++ compiler found.

Same requirement as the simulator: Xcode command line tools on macOS,
    xcode-select --install
EOF
    exit 2
fi

out="$(mktemp -t ledfeel)"
trap 'rm -f "$out"' EXIT

# The defaults file is pure data and needs no JSON library, which is why
# it is a separate translation unit from the parser.
#
# It does need CabinetMenu.h, because ConsoleConfig.h parses the
# `menu` array into CabinetMenu's MenuItem and the parser's header
# cannot name a type it does not include. That costs this script a
# second include path, and it is a real price for the split -- the
# "compile one file to learn the defaults" trick now needs two
# directories on the path. Flagged rather than hidden: if the menu ever
# wants to stop living in the config's header, this goes away.
c++ -std=c++11 -O0 -Wall -Wextra \
    -I "$root/lib/ConsoleConfig/src" \
    -I "$root/lib/CabinetMenu/src" \
    -o "$out" \
    "$here/led-feel-dump.cpp" \
    "$root/lib/ConsoleConfig/src/LedFeelDefaults.cpp"

if [[ "${1:-}" == "-o" ]]; then
    if [[ -z "${2:-}" ]]; then
        echo "[agent-script] -o needs a filename" >&2
        exit 1
    fi
    "$out" > "$2"
    echo "[agent-script] wrote $2"
else
    "$out"
fi
