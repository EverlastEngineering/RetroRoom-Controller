#!/usr/bin/env bash
# agent-script/e2e-consoles-json.sh
#
# End-to-end test for the /consoles.json upload endpoint + the
# /next / /prev console-cycling endpoints. TDD-driven: every
# assertion reads back the device's actual state (via GET
# /state.json, GET /consoles.json), not response codes. The
# firmware surface this depends on landed in commits fc3b293
# (GET /consoles.json, enriched /state.json, empty-vector guards)
# and 512397c (onStateJson UB fix on empty vector).
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
#   3   a scenario failed (wait_for_state / assert_eq / assert_in / cmp)
#   4   jq missing (we refuse to parse JSON with regex)
#
# Each scenario prints SCENARIO:<name>:PASS or SCENARIO:<name>:FAIL
# on its own line so a CI runner can grep them out.

set -u

HOST="${HOST:-RetroRoom.local}"
PORT="${PORT:-80}"
CONFIG_DIR="${CONFIG_DIR:-$(cd "$(dirname "$0")/.." && pwd)/example-configurations}"
BOOT_WAIT_SECS="${BOOT_WAIT_SECS:-25}"
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

# GET <path> -> echoes body; non-zero on non-2xx. Bounded by curl's
# --max-time so a wedged JSON endpoint doesn't hang the whole suite.
http_get() {
    curl -fsS --max-time 5 "$BASE_URL$1"
}

# http_get_code <path> -> echoes "<http_code> <body>"; never fails.
# Use when you need to assert on a specific non-2xx status (204,
# 409, etc.) without curl -f aborting the read.
http_get_code() {
    local body_file
    body_file="$(mktemp -t e2e_get.XXXXXX)"
    local code
    code="$(curl -sS --max-time 5 -o "$body_file" -w '%{http_code}' "$BASE_URL$1" || echo 000)"
    local body
    body="$(cat "$body_file" 2>/dev/null || true)"
    rm -f "$body_file"
    printf '%s %s\n' "$code" "$body"
}

# POST <path> <file> -> echoes body; non-zero on non-2xx.
http_post_file() {
    curl -fsS --max-time 8 -X POST --data-binary "@$2" \
        -H "Content-Type: application/json" "$BASE_URL$1"
}

# ---------- TDD primitives ----------

# wait_for_device [timeout_secs]
#   Poll /healthcheck until it answers 200, or until timeout. After
#   the timeout, also assert /wifi reports mode=sta -- failing fast
#   if the device came back in SoftAP mode (which would mean the
#   /wifi.json write was clobbered or something else took the device
#   back to captive-portal). Default timeout = $BOOT_WAIT_SECS.
wait_for_device() {
    local timeout="${1:-$BOOT_WAIT_SECS}"
    local i=0
    while [ "$i" -lt "$timeout" ]; do
        if curl -fsS --max-time 2 "$BASE_URL/healthcheck" >/dev/null 2>&1; then
            local mode
            mode="$(http_get /wifi | jq -r .mode 2>/dev/null || echo unknown)"
            if [ "$mode" != "sta" ]; then
                echo "  FAIL  device came back but mode=$mode (expected sta); wifi creds probably wiped" >&2
                return 1
            fi
            return 0
        fi
        i=$((i + 1))
        sleep 1
    done
    echo "  FAIL  device did not respond to /healthcheck after ${timeout}s" >&2
    return 1
}

# wait_for_state <jq-filter> <expected> <timeout_secs>
#   The TDD primitive. Polls GET /state.json every 500 ms (so
#   /next's selectedAtUptimeMs tick is captured within ~one tick),
#   runs `jq -r "$1"` on each response, and returns 0 the first
#   time the result equals "$2". Returns 1 on timeout.
#
#   Examples:
#     wait_for_state .total 3 5       # wait up to 5 s for total == 3
#     wait_for_state .index 1 5       # wait up to 5 s for index == 1
#     wait_for_state .name '"NES"' 5  # literal-string jq filter
#
#   Why 500 ms (not 1 s)? selectedAtUptimeMs ticks every loop()
#   call (millis() based). 500 ms gives ~4 polls per heartbeat
#   (2 s), so we don't miss a transition that happens to fall
#   between two 1-second polls.
wait_for_state() {
    local filter="$1"; local expected="$2"; local timeout="$3"
    local deadline=$((SECONDS + timeout))
    local last_seen=""
    while [ "$SECONDS" -lt "$deadline" ]; do
        local body got
        body="$(curl -fsS --max-time 2 "$BASE_URL/state.json" 2>/dev/null || true)"
        if [ -n "$body" ]; then
            got="$(echo "$body" | jq -r "$filter" 2>/dev/null || echo "<jq error>")"
            last_seen="$got"
            if [ "$got" = "$expected" ]; then
                echo "  ok    state[$filter] = $expected (within ${timeout}s)"
                return 0
            fi
        fi
        sleep 0.5
    done
    echo "  FAIL  state[$filter] never became '$expected' within ${timeout}s; last seen: '$last_seen'" >&2
    return 1
}

