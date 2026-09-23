# POST /consoles.json — upload + persist console config

## Status

POST path landed 2026-09-22 (commit `3903164`). Read-back path
landed 2026-09-22 (commits `fc3b293`, `512397c`, `25e9710`).
TDD harness rewrite landed 2026-09-22 (commit `25e9710`).
Awaiting live verification on Pico 2 W.

The TDD read-back work is documented in
[plans/2026-09-22_tdd-e2e-readback.md](../../plans/2026-09-22_tdd-e2e-readback.md)
and the handoff [plans/2026-09-22_tdd-e2e-readback-HANDOFF.md](../../plans/2026-09-22_tdd-e2e-readback-HANDOFF.md).

## What landed (POST — 2026-09-22, commit `3903164`)

## Goal

Eventually the operator-facing flow for editing the console config is
"edit JSON in the browser, hit Save, watch the device reconfigure
itself." The long-term landing site is a `/config` form; for now an
external service assembles the JSON and POSTs it to this endpoint.

## What landed (read-back — 2026-09-22, commits `fc3b293` / `512397c` / `25e9710`)

- `GET /consoles.json` — serves raw bytes from LittleFS (200 / 204 /
  500). No ArduinoJson on the read path so the round-trip is
  byte-identical to what POST /consoles.json wrote.
- `POST /consoles.json` — method guards on the onBody middleware so a
  GET with `Content-Length: 0` no longer writes zero bytes into
  `_tempObject` and 500s.
- `GET /state.json` — now includes `total`, `uptimeMs`,
  `selectedAtUptimeMs`. `StaticJsonDocument<256>` → `<384>` for
  headroom. Bug fix in `onStateJson` (commit `512397c`) adds an
  empty-vector guard so `name=""` (and no UB) when no consoles are
  configured.
- `GET /next`, `GET /prev` — 409 with typed JSON error if
  `consoles.empty()`. WS `next` / `prev` commands broadcast
  `console:noop` instead of advancing into UB.
- `src/consoles.{cpp,h}` — new `uint32_t currentConsoleSelectedAtMs`
  global, stamped in `advanceConsole()` / `rewindConsole()`
  at-the-moment-of-decision. RAM-only; `.bss` zeroing on every boot
  is documented as the lifetime contract.
- `agent-script/e2e-consoles-json.sh` — full TDD rewrite. New
  primitives `wait_for_state` (poll /state.json until jq-filter
  matches) and `post_and_verify_disk` (POST + reboot + GET +
  byte-compare). 10 scenarios: healthcheck, state-json-shape,
  post-then-read-back, cycle-three-consoles,
  cycle-three-consoles-prev, cycle-four-consoles,
  post-rejects-malformed-json, post-rejects-unknown-tvinput,
  post-empty-config, post-then-read-then-cycle.

## What landed (POST — 2026-09-22, commit `3903164`)

- `POST /consoles.json` accepts a JSON body (Content-Type ignored, raw
  bytes collected into a `_tempObject` buffer by an `onBody`
  middleware — same pattern `AsyncJson.cpp` uses internally).
  - 8 KB body cap (Content-Length checked up front, matched on the
    read side in `consoleconfig_store.cpp`).
  - 400 on empty body with a JSON error payload.
  - 413 on bodies > 8 KB.
- The body is fed byte-verbatim to the existing functional-core
  validator (`retroroom_core::validateConsoleConfigJson`). No
  re-serialization through ArduinoJson → bytes on disk == bytes from
  the wire (whitespace, ordering, indentation all preserved).
- On validation failure: 400 + the core's typed error string in
  `{"error":"..."}` JSON.
- On validation success: the new payload is written to LittleFS with
  two rolling backups via `retroroom_store::saveConsoleConfigWithBackups`
  (which delegates rotation to the functional core's
  `rotateBackupBlobs`, so the policy is unit-testable on the host).
- On FS write failure: 500 with a generic message; the FS may be in a
  partial-write state but the boot path falls back through
  bak1 → bak2 → PROGMEM so the device still boots.
- On success: 200 + `{"status":"saved","consoles":N,"irCodes":N,...}`
  then schedules a reboot via `g_pendingConsoleConfigRebootAt` (same
  pattern as `/factory-reset`). 1.5 s delay so the TCP send buffer
  drains first.

## Boot path

