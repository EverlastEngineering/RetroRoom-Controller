# RetroRoom Decision Log

This file records architectural decisions and notable changes to RetroRoom-Controller. Entries follow [MADR](https://adr.github.io/madr/) format — see [AGENT.md](AGENT.md) for the template and policy.

Entries are added to the top of this file by the `log_add` MCP tool. Use the `log_read` MCP tool to view recent entries.

<!-- insert-below -->
## 2026-09-07T14:55:51.540Z — MCP tool discovery round-trip works from a fresh chat

**Context:** Verifying that the `log_add` MCP tool is discoverable and invokable from a brand-new chat session without any prior tool priming or conversation history about it.

**Decision:** Successfully invoked the `log_add` MCP tool directly from a fresh chat, confirming the MCP tool-discovery round-trip works end-to-end (registration → tool list → invocation → LOG.md write).

**Consequences:** Reuse confirmed — future sessions can call `log_add` without re-priming. Revisit if the tool disappears from the discovery list or if LOG.md writes silently fail.

---

## 2026-09-07T14:48:31.265Z — Expose agent-scripts as MCP stdio server for Copilot Chat

**Context:** log-add.mjs and log-read.mjs were CLI-only and required manual invocation from a terminal. Copilot Chat could not call them directly, so log additions and reads during agent workflows had to fall back to copy-paste or terminal round-trips. Goal: surface these scripts as native Copilot tools via the Model Context Protocol with minimal added surface area.

**Decision:** Add a stdio MCP server (mcp-server.mjs) in agent-script/ that wraps the existing CLIs via @modelcontextprotocol/sdk + zod, spawn them as child processes from each tool handler, and register the server in .vscode/mcp.json. Extend log-add/log-read schema with --kind (decision|note|test) so operational entries (smoke tests, observations) fit alongside decisions without forcing them into the decision frame. Mirror the new flags in the MCP tool input schema.

**Consequences:** agent-script/package.json gains two runtime deps (@modelcontextprotocol/sdk, zod). Future tools only need new server.tool() blocks; the CLI scripts keep working standalone. log-add.mjs becomes kind-aware; existing decision entries remain valid because the default kind is 'decision'. Per the user's preference that 'more in log is better than less', the smoke-test entry previously written was re-classified as a kind=test entry instead of deleted.

---

## 2026-09-07T14:44:09.228Z — Smoke-test MCP wiring post-IDC-enable

**Kind:** test
**Run:** Issue MCP initialize + tools/list + tools/call(log_read, n=1) + tools/call(log_add, no dry-run flag) against the stdio server after enabling it in the VS Code tool picker.

**Result:** initialize returned protocol `2024-11-05` with `tools` capability; tools/list advertised both `log_add` and `log_read` with their input schemas; both tools/call invocations returned content correctly. This entry was the run artifact — the log_add call landed a real entry rather than a dry-run because `--dry-run` was omitted from the test JSON.

**Consequences:** Re-classified post-hoc from a decision to a `kind=test` entry to fit the extended MADR schema. Wording cleaned up to match the test/run/result frame.

---

## 2026-09-07T14:39:24.650Z — Commit partial JSON-rewrite WIP from prior session

**Context:** Uncommitted changes on session/agent-infrastructure refactor console configuration from hardcoded addConsole(...) calls in main.cpp to a JSON config parsed via ArduinoJson. Work was abandoned mid-rewrite. consoleDefinitions_init() in src/consoles.cpp never calls addConsole() and the JSON content is duplicated as a C++ string literal rather than loaded from src/json/base.json. Result: consoles vector is empty at boot, selectStack_init clocks zero times, currentConsole() is UB. TODO.md Phase 1 items 1 and 2 already document this exact state.

**Decision:** Commit the work as-is on session/agent-infrastructure with a WIP prefix and explicit do-not-flash warning in the commit body, rather than discarding or attempting to complete the rewrite. Per AGENT.md policy we do not delete items silently; TODO already names the blockers.

**Consequences:** HEAD on session/agent-infrastructure will not produce a functional binary. Do not flash from this commit until TODO #1 (wire JSON loading or restore hardcoded addConsole calls) and TODO #2 (fix selectStack_init) are resolved. Source of truth for what is broken is now TODO.md, not the diff.

---

## 2026-09-07T14:30:31.977Z — Adopt MADR-format decision log and agent-script tooling

**Context:** Decisions were being made in chat and lost across sessions. No persistent record of why choices were made or what tradeoffs were accepted.

**Decision:** Adopt MADR (Markdown ADR) format for LOG.md, three-file layout (AGENT/TODO/LOG) at repo root, and Node.js scripts in agent-script/ for deterministic log management. Session branches named session/<topic-slug> off main. UTC timestamps. Scripts resolve paths via import.meta.url so cwd does not matter.

**Consequences:** All future significant decisions get logged. Scripts handle formatting consistency. May feel heavy for trivial changes — use judgment. package.json and .nvmrc scoped to agent-script/ to keep PlatformIO root clean.

---


---
