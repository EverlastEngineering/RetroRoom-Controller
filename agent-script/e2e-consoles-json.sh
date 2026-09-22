#!/usr/bin/env bash
# agent-script/e2e-consoles-json.sh
#
# End-to-end test for the /consoles.json upload endpoint + the
# /next / /prev console-cycling endpoints. Drives the live device over
# HTTP from the host; uses curl for the body POSTs and JSON GETs.
#
# Usage:
#   ./agent-script/e2e-consoles-json.sh                          # run --all against RetroRoom.local
#   ./agent-script/e2e-consoles-json.sh --host 192.168.1.100     # explicit IP
#   ./agent-script/e2e-consoles-json.sh --scenario cycle-three   # one scenario only
#   ./agent-script/e2e-consoles-json.sh --list                   # list scenarios
#
# Discovery: the Pico 2 W advertises itself on DHCP+mDNS as
# "RetroRoom" (see kHostname in src/network.cpp), so RetroRoom.local
# works on any mDNS-aware LAN without a hardcoded IP. Pass --host to
# override.
#
# Exit codes:
#   0   all selected scenarios passed
#   1   bad CLI args
#   2   device unreachable (curl --max-time 3 failed)
#   3   a scenario failed (assert_eq / assert_in failed)
#   4   jq missing (we refuse to parse JSON with regex)
#
# Each scenario prints SCENARIO:<name>:PASS or SCENARIO:<name>:FAIL
# on its own line so a CI runner can grep them out.

set -u

HOST="${HOST:-RetroRoom.local}"
PORT="${PORT:-80}"
CONFIG_DIR="${CONFIG_DIR:-$(cd "$(dirname "$0")/.." && pwd)/example-configurations}"
BOOT_WAIT_SECS="${BOOT_WAIT_SECS:-15}"
SCENARIO_FILTER=""

print_usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

# ---------- arg parsing ----------

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --config-dir) CONFIG_DIR="$2"; shift 2 ;;
        --boot-wait) BOOT_WAIT_SECS="$2"; shift 2 ;;
        --scenario) SCENARIO_FILTER="$2"; shift 2 ;;
        --all) SCENARIO_FILTER=""; shift ;;
        --list) SCENARIO_FILTER="__list"; shift ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; print_usage >&2; exit 1 ;;
    esac
done

BASE_URL="http://${HOST}:${PORT}"

# ---------- preflight ----------

if ! command -v curl >/dev/null 2>&1; then
    echo "FATAL: curl is required" >&2
    exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "FATAL: jq is required (we don't parse JSON with regex)" >&2
    exit 4
fi

# ---------- assertion primitives ----------

# assert_eq <expected> <actual> <label>
assert_eq() {
    local expected="$1"; local actual="$2"; local label="$3"
    if [ "$expected" = "$actual" ]; then
        echo "  ok    $label = $actual"
        return 0
    fi
    echo "  FAIL  $label: expected '$expected', got '$actual'" >&2
    return 1
}

# assert_in <needle> <haystack> <label>
assert_in() {
    local needle="$1"; local haystack="$2"; local label="$3"
    if echo "$haystack" | grep -q -- "$needle"; then
        echo "  ok    $label contains '$needle'"
        return 0
    fi
    echo "  FAIL  $label: '$needle' not found in:" >&2
    echo "$haystack" | sed 's/^/    /' >&2
    return 1
}

# ---------- HTTP helpers ----------

# Wait up to BOOT_WAIT_SECS for the device to respond to /healthcheck.
# After POST /consoles.json the device reboots within ~1.5 s; the
# network stack then comes back up within a few seconds (STA mode
# reconnects to the saved AP).
wait_for_device() {
    local i=0
    while [ "$i" -lt "$BOOT_WAIT_SECS" ]; do
        if curl -fsS --max-time 2 "$BASE_URL/healthcheck" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done
    echo "  FAIL  device did not respond to /healthcheck after ${BOOT_WAIT_SECS}s" >&2
    return 1
}

# GET <path> -> echoes body; non-zero on non-2xx.
http_get() {
    curl -fsS --max-time 5 "$BASE_URL$1"
}

# POST <path> <file> -> echoes body; non-zero on non-2xx.
http_post_file() {
    curl -fsS --max-time 8 -X POST --data-binary "@$2" \
        -H "Content-Type: application/json" "$BASE_URL$1"
}

