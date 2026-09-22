# Plan: TDD-driven end-to-end e2e for `/consoles.json`

> Status: revised after red-agent review. Ready to implement.

## TL;DR

The current `agent-script/e2e-consoles-json.sh` is too **trusting**: it POSTs
configs and confirms the device reboots + cycles, but never reads back what
landed on disk. To make the harness test-driven, the firmware gets:

- One new endpoint (`GET /consoles.json`) returning raw bytes or 204/500.
- A richer `GET /state.json` payload with `total`, `uptimeMs`, `selectedAtUptimeMs`.

The harness is rewritten around two new helpers — `wait_for_state` and
`post_and_verify_disk` — so every assertion is backed by reading the device's
actual state, not by trusting response codes.

## Red-agent blockers addressed in this revision

The red-agent review (run 2026-09-22) flipped four foundational assumptions in
the original draft:

1. **`currentConsoleIndex` does NOT survive `rp2040.restart()`.** RP2350's
   `crt0` zeroes `.bss` on every boot (warm or cold). The harness's
   `baseline_idx`-capture dance is unnecessary; the value is provably 0
   post-reboot. Either simplify the harness to hardcode `baseline_idx=0`, or
   actually persist the index (out of scope this turn).
2. **`json_minify` does NOT exist in the harness.** The original draft
   hallucinated it. Removed — the harness no longer needs a Python minifier
   because the firmware never re-serializes.
3. **ArduinoJson v7 `serializeJson` is NOT byte-stable.** Minified output,
   `\uXXXX` escapes non-ASCII by default, no hex support, numeric reformat.
   **Decision (final): keep raw-bytes GET. Do NOT re-serialize on the device.**
   Your "we'll likely run into nagging errors where that fails" intuition is
   right — the cleanest mitigation is to never touch ArduinoJson on the GET
   path. The firmware writes the POSTed payload verbatim via `f.print()`, and
   the new GET handler reads the same file via `loadLiveConsoleConfig` and
   ships it verbatim. Byte-identical round-trip is trivially achievable.
4. **GET + POST on the same path in esp32async/ESPAsyncWebServer v3.12.1** —
   routes are per-method handler instances, but the POST's `onBody` middleware
   fires for *any* request with a `Content-Length` header (including
   `Content-Length: 0` on a GET). Fix: add an explicit method guard at the top
   of `onBody` and `onRequest`.

## Major issues addressed

- **Tri-state live-load** (missing / unreadable / corrupt). The new GET
  endpoint surfaces this: `200` with `_status: "ok"` + raw bytes,
  `204` with `_status: "absent"`, or `500` with `{"error":"fs mount failed"}`.
- **`CurrentConsole()` UB on empty vector.** Calling `/next` after
  `post-empty-config` will hit `operator[]` on a 0-size vector. The HTTP
  handlers must check `consoles.empty()` and return `409 Conflict`; the WS
  handlers must broadcast `"console:noop"` so connected UIs don't drift.
- **`currentConsoleSelectedAtMs` ordering contract.** Picked: **at-the-
  moment-of-decision** — assign immediately after `wraparoundNext()` returns,
  before `lightSingle` / `selectConsole` / broadcast. Only contract the
  harness can rely on without time-of-flight latency.

## Minor issues addressed

- Drop the boot-time `clampIndex` call — provably a no-op given `.bss` zeroing.
- Bump `StaticJsonDocument<256>` → `<384>` in `onStateJson` for headroom.
- Drop the `uptimeMs`-non-decreasing sanity check (trivially true over 30s).
- Rename `selectedAtMs` → `selectedAtUptimeMs` so the "delta = time-selected"
  usage is unambiguous.
- Add a SoftAP-mode detection step in `wait_for_device`: after reboot,
  `GET /wifi`; if `mode=="ap"`, fail fast with a clear message.
- Update `src/network.h` docstring for new endpoints and fields.
- Add a "config source" breadcrumb log line in `startStaServer()` so the
  monitor and live operator can see `served by: LittleFS /consoles.json` vs
  `served by: PROGMEM default`.

## Steps

### Phase 1 — Firmware surface (`src/network.cpp`, `src/network.h`, `src/consoles.cpp`, `src/consoles.h`)

