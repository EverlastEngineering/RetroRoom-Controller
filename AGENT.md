# AGENT.md

Working agreement for RetroRoom-Controller development. Applies to AI agents and human contributors equally.

## Policy

1. **Always log decisions and changes.** Before completing any non-trivial action, add an entry to `LOG.md` using the `log_add` MCP tool — **never** invoke `agent-script/log-add.mjs` from the terminal. Use the template below.
2. **Maintain a curated TODO.** Track actionable items in `TODO.md`. When an item is completed or rejected, log the disposition in `LOG.md`.
3. **Session branches.** New topic-scoped work happens on a `session/<topic-slug>` branch off `main`. Small, granular commits. Merge back into `main` locally; switch to a PR when collaborators join.
4. **Single source of truth for policy.** This file owns the working agreement. `LOG.md` and `TODO.md` reference back here.
5. **Hardware-touching tools require explicit user permission.** `pio_upload`, `pio_monitor`, and `pio_monitor_stop` (and any future tool that flashes firmware, attaches to a serial port, or otherwise talks to physical hardware) may only be invoked after the user has explicitly asked for that action in the current turn. A bare "compile" or "what's the build state" does **not** count as a flash request.
6. **Never hide user-facing command output via pipe filters.** When running a build, install, test, or any long-running command on the user's behalf, do not pipe the output through `tail`, `head`, `grep -v`, or other filters that discard lines. Let the output stream to the user's terminal, or redirect the full output to a file the user can inspect (`*.log`, the agent's logging wrappers, etc.). Trivial short summaries (`git status -sb`, `git log --oneline -n 5`, `ls -la`) are still fine. The user must always be able to see what happened so they can troubleshoot and watch things.

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

## MCP tools

Seven MCP tools are registered via `.vscode/mcp.json`. The log_* tools are the **only** way the agent should read or write `LOG.md`; the pio_* tools are the only way it should invoke PlatformIO or capture serial monitor output.

### Log tools

- `log_add(title, context, decision, [consequences], [kind], [kind-specific args], [dryRun])` — append a MADR entry. Accepts kinds `decision` (default), `note`, `test`.
- `log_read(n?, since?, kind?)` — read recent entries; slice by `--n`, restrict by `--since` date, or filter by `--kind`.

### PlatformIO tools

- `pio_port()` — list connected serial devices via `pio device list`. **Always call this first** if you don't know the `/dev/cu.*` path.
- `pio_compile(env?, clean?)` — `pio run`. No hardware interaction.
- `pio_upload(env?, port)` — `pio run --target upload`. **Destructive — only when explicitly requested (see Policy #5).**
- `pio_monitor(port, baud?, env?, logPath?)` — start `pio device monitor` in background, capture to a per-session log file. **Destructive — only when explicitly requested.**
- `pio_monitor_stop(pid?)` — stop one or all running monitors (PIDs are tracked by the wrapper).

The pio_* tools do not read their output files themselves — they return only the wrapper's one-line summary. Full build/upload output goes to `<repo>/pio-log.txt`; monitor output goes to a per-session `serial-log-<port>-<ts>.txt`. Use `read_file` or `grep_search` on those files when needed.

### What the agent must NOT do

- Invoke `agent-script/log-add.mjs`, `log-read.mjs`, `pio.sh`, or `pio-monitor.sh` directly via the terminal. They are implementation detail; the MCP layer is the only sanctioned interface.
- Call `pio_upload` or `pio_monitor` without an explicit user request in the current turn (Policy #5).

The CLI scripts and bash wrappers remain on disk for human use — see `agent-script/README.md`.

## Pointers

- `LOG.md` — running record of decisions and changes.
- `TODO.md` — curated list of pending work.
- `agent-script/` — Node.js tooling for managing these docs.