# POST <path> <literal-string> -> echoes body; non-zero on non-2xx.
http_post_string() {
    curl -fsS --max-time 8 -X POST --data-binary "$2" \
        -H "Content-Type: application/json" "$BASE_URL$1"
}

# ----------- scenarios -----------

# Capture "every scenario set a SCENARIO_RESULT variable" so the
# top-level runner knows whether to keep going or stop.
SCENARIO_RESULT=0

scenario_healthcheck() {
    local body rc=0
    body="$(http_get /healthcheck 2>/dev/null)" || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "  FAIL  GET /healthcheck failed (curl exit $rc); is the device online at $BASE_URL ?" >&2
        return 1
    fi
    assert_eq "OK" "$body" "/healthcheck body" || rc=1
    return $rc
}

# POST /consoles.json with the multi-console example, wait for the
# reboot, walk /next and /prev and verify the index + name flip in
# the expected wraparound pattern. Uses the live /state.json as the
# ground truth (HTTP-driven cycle endpoints also broadcast a
# `console:N:idx` WS message, but that only shows on the embedded UI).
scenario_cycle_three_consoles() {
    local rc=0
    local config_file="$CONFIG_DIR/example1.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    echo "  posting $config_file -> POST /consoles.json"
    if ! http_post_file /consoles.json "$config_file" >/dev/null; then
        echo "  FAIL  POST /consoles.json did not return 2xx" >&2
        return 1
    fi
    echo "  POST OK, waiting for reboot..."
    if ! wait_for_device; then
        echo "  FAIL  device did not come back after reboot" >&2
        return 1
    fi
    # After reboot, currentConsoleIndex retains whatever value it
    # had pre-reboot (it's a RAM-only global). So the device may
    # boot into any index in [0, n_consoles). Capture the baseline
    # the device actually boots into and derive all assertions
    # from it -- never hardcode "index 0" or "NES".
    local baseline_idx baseline_name n_consoles
    n_consoles=3  # example1.json
    baseline_idx="$(http_get /state.json | jq -r .index)"
    baseline_name="$(http_get /state.json | jq -r .name)"
    echo "  baseline after reboot: idx=$baseline_idx name=$baseline_name"

    # Build the index->name table by walking /next from the
    # baseline through every other index. After n_consoles-1
    # forward steps we have visited every console and learned its
    # name; the n_consoles-th step lands us back on baseline,
    # which we treat as the start of the assertion loop below.
    declare -A name_table
    name_table["$baseline_idx"]="$baseline_name"
    for ((step=1; step<n_consoles; step++)); do
        local body got_idx got_name
        body="$(http_get /next)" || { echo "  FAIL  GET /next failed (table step=$step)" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        name_table["$got_idx"]="$got_name"
    done
    if [ "${#name_table[@]}" -ne "$n_consoles" ]; then
        echo "  FAIL  name table has ${#name_table[@]} entries, expected $n_consoles; keys: ${!name_table[*]}" >&2
        rc=1
        return $rc
    fi

    # Now walk /next from baseline once more. After n_consoles
    # forward steps we must be back at baseline (wraparound proof).
    local current_idx="$baseline_idx"
    for ((step=1; step<=n_consoles; step++)); do
        local exp_idx=$(( (current_idx + 1) % n_consoles ))
        local exp_name="${name_table[$exp_idx]}"
        local got_idx got_name body
        body="$(http_get /next)" || { echo "  FAIL  GET /next failed (assert step=$step)" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        assert_eq "$exp_idx" "$got_idx" "/next step=$step index" || rc=1
        assert_eq "$exp_name" "$got_name" "/next step=$step name" || rc=1
        current_idx="$got_idx"
    done
    assert_eq "$baseline_idx" "$current_idx" "after $n_consoles /next we wrap to baseline" || rc=1

    # Walk /prev n_consoles times; same wraparound rule applies.
    current_idx="$baseline_idx"
    for ((step=1; step<=n_consoles; step++)); do
        local raw=$((current_idx - 1))
        local exp_idx=$(( (raw % n_consoles + n_consoles) % n_consoles ))
        local exp_name="${name_table[$exp_idx]}"
        local got_idx got_name body
        body="$(http_get /prev)" || { echo "  FAIL  GET /prev failed (step=$step)" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        assert_eq "$exp_idx" "$got_idx" "/prev step=$step index" || rc=1
        assert_eq "$exp_name" "$got_name" "/prev step=$step name" || rc=1
        current_idx="$got_idx"
    done
    assert_eq "$baseline_idx" "$current_idx" "after $n_consoles /prev we wrap to baseline" || rc=1

    return $rc
}

# POST a 4-console config (example2.json) and verify the wraparound
# bound moves to 4 consoles. Same dynamic-table pattern as the
# 3-console scenario so the test stays robust to whatever
# index+name the device boots into.
scenario_cycle_four_consoles() {
    local rc=0
    local config_file="$CONFIG_DIR/example2.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    if ! http_post_file /consoles.json "$config_file" >/dev/null; then
        echo "  FAIL  POST /consoles.json did not return 2xx" >&2
        return 1
    fi
    echo "  POST OK, waiting for reboot..."
    if ! wait_for_device; then
        echo "  FAIL  device did not come back after reboot" >&2
        return 1
    fi
    local baseline_idx baseline_name n_consoles
    n_consoles=4  # example2.json
    baseline_idx="$(http_get /state.json | jq -r .index)"
    baseline_name="$(http_get /state.json | jq -r .name)"
    echo "  baseline after reboot: idx=$baseline_idx name=$baseline_name"

    declare -A name_table
    name_table["$baseline_idx"]="$baseline_name"
    for ((step=1; step<n_consoles; step++)); do
        local body got_idx got_name
        body="$(http_get /next)" || { echo "  FAIL  GET /next failed (table step=$step)" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        name_table["$got_idx"]="$got_name"
    done
    if [ "${#name_table[@]}" -ne "$n_consoles" ]; then
        echo "  FAIL  name table has ${#name_table[@]} entries, expected $n_consoles" >&2
        rc=1
        return $rc
    fi

    local current_idx="$baseline_idx"
    for ((step=1; step<=n_consoles; step++)); do
        local exp_idx=$(( (current_idx + 1) % n_consoles ))
        local exp_name="${name_table[$exp_idx]}"
        local got_idx got_name body
        body="$(http_get /next)" || { echo "  FAIL  GET /next failed (assert step=$step)" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        assert_eq "$exp_idx" "$got_idx" "/next step=$step index" || rc=1
        assert_eq "$exp_name" "$got_name" "/next step=$step name" || rc=1
        current_idx="$got_idx"
    done
    assert_eq "$baseline_idx" "$current_idx" "after $n_consoles /next we wrap to baseline" || rc=1
    return $rc
}

# POST a JSON body that the core's parser will reject. We expect 400
# and a JSON `{"error":"..."}` body. CRITICAL: the device must NOT
# reboot -- if it does, /healthcheck will hang for BOOT_WAIT_SECS.
scenario_post_rejects_malformed_json() {
    local rc=0
    local status_body
    # Use curl -w to capture HTTP status AND body in one call. -f
    # would make us treat 4xx as error here (good -- we expect 4xx)
    # but it also suppresses the body, so don't use -f; use -sS
    # and check status manually.
    local out
    out="$(curl -sS --max-time 5 -X POST \
        --data-binary '{ this is not json' \
        -H "Content-Type: application/json" \
        -o /tmp/e2e_post_malformed.body \
        -w '%{http_code}' \
        "$BASE_URL/consoles.json")" || { echo "  FAIL  curl failed" >&2; return 1; }
    assert_eq "400" "$out" "HTTP status for malformed body" || rc=1
    local body
    body="$(cat /tmp/e2e_post_malformed.body 2>/dev/null || true)"
    assert_in '"error"' "$body" "response body is JSON error shape" || rc=1
    assert_in 'JSON parse error' "$body" "parser error string surfaced" || rc=1

    # Critical: confirm the device did NOT reboot. /healthcheck should
    # still answer immediately.
    local hc
    hc="$(curl -sS --max-time 3 "$BASE_URL/healthcheck" 2>/dev/null || echo DOWN)"
    assert_eq "OK" "$(echo "$hc" | tr -d '\n')" "device still online after rejected POST" || rc=1
    return $rc
}

# POST a structurally-valid JSON whose tvInput references an IR code
# that doesn't exist in the irCodes map. Core parser should reject
# this with a typed error. Same reboot-isolation check.
scenario_post_rejects_unknown_tvinput() {
    local rc=0
    local body_out='{"irCodes":{"Video":"0x430"},"consoleNames":{"NES":"Nintendo Entertainment System"},"consoles":[{"id":"NES","tvInput":"HDMI","selectorPosition":1,"ledPosition":1,"ledWidth":1}]}'
    local status
    status="$(curl -sS --max-time 5 -X POST \
        --data "$body_out" \
        -H "Content-Type: application/json" \
        -o /tmp/e2e_post_unknown.body \
        -w '%{http_code}' \
        "$BASE_URL/consoles.json")" || { echo "  FAIL  curl failed" >&2; return 1; }
    assert_eq "400" "$status" "HTTP status for unknown-tvInput body" || rc=1
    local body
    body="$(cat /tmp/e2e_post_unknown.body 2>/dev/null || true)"
    assert_in '"error"' "$body" "response body is JSON error shape" || rc=1
    assert_in 'unknown tvInput' "$body" "core error string surfaced" || rc=1

    local hc
    hc="$(curl -sS --max-time 3 "$BASE_URL/healthcheck" 2>/dev/null || echo DOWN)"
    assert_eq "OK" "$(echo "$hc" | tr -d '\n')" "device still online after rejected POST" || rc=1
    return $rc
}

# POST a config that is syntactically valid but contains NO consoles
# (empty consoles array). The boot path should accept this -- there's
# no structural error, just an empty list. Device should still come
# back; we don't try to /next through it because CurrentConsole()
# on an empty vector is UB and the firmware logs "no consoles loaded"
# instead of advancing.
scenario_post_empty_config() {
    local rc=0
    local config_file="$CONFIG_DIR/empty.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    if ! http_post_file /consoles.json "$config_file" >/dev/null; then
        echo "  FAIL  POST /consoles.json (empty config) did not return 2xx" >&2
        return 1
    fi
    echo "  POST empty config OK, waiting for reboot..."
    if ! wait_for_device; then
        echo "  FAIL  device did not come back after empty-config POST" >&2
        return 1
    fi
    local hc
    hc="$(curl -sS --max-time 3 "$BASE_URL/healthcheck" 2>/dev/null || echo DOWN)"
    assert_eq "OK" "$(echo "$hc" | tr -d '\n')" "device online after empty-config POST" || rc=1
    return $rc
}

# ---------- runner ----------

# Run a scenario by name. Sets SCENARIO_RESULT.
run_scenario() {
    local name="$1"
    local fn="scenario_${name//-/_}"
    if ! type "$fn" >/dev/null 2>&1; then
        echo "SCENARIO:$name:UNKNOWN (no function $fn)" >&2
        SCENARIO_RESULT=99
        return
    fi
    echo
    echo "=== scenario: $name ==="
    if "$fn"; then
        echo "SCENARIO:$name:PASS"
        SCENARIO_RESULT=0
    else
        echo "SCENARIO:$name:FAIL" >&2
        SCENARIO_RESULT=1
    fi
}

ALL_SCENARIOS=(
    healthcheck
    post-rejects-malformed-json
    post-rejects-unknown-tvinput
    cycle-three-consoles
    cycle-four-consoles
    post-empty-config
)

if [ "$SCENARIO_FILTER" = "__list" ]; then
    echo "available scenarios:"
    for s in "${ALL_SCENARIOS[@]}"; do
        echo "  $s"
    done
    exit 0
fi

# Preflight: refuse to start if the device isn't even answering
# /healthcheck. Saves a confusing scenario list where every item
# fails for the same reason.
if ! curl -fsS --max-time 3 "$BASE_URL/healthcheck" >/dev/null 2>&1; then
    echo "FATAL: $BASE_URL/healthcheck did not respond (host=$HOST)" >&2
    echo "       pass --host <ip> if mDNS isn't resolving RetroRoom.local" >&2
    exit 2
fi

OVERALL_RESULT=0

if [ -n "$SCENARIO_FILTER" ]; then
    run_scenario "$SCENARIO_FILTER" || OVERALL_RESULT=$SCENARIO_RESULT
else
    for s in "${ALL_SCENARIOS[@]}"; do
        run_scenario "$s"
        # Each scenario's pass/fail lands in SCENARIO_RESULT. Don't
        # stop on the first failure -- the operator wants to see all
        # of them at once for triage. Keep going, track the worst.
        if [ "$SCENARIO_RESULT" -ne 0 ] && [ "$OVERALL_RESULT" -eq 0 ]; then
            OVERALL_RESULT=$SCENARIO_RESULT
        fi
    done
fi

echo
if [ "$OVERALL_RESULT" -eq 0 ]; then
    echo "ALL OK"
    exit 0
else
    echo "FAILED (exit $OVERALL_RESULT)" >&2
    exit 3
fi