1. Add `onConsolesJsonGet` handler in `src/network.cpp`.
   - Call `retroroom_store::loadLiveConsoleConfig`.
   - **200** with `_status:"ok"` header line + raw bytes if file present.
     Actually: ship raw bytes only, no JSON wrapper. Status conveyed by HTTP.
   - **204** if `loadLiveConsoleConfig` returned `false` (file missing).
   - **500** with `{"error":"fs mount failed"}` if `ensureMounted()` failed.
   - Send via `req->send(code, "application/json", fs_json.c_str(), fs_json.size())`.
2. Register `server.on("/consoles.json", HTTP_GET, onConsolesJsonGet);` in
   `startStaServer()` adjacent to the POST registration (line ~696).
3. Add method guard at top of `consolesJsonOnBody`:
   `if (req->method() != HTTP_POST) return;`. Same for `consolesJsonOnRequest`.
4. Expand `onStateJson` to add:
   - `total` (`HowManyConsoles()`)
   - `uptimeMs` (`millis()`)
   - `selectedAtUptimeMs` (new global, see step 5)
   Bump `StaticJsonDocument<256>` → `<384>`.
5. Add `uint32_t currentConsoleSelectedAtMs = 0;` global in `src/consoles.cpp`,
   declared `extern` in `src/consoles.h`.
6. Assign `currentConsoleSelectedAtMs = millis();` in `advanceConsole()` and
   `rewindConsole()` **immediately after `wraparoundNext()` returns**, before
   `lightSingle` / `selectConsole` / WS broadcast. Comment the contract.
7. Guard `onConsoleNext` / `onConsolePrev`:
   ```cpp
   if (consoles.empty()) {
       req->send(409, "application/json",
                 "{\"error\":\"no consoles configured\"}\n");
       return;
   }
   ```
8. Guard WS `next` / `prev` handlers: if empty, broadcast `"console:noop"` and
   return without advancing.
9. Drop the proposed `clampIndex` boot-time call (no-op given `.bss` zeroing).
10. Update `src/network.h` endpoint docstring:
    - `GET /consoles.json` → 200/204/500
    - `GET /state.json` → document new fields

### Phase 2 — Functional core

No changes. The 24 existing host tests cover the parse/rotation math. The new
code is all shell-side I/O + JSON serialization, which is intentionally not
unit-tested on the host.

### Phase 3 — Build + flash

11. `./agent-script/pio-upload-monitor.sh -e pico2w --no-upload -t 5` — verify
    compile, capture any new warnings.
12. `./agent-script/pio-upload-monitor.sh -e pico2w -t 25` — flash + monitor.

### Phase 4 — Rewrite `agent-script/e2e-consoles-json.sh`

13. Add `wait_for_state <jq-filter> <expected> <timeout_s>` helper:
    - Polls `/state.json` every 500ms up to `timeout_s`.
    - Asserts `jq -r "$1" <<<"$body"` equals `$2`.
    - Returns 0 on success, 1 on timeout.
14. Add `post_and_verify_disk <file>` helper:
    - `POST /consoles.json` with the file.
    - Wait up to 25s for `/healthcheck` to answer.
    - `wait_for_device` should also assert `mode==sta` after reboot (fail fast
      if SoftAP).
    - `GET /consoles.json` and byte-compare against `$(cat <file>)` via
      `cmp -s` (no minification — the firmware never re-serializes).
    - Print diff (via `diff`) on mismatch.
15. Drop `json_minify` entirely. No Python in this path.
16. Bump `BOOT_WAIT_SECS` default to 25 (was 15).
17. Simplify cycle scenarios:
    - **Replace** the dynamic `baseline_idx` capture with hardcoded
      `baseline_idx=0`. After `rp2040.restart()` it is provably 0.
    - Use `post_and_verify_disk` up front for the cycle scenarios.
    - Per-step `wait_for_state .index` / `wait_for_state .name` after each
      `/next` / `/prev`.
18. Rewrite `scenario_post_empty_config`:
    - POST `empty.json`.
    - Wait for `/healthcheck` + assert `mode==sta`.
    - `GET /consoles.json` → expect HTTP 204 (no body) and no reboot-crash.
    - `GET /next` → expect HTTP 409 with `no consoles configured`.
    - `GET /state.json` → expect `.total == 0`.
19. Add `scenario_post_then_read_then_cycle` (new combined smoke):
    - `post_and_verify_disk example1.json`.
    - Walk `/next` 3 times with per-step `wait_for_state`.
    - Walk `/prev` 3 times with per-step `wait_for_state`.
    - Verify wrap-back to baseline.

