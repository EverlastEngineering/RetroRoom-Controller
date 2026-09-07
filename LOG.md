# RetroRoom Decision Log

This file records architectural decisions and notable changes to RetroRoom-Controller. Entries follow [MADR](https://adr.github.io/madr/) format — see [AGENT.md](AGENT.md) for the template and policy.

Entries are added to the top of this file by `agent-script/log-add.mjs`. Use `agent-script/log-read.mjs` to view recent entries.

<!-- insert-below -->
## 2026-09-07T14:30:31.977Z — Adopt MADR-format decision log and agent-script tooling

**Context:** Decisions were being made in chat and lost across sessions. No persistent record of why choices were made or what tradeoffs were accepted.

**Decision:** Adopt MADR (Markdown ADR) format for LOG.md, three-file layout (AGENT/TODO/LOG) at repo root, and Node.js scripts in agent-script/ for deterministic log management. Session branches named session/<topic-slug> off main. UTC timestamps. Scripts resolve paths via import.meta.url so cwd does not matter.

**Consequences:** All future significant decisions get logged. Scripts handle formatting consistency. May feel heavy for trivial changes — use judgment. package.json and .nvmrc scoped to agent-script/ to keep PlatformIO root clean.

---


---
