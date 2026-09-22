# Handoff: TDD e2e readback for `/consoles.json`

> Read this first, then [2026-09-22_tdd-e2e-readback.md](2026-09-22_tdd-e2e-readback.md).
> Total read time: ~5 min.

## What you're picking up

A Raspberry Pi Pico 2 W (RP2350) firmware at `/Users/jasoncopp/Source/GitHub/RetroRoom-Controller/`
has a working POST `/consoles.json` endpoint that validates and persists console configs to
LittleFS, plus a bash e2e harness at `agent-script/e2e-consoles-json.sh`. The user wants
the harness to be **TDD-driven**: every assertion must read back the device's actual state,
not trust response codes. To make that possible, the firmware needs a `GET /consoles.json`
endpoint and richer `GET /state.json` output.

The previous agent drafted a plan, ran a red-agent critique (4 blockers, several majors),
updated the plan, and is handing off here. The harness rewrite and firmware changes have
**not** been started — that's your job.

## Repo conventions (read or you'll trip)

1. **Build entrypoint**: `./agent-script/pio-upload-monitor.sh`. **Never call raw `pio run`.**
   - Default: 25s monitor window. Use `--no-upload -t 5` for compile-only.
   - Auto-detects port at `/dev/cu.usbmodem*`.
   - The current host is macOS; port is `/dev/cu.usbmodem11101`.
2. **Only target `[env:pico2w]`** — `pico_base` is out of scope. `platformio.ini` still
   declares it but you don't touch it.
3. **AGENT.md §4** allows flash + reboot when the user grants explicit permission. The
   current task has that permission ("go forth and make it awesome!" + amendment task).
4. **HTML pages** live in `src/html/` via `R""""(...)` includes into `src/html.h`. Not
   relevant for this task (no new HTML).
5. **Functional core in `lib/ConsoleConfig/`** is pure C++ with no Arduino/LittleFS headers.
   Unit-tested via `pio test -e test_native` (24/24 currently passing). Don't add Arduino
   code there.
6. **Imperative shell in `src/consoleconfig_store.{h,cpp}`** wraps LittleFS I/O. Already
   exposes `loadLiveConsoleConfig(std::string& out)` returning raw bytes — reuse it.

## Red-agent findings (don't re-litigate)

The previous draft's assumptions were stress-tested. The revised plan reflects the findings.
Don't re-open these unless you find new evidence:

1. **`currentConsoleIndex` does NOT survive `rp2040.restart()`** — RP2350's `crt0` zeroes
   `.bss` on every boot (warm or cold). Harness hardcodes `baseline_idx=0`.
2. **`json_minify` doesn't exist** — drop it. The firmware never re-serializes on GET, so
   no normalization is needed on the harness side either. Use `cmp -s` for byte compare.
3. **ArduinoJson v7 `serializeJson` is NOT byte-stable** — minified, `\uXXXX` for non-ASCII,
   no hex, numeric reformat. **Decision (final): keep raw-bytes GET. Do not re-serialize.**
4. **GET + POST on same path needs method guards** in esp32async/ESPAsyncWebServer v3.12.1.
   `onBody` middleware fires for any request with `Content-Length`, including GET-with-zero.
5. **Tri-state live-load** (missing / unreadable / corrupt). GET returns 200 / 204 / 500.
6. **`CurrentConsole()` UB on empty vector** — handlers must check `consoles.empty()`.
7. **`currentConsoleSelectedAtMs` ordering contract** — at-the-moment-of-decision, before
   `lightSingle`/`selectConsole`/broadcast. Document the contract in a comment.

## Current device state (as of 2026-09-22)

- Flashed firmware: latest pico2w build with POST `/consoles.json`, GET/POST `/next` `/prev`,
  WS `next`/`prev` commands. **Missing**: GET `/consoles.json`, enriched `/state.json`,
  `selectedAtUptimeMs`.