# post_and_verify_disk <file>
#   The other TDD primitive. POSTs <file> to /consoles.json, waits
#   for the reboot to complete (asserting STA mode), and then GETs
#   /consoles.json to verify the bytes on disk are byte-identical
#   to what we sent. Prints a diff on mismatch.
#
#   Why byte-compare and not JSON re-parse: the firmware never
#   re-serializes on the read path (see the comment on
#   onConsolesJsonGet in src/network.cpp). A byte-cmp is the
#   strongest possible assertion: the operator's exact file landed
#   on the device, not a re-formatted copy.
#
#   Side effects: leaves the device booted into whatever index
#   currentConsoleIndex is at post-reboot (provably 0 on a fresh
#   boot thanks to .bss zeroing -- see plan §1).
post_and_verify_disk() {
    local file="$1"
    local rc=0
    if [ ! -f "$file" ]; then
        echo "  FAIL  config file missing: $file" >&2
        return 1
    fi
    echo "  posting $file -> POST /consoles.json"
    if ! http_post_file /consoles.json "$file" >/dev/null; then
        echo "  FAIL  POST /consoles.json did not return 2xx" >&2
        return 1
    fi
    echo "  POST OK, waiting for reboot..."
    if ! wait_for_device; then
        return 1
    fi
    local got_file
    got_file="$(mktemp -t e2e_get_back.XXXXXX)"
    if ! http_get /consoles.json > "$got_file"; then
        echo "  FAIL  GET /consoles.json did not return 2xx after POST" >&2
        rm -f "$got_file"
        return 1
    fi
    if cmp -s "$file" "$got_file"; then
        echo "  ok    POST bytes == GET bytes ($(wc -c < "$file" | tr -d ' ') bytes)"
    else
        echo "  FAIL  POST bytes != GET bytes; diff follows:" >&2
        diff "$file" "$got_file" | head -40 >&2 || true
        rc=1
    fi
    rm -f "$got_file"
    return $rc
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

# GET /state.json -- assert the new fields exist and have sane values.
# Doesn't depend on which console is loaded; just that the schema is
# right and the mode is sta.
scenario_state_json_shape() {
    local rc=0
    local body
    body="$(http_get /state.json)" || { echo "  FAIL  GET /state.json failed" >&2; return 1; }
    # jq -e exits 0 iff the expression is truthy. We use `has(f)`
    # to assert each field is present (even if its value is empty).
    local field
    for field in .index .total .name .ledOn .flash .mode .uptimeMs .selectedAtUptimeMs; do
        if ! echo "$body" | jq -e "has($field)" >/dev/null 2>&1; then
            echo "  FAIL  /state.json missing field $field" >&2
            rc=1
        fi
    done
    [ "$rc" -eq 0 ] && echo "  ok    /state.json has all expected fields"
    local mode
    mode="$(echo "$body" | jq -r .mode)"
    assert_eq "sta" "$mode" "/state.json mode" || rc=1
    local uptime_ms
    uptime_ms="$(echo "$body" | jq -r .uptimeMs)"
    # uptimeMs > 1s (firmware has been up at least a moment), < 1h
    # (catches wraparound-from-bad-init bugs).
    if [ "$uptime_ms" -ge 1000 ] 2>/dev/null && [ "$uptime_ms" -le 3600000 ] 2>/dev/null; then
        echo "  ok    uptimeMs = $uptime_ms (sane)"
    else
        echo "  FAIL  uptimeMs = $uptime_ms (out of range [1s, 1h])" >&2
        rc=1
    fi
    return $rc
}

# GET /consoles.json read-back after a POST. Pure TDD: the harness
# doesn't trust the POST response, it reads the device's actual
# state and byte-compares.
scenario_post_then_read_back() {
    local rc=0
    local config_file="$CONFIG_DIR/example1.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    post_and_verify_disk "$config_file" || rc=1
    if [ "$rc" -eq 0 ]; then
        wait_for_state .total 3 5 || rc=1
        local name
        name="$(http_get /state.json | jq -r .name)"
        case "$name" in
            "Nintendo Entertainment System"|"Super Nintendo Entertainment System"|"Sega Genesis")
                echo "  ok    /state.json name = '$name' (matches example1.json)" ;;
            *)
                echo "  FAIL  /state.json name = '$name' (not in example1.json)" >&2
                rc=1 ;;
        esac
    fi
    return $rc
}

