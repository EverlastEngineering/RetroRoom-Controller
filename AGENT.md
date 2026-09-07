# AGENT.md

Working agreement for RetroRoom-Controller development. Applies to AI agents and human contributors equally.

## Policy

1. **Always log decisions and changes.** Before completing any non-trivial action, add an entry to `LOG.md` using the `log_add` MCP tool — **never** invoke `agent-script/log-add.mjs` from the terminal. Use the template below.
2. **Maintain a curated TODO.** Track actionable items in `TODO.md`. When an item is completed or rejected, log the disposition in `LOG.md`.
3. **Session branches.** New topic-scoped work happens on a `session/<topic-slug>` branch off `main`. Small, granular commits. Merge back into `main` locally; switch to a PR when collaborators join.
4. **Single source of truth for policy.** This file owns the working agreement. `LOG.md` and `TODO.md` reference back here.

## LOG.md Entry Template (MADR)

Entries are written in [MADR](https://adr.github.io/madr/) format. They are added to the top of `LOG.md` by the `log_add` MCP tool (which shells out to `agent-script/log-add.mjs` under the hood).

```markdown
## <ISO-8601 UTC timestamp> — <Short title>

**Context:** What situation prompted the decision.

**Decision:** What we chose to do.

**Consequences:** Tradeoffs, followups, or "revisit if X changes."

---
```

Keep entries short — 5 to 15 lines is typical. If an entry is growing past that, link to a longer doc instead.

## Scripts & MCP tools

Two MCP tools are registered via `.vscode/mcp.json` and are the **only** way the agent should read or write `LOG.md`:

- `log_add(title, context, decision, [consequences], [kind], [kind-specific args], [dryRun])` — append a MADR entry. Accepts kinds `decision` (default), `note`, `test`.
- `log_read(n?, since?, kind?)` — read recent entries; slice by `--n`, restrict by `--since` date, or filter by `--kind`.

The underlying CLI scripts (`agent-script/log-add.mjs`, `agent-script/log-read.mjs`) are an implementation detail of the MCP server. **Do not invoke them directly** from a terminal — formatting, validation, and kind classification all go through the MCP layer. `agent-script/README.md` documents the CLI flag reference for humans only.

## Pointers

- `LOG.md` — running record of decisions and changes.
- `TODO.md` — curated list of pending work.
- `agent-script/` — Node.js tooling for managing these docs.
