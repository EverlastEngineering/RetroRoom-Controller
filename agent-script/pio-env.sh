# agent-script/pio-env.sh
#
# Shared PlatformIO resolver. Sourced by the other agent-script/ wrappers
# -- not meant to be executed directly.
#
# Why this exists
# ---------------
# PlatformIO's installer drops the `pio` shim into ~/.platformio/penv/bin
# and expects you to put it on PATH yourself. On a fresh machine (or any
# non-interactive shell) that has not happened, and every wrapper in this
# directory dies with a bare "command not found" that looks like the repo
# is broken. So each wrapper resolves the CLI once, here, and fails loudly
# with install instructions if it genuinely isn't there.
#
# Usage (from a sibling wrapper):
#
#     . "$(dirname "$0")/pio-env.sh"    # sets $PIO; exits 78 if not found
#     "$PIO" run -e pico2w
#
# Exit code 78 = EX_CONFIG, per sysexits(3): "configuration error".

_rr_resolve_pio() {
    # 1. Whatever is already on PATH wins. This deliberately respects the
    #    user's own choice (pipx, a distro package, a pinned version, a
    #    different PLATFORMIO_CORE_DIR) instead of second-guessing it.
    if command -v pio >/dev/null 2>&1; then
        command -v pio
        return 0
    fi

    # 2. PlatformIO's own virtualenv. PLATFORMIO_CORE_DIR is the supported
    #    override; the default root is ~/.platformio.
    for _candidate in \
        "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/penv/bin/pio" \
        "$HOME/.platformio/penv/bin/pio"
    do
        if [ -x "$_candidate" ]; then
            echo "$_candidate"
            return 0
        fi
    done

    return 1
}

if ! PIO="$(_rr_resolve_pio)"; then
    cat >&2 <<'EOF'
[agent-script] PlatformIO CLI not found.

Searched:
  1. `pio` on PATH
  2. $PLATFORMIO_CORE_DIR/penv/bin/pio  (default ~/.platformio/penv/bin/pio)

This shell may simply not have PlatformIO on PATH -- a restart of the
editor or terminal, or a fresh login shell, often fixes it. To install
it, see https://platformio.org/install/ , e.g.:

    pipx install platformio

then make sure `pio` is on PATH for non-interactive shells too
(~/.zprofile on macOS, ~/.bashrc on Linux), e.g.:

    ln -s ~/.platformio/penv/bin/pio /opt/homebrew/bin/pio
EOF
    exit 78
fi

export PIO