# Walk /next three times with per-step state verification. This is
# the headline TDD scenario: every step asserts both the response
# body AND that /state.json matches what the response said. If the
# response lies (curl got a cached 200 from somewhere, etc.), the
# state assertion catches it.
scenario_cycle_three_consoles() {
    local rc=0
    local config_file="$CONFIG_DIR/example1.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    post_and_verify_disk "$config_file" || { rc=1; return $rc; }
    # After reboot, currentConsoleIndex is provably 0 (.bss zeroing
    # -- see plan §1). We assert that explicitly.
    wait_for_state .index 0 5 || rc=1
    # Baseline name = example1.json's first console = NES.
    # Asserting the absolute name is a stricter check than the
    # previous "whatever the device boots into" dance, and it
    # surfaces boot-path regressions (e.g. if config loading
    # silently falls back to PROGMEM).
    wait_for_state .name '"Nintendo Entertainment System"' 5 || rc=1
    local baseline_sel
    baseline_sel="$(http_get /state.json | jq -r .selectedAtUptimeMs)"
    echo "  baseline selectedAtUptimeMs = $baseline_sel"
    local steps=(1 2 3)
    local exp_names=(
        "Super Nintendo Entertainment System"
        "Sega Genesis"
        "Nintendo Entertainment System"
    )
    for ((i=0; i<${#steps[@]}; i++)); do
        local step="${steps[$i]}"
        local exp_name="${exp_names[$i]}"
        local body got_idx got_name
        body="$(http_get /next)" || { echo "  FAIL  GET /next step=$step failed" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        assert_eq "$step" "$got_idx" "/next step=$step response index" || rc=1
        assert_eq "$exp_name" "$got_name" "/next step=$step response name" || rc=1
        # Cross-check: /state.json should agree with the response.
        wait_for_state ".index" "$step" 3 || rc=1
        wait_for_state ".name" "\"$exp_name\"" 3 || rc=1
    done
    # After 3 /next calls we should be back at index 0 (wraparound).
    # Cross-check selectedAtUptimeMs changed (TDD proof of motion --
    # if /next silently no-op'd the response would still be 2xx but
    # selectedAtUptimeMs would not have moved).
    local final_sel
    final_sel="$(http_get /state.json | jq -r .selectedAtUptimeMs)"
    if [ "$final_sel" -gt "$baseline_sel" ] 2>/dev/null; then
        echo "  ok    selectedAtUptimeMs advanced $baseline_sel -> $final_sel across /next walk"
    else
        echo "  FAIL  selectedAtUptimeMs did not advance (baseline=$baseline_sel, final=$final_sel)" >&2
        rc=1
    fi
    return $rc
}

# 4-console example2.json, same shape as cycle-three but verifies the
# count moved and the wraparound bound is now 4.
scenario_cycle_four_consoles() {
    local rc=0
    local config_file="$CONFIG_DIR/example2.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    post_and_verify_disk "$config_file" || { rc=1; return $rc; }
    wait_for_state .index 0 5 || rc=1
    wait_for_state .total 4 5 || rc=1
    wait_for_state .name '"Nintendo Entertainment System"' 5 || rc=1
    local steps=(1 2 3 4)
    local exp_names=(
        "Sega Master System"
        "Microsoft Xbox"
        "MAME Arcade Cabinet"
        "Nintendo Entertainment System"
    )
    for ((i=0; i<${#steps[@]}; i++)); do
        local step="${steps[$i]}"
        local exp_name="${exp_names[$i]}"
        local body got_idx got_name
        body="$(http_get /next)" || { echo "  FAIL  GET /next step=$step failed" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        assert_eq "$step" "$got_idx" "/next step=$step response index" || rc=1
        assert_eq "$exp_name" "$got_name" "/next step=$step response name" || rc=1
        wait_for_state ".index" "$step" 3 || rc=1
        wait_for_state ".name" "\"$exp_name\"" 3 || rc=1
    done
    return $rc
}

# Walk /prev three times on a 3-console config. Mirror of cycle-three
# in the other direction. Catches bugs in the -1 wraparound math.
scenario_cycle_three_consoles_prev() {
    local rc=0
    local config_file="$CONFIG_DIR/example1.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    post_and_verify_disk "$config_file" || { rc=1; return $rc; }
    wait_for_state .index 0 5 || rc=1
    wait_for_state .name '"Nintendo Entertainment System"' 5 || rc=1
    local baseline_sel
    baseline_sel="$(http_get /state.json | jq -r .selectedAtUptimeMs)"
    # /prev from index 0 wraps to index 2 (Sega Genesis).
    local steps=(2 1 0)
    local exp_names=(
        "Sega Genesis"
        "Super Nintendo Entertainment System"
        "Nintendo Entertainment System"
    )
    for ((i=0; i<${#steps[@]}; i++)); do
        local exp_idx="${steps[$i]}"
        local exp_name="${exp_names[$i]}"
        local body got_idx got_name
        body="$(http_get /prev)" || { echo "  FAIL  GET /prev step=$((i+1)) failed" >&2; rc=1; return $rc; }
        got_idx="$(echo "$body" | jq -r .index)"
        got_name="$(echo "$body" | jq -r .name)"
        assert_eq "$exp_idx" "$got_idx" "/prev step=$((i+1)) response index" || rc=1
        assert_eq "$exp_name" "$got_name" "/prev step=$((i+1)) response name" || rc=1
        wait_for_state ".index" "$exp_idx" 3 || rc=1
        wait_for_state ".name" "\"$exp_name\"" 3 || rc=1
    done
    local final_sel
    final_sel="$(http_get /state.json | jq -r .selectedAtUptimeMs)"
    if [ "$final_sel" -gt "$baseline_sel" ] 2>/dev/null; then
        echo "  ok    selectedAtUptimeMs advanced $baseline_sel -> $final_sel across /prev walk"
    else
        echo "  FAIL  selectedAtUptimeMs did not advance (baseline=$baseline_sel, final=$final_sel)" >&2
        rc=1
    fi
    return $rc
}

# POST a JSON body that the core's parser will reject. We expect 400
# and a JSON `{"error":"..."}` body. CRITICAL: the device must NOT
# reboot -- if it does, /healthcheck will hang for BOOT_WAIT_SECS.
scenario_post_rejects_malformed_json() {
    local rc=0
    local out code body
    out="$(curl -sS --max-time 5 -X POST \
        --data-binary '{ this is not json' \
        -H "Content-Type: application/json" \
        -o /tmp/e2e_post_malformed.body \
        -w '%{http_code}' \
        "$BASE_URL/consoles.json")" || { echo "  FAIL  curl failed" >&2; return 1; }
    code="$out"
    assert_eq "400" "$code" "HTTP status for malformed body" || rc=1
    body="$(cat /tmp/e2e_post_malformed.body 2>/dev/null || true)"
    assert_in '"error"' "$body" "response body is JSON error shape" || rc=1
    assert_in 'JSON parse error' "$body" "parser error string surfaced" || rc=1
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
    local out code body
    out="$(curl -sS --max-time 5 -X POST \
        --data "$body_out" \
        -H "Content-Type: application/json" \
        -o /tmp/e2e_post_unknown.body \
        -w '%{http_code}' \
        "$BASE_URL/consoles.json")" || { echo "  FAIL  curl failed" >&2; return 1; }
    code="$out"
    assert_eq "400" "$code" "HTTP status for unknown-tvInput body" || rc=1
    body="$(cat /tmp/e2e_post_unknown.body 2>/dev/null || true)"
    assert_in '"error"' "$body" "response body is JSON error shape" || rc=1
    assert_in 'unknown tvInput' "$body" "core error string surfaced" || rc=1
    local hc
    hc="$(curl -sS --max-time 3 "$BASE_URL/healthcheck" 2>/dev/null || echo DOWN)"
    assert_eq "OK" "$(echo "$hc" | tr -d '\n')" "device still online after rejected POST" || rc=1
    return $rc
}

# POST a config that is syntactically valid but contains NO consoles
# (empty consoles array). Three independent assertions, all backed
# by device state:
#   - GET /consoles.json -> 204 (no body); the empty config WAS
#     written (no missing-file fallback to PROGMEM).
#   - GET /next -> 409 with "no consoles configured" (the new
#     empty-vector guard worked, no UB crash).
#   - GET /state.json -> total == 0 (the device booted with zero
#     consoles, not a stale count from the previous config).
scenario_post_empty_config() {
    local rc=0
    local config_file="$CONFIG_DIR/empty.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    echo "  posting $config_file -> POST /consoles.json"
    if ! http_post_file /consoles.json "$config_file" >/dev/null; then
        echo "  FAIL  POST /consoles.json (empty config) did not return 2xx" >&2
        return 1
    fi
    echo "  POST OK, waiting for reboot..."
    if ! wait_for_device; then
        return 1
    fi
    # 1. GET /consoles.json -> 204, no body.
    local code body
    read -r code body < <(http_get_code /consoles.json)
    assert_eq "204" "$code" "GET /consoles.json after POST empty" || rc=1
    if [ -n "$body" ]; then
        echo "  FAIL  GET /consoles.json 204 response had body: '$body'" >&2
        rc=1
    else
        echo "  ok    GET /consoles.json 204 response had empty body"
    fi
    # 2. GET /next -> 409 with the typed error. The old code would
    #    UB on CurrentConsole() with an empty vector; the new
    #    guard returns 409 instead.
    read -r code body < <(http_get_code /next)
    assert_eq "409" "$code" "GET /next with empty consoles" || rc=1
    assert_in "no consoles configured" "$body" "/next 409 body has typed error" || rc=1
    # 3. GET /state.json -> total == 0.
    wait_for_state .total 0 5 || rc=1
    return $rc
}

# POST -> read-back -> walk /next -> walk /prev -> end on baseline.
# The "everything works together" scenario. Posts example1.json,
# verifies the byte-identical round-trip via post_and_verify_disk,
# then exercises the full cycle in both directions.
scenario_post_then_read_then_cycle() {
    local rc=0
    local config_file="$CONFIG_DIR/example1.json"
    if [ ! -f "$config_file" ]; then
        echo "  FAIL  config file missing: $config_file" >&2
        return 1
    fi
    post_and_verify_disk "$config_file" || { rc=1; return $rc; }
    wait_for_state .total 3 5 || rc=1
    local forward_steps=(1 2 3)
    local forward_names=(
        "Super Nintendo Entertainment System"
        "Sega Genesis"
        "Nintendo Entertainment System"
    )
    for ((i=0; i<${#forward_steps[@]}; i++)); do
        local exp_idx="${forward_steps[$i]}"
        local exp_name="${forward_names[$i]}"
        http_get /next >/dev/null || { echo "  FAIL  forward step $((i+1)) /next" >&2; rc=1; return $rc; }
        wait_for_state ".index" "$exp_idx" 3 || rc=1
        wait_for_state ".name" "\"$exp_name\"" 3 || rc=1
    done
    local backward_steps=(2 1 0)
    local backward_names=(
        "Sega Genesis"
        "Super Nintendo Entertainment System"
        "Nintendo Entertainment System"
    )
    for ((i=0; i<${#backward_steps[@]}; i++)); do
        local exp_idx="${backward_steps[$i]}"
        local exp_name="${backward_names[$i]}"
        http_get /prev >/dev/null || { echo "  FAIL  backward step $((i+1)) /prev" >&2; rc=1; return $rc; }
        wait_for_state ".index" "$exp_idx" 3 || rc=1
        wait_for_state ".name" "\"$exp_name\"" 3 || rc=1
    done
    wait_for_state .index 0 3 || rc=1
    wait_for_state .name '"Nintendo Entertainment System"' 3 || rc=1
    wait_for_state .total 3 3 || rc=1
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
    state-json-shape
    post-then-read-back
    cycle-three-consoles
    cycle-three-consoles-prev
    cycle-four-consoles
    post-rejects-malformed-json
    post-rejects-unknown-tvinput
    post-empty-config
    post-then-read-then-cycle
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