`consoleDefinitions()` (`src/consoles.cpp`) now prefers
`/consoles.json` from LittleFS if it exists. Falls back to the
embedded PROGMEM `CONFIG_JSON` literal if:

- the FS isn't mounted (board without a LittleFS partition),
- `/consoles.json` is missing (factory-fresh device),
- or `/consoles.json` fails to parse (corrupt or partial write).

The boot-time log line now distinguishes the two sources:
`Loaded N consoles from LittleFS /consoles.json: [...]` vs
`Loaded N consoles from PROGMEM default (CONFIG_JSON): [...]`.

## Files touched

- `lib/ConsoleConfig/src/ConsoleConfig.h` — added
  `validateConsoleConfigJson`, `BackupRotation`, `rotateBackupBlobs`.
- `lib/ConsoleConfig/src/ConsoleConfig.cpp` — implementations.
- `test/test_console_store/test_console_store.cpp` — 11 new Unity
  tests (validation + rotation policy).
- `src/consoleconfig_store.h` — namespace `retroroom_store` shell API.
- `src/consoleconfig_store.cpp` — LittleFS read/write glue.
- `src/network.cpp` — `POST /consoles.json` handler + reboot flag +
  `network_loop()` arm + `GET /next` / `GET /prev` cycle endpoints +
  `next` / `prev` WS commands.
- `src/network.h` — endpoint docstring updated.
- `src/consoles.cpp` — boot path now prefers FS config; new
  `rewindConsole()` wrapper for the `-1` direction (math still lives
  in the functional core's `wraparoundNext`).
- `src/consoles.h` — declaration for `rewindConsole()`.
- `agent-script/pio-upload-monitor.sh` — added `--no-upload` flag
  (build-only check, AGENT.md §4 compliant), restored the leading
  shebang that an external tool had truncated, and strip the
  ArduinoJson `StaticJsonDocument` deprecation warnings so build
  output stays readable.
- `agent-script/e2e-consoles-json.sh` (new) — bash e2e harness
  driving the live device over curl. Six scenarios (healthcheck,
  post-rejects-malformed-json, post-rejects-unknown-tvinput,
  cycle-three-consoles, cycle-four-consoles, post-empty-config).

## Test results

- `pio test -d . -e test_native` → 35/35 pass (24 pre-existing +
  11 new).
- `pio run -e pico2w` green with the new flag (`--no-upload`).
- Live runs against the device at `192.168.1.100` (per the user's
  terminal selection 2026-09-22):
  - `scenario: healthcheck` → PASS.
  - `scenario: post-rejects-malformed-json` → PASS — confirms 400 +
    JSON error body + no reboot.

## Follow-ups

- [ ] Operator UI: the `/config` form that assembles the JSON and POSTs
      it. Today the operator (or an external service) builds the body
      manually and POSTs raw. The form lives in `src/html/` per the
      conventions memory.
- [ ] StaticJsonDocument → JsonDocument migration in
      `src/network.cpp` (3 sites). Pre-existing deprecation warnings;
      separate task from this feature. Worth doing before
      ArduinoJson 8 ships and removes the alias.
- [ ] Run the rewritten harness end-to-end against the live device
      (`./agent-script/e2e-consoles-json.sh --host 192.168.1.100
      --all`). All 10 scenarios should pass on the firmware from
      `512397c`. Deferred to when the operator can power-cycle /
      confirm the device is stable after the CYW43 wedge incident
      during this session.
- [ ] Add a `cycle` scenario to e2e-consoles-json.sh that exercises
      the WebSocket `next` / `prev` commands over `websocat` (or a
      Python fallback). Not gated on anything; skipped here because
      adding websocat to dev environments is out of scope.
- [ ] Persist `currentConsoleIndex` to LittleFS so the device boots
      back into the last-selected console after the operator power-
      cycles. Pairs naturally with the FS-config boot path but is its
      own item.
- [x] Decide what `currentConsoleIndex` should be on an empty
      config. Fixed in `512397c` (onStateJson empty-vector guard) +
      `fc3b293` (onConsoleNext/Prev return 409 with typed error).
      Remaining: WS `next`/`prev` should broadcast `console:noop` for
      empty consoles (added in `fc3b293`); the WS side hasn't been
      covered by an e2e scenario yet (see WS follow-up above).