- IP: `192.168.1.100` (STA mode, on user's network).
- LittleFS state: previously POSTed `empty.json`; may contain whatever the last test left.
- A stale `pio device monitor` is running in another VS Code terminal (pid 87882, ttys005).
  **Kill it before flashing** — the picotool upload will fail if a monitor has the port open.
  ```bash
  kill 87882
  ```

## Build + verify entry points

```bash
# Compile only (fast, no flash)
./agent-script/pio-upload-monitor.sh -e pico2w --no-upload -t 5

# Flash + 25s monitor
./agent-script/pio-upload-monitor.sh -e pico2w -t 25

# Host-side unit tests (must stay 24/24)
pio test -e test_native

# E2E harness
./agent-script/e2e-consoles-json.sh --host 192.168.1.100 --all
./agent-script/e2e-consoles-json.sh --host 192.168.1.100 --scenario <name>

# Manual sanity (after flash)
curl -sS http://192.168.1.100/consoles.json | jq .
curl -sS http://192.168.1.100/state.json | jq .
curl -sS http://192.168.1.100/wifi | jq .
```

## Implementation checklist (in order)

### A. Firmware (`src/network.cpp`, `src/network.h`, `src/consoles.cpp`, `src/consoles.h`)

1. **Add `onConsolesJsonGet`** handler — call `retroroom_store::loadLiveConsoleConfig`.
   - 200 + raw bytes if file present.
   - 204 if `loadLiveConsoleConfig` returned `false` (file missing).
   - 500 with `{"error":"fs mount failed"}` if `ensureMounted()` failed.
   - Send via `req->send(code, "application/json", fs_json.c_str(), fs_json.size())`.
2. **Register** `server.on("/consoles.json", HTTP_GET, onConsolesJsonGet);` in
   `startStaServer()` near the POST registration (~line 696).
3. **Method guard** at top of `consolesJsonOnBody` and `consolesJsonOnRequest`:
   `if (req->method() != HTTP_POST) return;`
4. **Expand `onStateJson`** — add `total`, `uptimeMs`, `selectedAtUptimeMs`.
   Bump `StaticJsonDocument<256>` → `<384>`.
5. **Add global** `uint32_t currentConsoleSelectedAtMs = 0;` in `src/consoles.cpp`,
   `extern` in `src/consoles.h`.
6. **Assign** `currentConsoleSelectedAtMs = millis();` in `advanceConsole()` /
   `rewindConsole()` **immediately after `wraparoundNext()`**, before any side effects.
   Comment the contract.
7. **Guard `onConsoleNext` / `onConsolePrev`** against empty vector:
   ```cpp
   if (consoles.empty()) {
       req->send(409, "application/json",
                 "{\"error\":\"no consoles configured\"}\n");
       return;
   }
   ```
8. **Guard WS `next` / `prev` handlers** — broadcast `"console:noop"` if empty.
9. **Do NOT add the boot-time `clampIndex` call** — it's a provable no-op given `.bss`
   zeroing. Skip it.
10. **Update `src/network.h` endpoint docstring** — list `GET /consoles.json` (200/204/500)
    and document the new `/state.json` fields.

### B. Build + flash

11. Compile via wrapper `--no-upload -t 5`. Capture any new warnings; investigate.
12. Kill stale monitor (`kill 87882`).
13. Flash + monitor via wrapper `-t 25`. Confirm `Setup Complete.` and the new
    config-source breadcrumb.

### C. Harness rewrite (`agent-script/e2e-consoles-json.sh`)

14. Add `wait_for_state <jq-filter> <expected> <timeout_s>` — polls `/state.json` every
    500ms, asserts `jq -r "$1" <<<"$body"` equals `$2`. Returns 0 on success.
15. Add `post_and_verify_disk <file>` — POST + wait for `/healthcheck` (assert
    `mode==sta` after reboot) + `GET /consoles.json` + `cmp -s` against the POSTed file.
    No minifier.
16. Bump `BOOT_WAIT_SECS` default to 25.
17. Simplify cycle scenarios — replace dynamic `baseline_idx` capture with
    `baseline_idx=0`. Use `post_and_verify_disk` up front.
18. Per-step `wait_for_state .index` / `wait_for_state .name` after each `/next` / `/prev`.
19. Rewrite `scenario_post_empty_config` — POST empty, wait, expect 204 from
    `GET /consoles.json`, expect 409 from `GET /next`, expect `.total == 0` from
    `/state.json`.
20. Add `scenario_post_then_read_then_cycle` — combined smoke: POST + verify + walk
    `/next` 3× + walk `/prev` 3× with per-step state verification.

### D. Verify + docs

21. Run the full suite (`./agent-script/e2e-consoles-json.sh --host 192.168.1.100 --all`).
    All scenarios must PASS.
22. Update `todo/open/2026-09-22_consoles-json-upload-endpoint.md` — mark read-back path
    as done, link to this plan + handoff.
23. (Optional, helpful) Append a `LOG.md` MADR entry summarizing the GET endpoint +
    TDD harness addition. Keep it short.

## Files you'll touch

| File | Change |
|------|--------|
| [src/network.cpp](src/network.cpp) | Add `onConsolesJsonGet`, expand `onStateJson`, method guards on POST body middleware, register route, guards on `/next` `/prev` |
| [src/network.h](src/network.h) | Docstring update for `/consoles.json` and `/state.json` |
| [src/consoles.cpp](src/consoles.cpp) | New global, assign in advance/rewind (do NOT add clampIndex) |
| [src/consoles.h](src/consoles.h) | Declare new global |
| [agent-script/e2e-consoles-json.sh](agent-script/e2e-consoles-json.sh) | Full rewrite of helpers + scenarios |
| [todo/open/2026-09-22_consoles-json-upload-endpoint.md](todo/open/2026-09-22_consoles-json-upload-endpoint.md) | Status update |

## Out of scope (don't do)

- Migrating `StaticJsonDocument` → `JsonDocument` (deprecation warnings already filtered).
- WS-driven e2e scenarios (needs `websocat` dependency).
- Persisting `currentConsoleIndex` across boots (explicit deferred item — note in TODO).
- Exposing `bak1` / `bak2` over HTTP.
- Re-serializing GET `/consoles.json` through ArduinoJson (explicitly rejected — see red-agent finding #3).
