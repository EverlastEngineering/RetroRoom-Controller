# AGENT.md

Working agreement for RetroRoom-Controller development. Applies to AI agents and human contributors equally.

## Policy

1. **Always log decisions and changes.** Before completing any non-trivial action, add an entry to `LOG.md` using `agent-script/log-add.mjs`. Use the template below.
2. **Maintain a curated TODO.** Track actionable items in `TODO.md`. When an item is completed or rejected, log the disposition in `LOG.md`.
3. **Session branches.** New topic-scoped work happens on a `session/<topic-slug>` branch off `main`. Small, granular commits. Merge back into `main` locally; switch to a PR when collaborators join.
4. **Single source of truth for policy.** This file owns the working agreement. `LOG.md` and `TODO.md` reference back here.

## LOG.md Entry Template (MADR)

Entries are written in [MADR](https://adr.github.io/madr/) format. They are added to the top of `LOG.md` by `agent-script/log-add.mjs`.

```markdown
## <ISO-8601 UTC timestamp> — <Short title>

**Context:** What situation prompted the decision.

**Decision:** What we chose to do.

**Consequences:** Tradeoffs, followups, or "revisit if X changes."

---
```

Keep entries short — 5 to 15 lines is typical. If an entry is growing past that, link to a longer doc instead.

## Scripts

See `agent-script/README.md` for usage of `log-add.mjs` and `log-read.mjs`.

## Pointers

- `LOG.md` — running record of decisions and changes.
- `TODO.md` — curated list of pending work.
- `agent-script/` — Node.js tooling for managing these docs.
