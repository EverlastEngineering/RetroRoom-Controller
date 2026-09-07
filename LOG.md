# RetroRoom Decision Log

This file records architectural decisions and notable changes to RetroRoom-Controller. Entries follow [MADR](https://adr.github.io/madr/) format — see [AGENT.md](AGENT.md) for the template and policy.

Entries are added to the top of this file by `agent-script/log-add.mjs`. Use `agent-script/log-read.mjs` to view recent entries.

<!-- insert-below -->
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