### Phase 5 — Live verification + docs

20. Run the full suite: `./agent-script/e2e-consoles-json.sh --host 192.168.1.100 --all`.
21. Update `todo/open/2026-09-22_consoles-json-upload-endpoint.md` to mark the
    read-back path as done and link to this plan.

## Relevant files

- [src/network.cpp](src/network.cpp) — `onStateJson` (~474–484), `onConsoleNext`/`Prev` (~453–468),
  `startStaServer()` route table (~530–790), POST `/consoles.json` (~558–700),
  WS event handler (~792–858), reboot arms (~980/994).
- [src/network.h](src/network.h) — endpoint docstring block.
- [src/consoles.cpp](src/consoles.cpp) — `currentConsoleIndex` global, `consoleDefinitions()`,
  `advanceConsole()` (line ~145), `rewindConsole()` (line ~181).
- [src/consoles.h](src/consoles.h) — declarations.
- [src/consoleconfig_store.cpp](src/consoleconfig_store.cpp) — `loadLiveConsoleConfig()` already
  returns the bytes; no API change.
- [agent-script/e2e-consoles-json.sh](agent-script/e2e-consoles-json.sh) — full rewrite of helpers + scenarios.
- [todo/open/2026-09-22_consoles-json-upload-endpoint.md](todo/open/2026-09-22_consoles-json-upload-endpoint.md) — status update.
- `plans/2026-09-22_tdd-e2e-readback.md` (this file).

## Verification

1. **Unit tests**: `pio test -e test_native` → 24/24 pass (no functional-core change).
2. **Compile**: wrapper with `--no-upload -t 5` → exit 0.
3. **Flash + boot**: wrapper with `-t 25` → exit 0, monitor shows `Setup Complete.`
   plus the new config-source breadcrumb.
4. **Manual sanity**:
   - `curl -sS http://192.168.1.100/consoles.json | jq .` returns the live config (or empty on 204).
   - `curl -sS http://192.168.1.100/state.json | jq .` includes `total`, `uptimeMs`, `selectedAtUptimeMs`.
   - `curl -sS -X GET http://192.168.1.100/consoles.json` returns the same bytes as
     `curl -sS -X POST --data-binary @example1.json http://192.168.1.100/consoles.json | jq -Rs '.'`
     (after reboot, byte-identical).
5. **E2E suite**: `./agent-script/e2e-consoles-json.sh --host 192.168.1.100 --all` → all 8 scenarios PASS.

## Decisions

- **GET `/consoles.json` returns raw bytes** (200 + `application/json`), 204 if missing,
  500 if FS mount failed. No ArduinoJson involvement on the read path — guarantees
  byte-identical round-trip.
- **State JSON gets `total`, `uptimeMs`, `selectedAtUptimeMs`** in one endpoint rather
  than splitting into a separate `/current.json`. Fewer routes, one round-trip per assertion.
- **`currentConsoleSelectedAtMs` is RAM-only** and resets to 0 on every boot.
  Harness asserts it *changes* when `/next` fires, not its absolute value.
- **`currentConsoleIndex` does NOT survive `rp2040.restart()`** — `.bss` is zeroed by
  `crt0` on every boot. The harness hardcodes `baseline_idx=0`. (Persisting the index
  across boots is a deferred follow-up — see TODO entry.)
- **`BOOT_WAIT_SECS=25`** (was 15). User-confirmed reboot + STA reconnect ≈ 10s.
- **`baseline_idx=0` is hardcoded** in cycle scenarios. The dynamic capture dance
  existed to defend against an assumption (index survival) that is provably false.

## Scope

**In scope:** GET `/consoles.json`, enriched `/state.json`, empty-vector guards on
`/next` / `/prev` + WS, TDD harness rewrite, live verification, plan/README updates.

**Out of scope (deliberately):**
- Migrating `StaticJsonDocument` → `JsonDocument` (deprecation warnings already
  filtered by the build wrapper).
- WS-driven e2e scenarios (would need `websocat` dependency, deferrable).
- Persisting `currentConsoleIndex` across boots (tracked in TODO).
- Exposing `bak1` / `bak2` over HTTP (operator-debug only).
- Re-serializing GET `/consoles.json` through ArduinoJson (explicitly rejected —
  would diverge from raw bytes via `\uXXXX` escapes and numeric reformats).
