# agent-script/

Tooling for managing `LOG.md`, `TODO.md`, PlatformIO builds/uploads, and serial-monitor capture in RetroRoom-Controller. Includes a stdio MCP server (`mcp-server.mjs`) so the agent can call these scripts natively from Copilot Chat.

## Prerequisites

- Node.js 18+ (see `.nvmrc`)
- `pio` (PlatformIO Core) — install via the official script, or use the homebrew venv at `~/.platformio/penv/bin/pio` if present
- `bash` 3.2+ (already on macOS)

## Install

```bash
cd agent-script
npm install
```

Then add `pio` to your PATH so the wrappers can find it:

```bash
echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.zshrc
```

The MCP server is started by VS Code from `.vscode/mcp.json` (`npm --prefix agent-script run mcp`); you don't need to launch it manually.

## Scripts

### `log-add.mjs` — append a new entry to `LOG.md`

Usage:

```bash
# decision (default)
node log-add.mjs --title "Short title here" --context "What situation." --decision "What we chose." --consequences "Tradeoffs / followups."

# note (no decision frame needed)
node log-add.mjs --kind note --title "Observation" --note "Free-form note text."

# test (records a run + result)
node log-add.mjs --kind test --title "What was tested" --run "What was executed" --result "What was observed"
```

If `--kind` is omitted, the entry is treated as a `decision`.

Arguments:

| Flag | Required | Required for kind | Description |
|---|---|---|---|
| `--title` | yes | all | Short title shown in the entry heading |
| `--kind` | no | n/a | One of `decision`, `note`, `test`. Default `decision`. |
| `--context` | yes | decision | What situation prompted the decision |
| `--decision` | yes | decision | What was chosen |
| `--consequences` | no | decision, test | Tradeoffs, followups, or "revisit if X" |
| `--note` | yes | note | The note text |
| `--run` | yes | test | What was executed |
| `--result` | yes | test | What was observed |
| `--dry-run` | no | n/a | Print the entry that would be written, do not modify any file |

### `log-read.mjs` — read recent LOG.md entries

Usage:

```bash
node log-read.mjs                                  # read all entries
node log-read.mjs --n 5                            # read the 5 most recent
node log-read.mjs --since 2026-09-01               # entries on or after a date
node log-read.mjs --kind decision|note|test        # filter by entry kind
```

If invoked with no arguments, reads all entries.

Arguments:

| Flag | Required | Description |
|---|---|---|
| `--n` | no | Number of most-recent entries to print (default: all) |
| `--since` | no | ISO date `YYYY-MM-DD`; only entries on or after this date |
| `--kind` | no | One of `decision`, `note`, `test`. Entries without an explicit kind default to `decision`. |

## How entries are formatted

Entries use [MADR](https://adr.github.io/madr/) format with three kinds:

**`decision` (default)** — for choices worth recording:

```markdown
## <ISO-8601 UTC timestamp> — <title>

**Context:** <text>

**Decision:** <text>

**Consequences:** <text>

---
```

**`note`** — for observations, reminders, or non-decision content:

```markdown
## <ISO-8601 UTC timestamp> — <title>

**Kind:** note
**Note:** <text>

---
```

**`test`** — for recording the result of a run (smoke test, build, flash, etc.):

```markdown
## <ISO-8601 UTC timestamp> — <title>

**Kind:** test
**Run:** <text>

**Result:** <text>

**Consequences:** <text>

---
```

A marker comment `<!-- insert-below -->` in `LOG.md` tells the script where to place new entries. The script inserts immediately after this marker.

## MCP server (`mcp-server.mjs`)

A stdio MCP server that exposes the LOG scripts and the PlatformIO wrappers as Copilot Chat tools. Configure in `.vscode/mcp.json`; the server auto-starts when VS Code opens the workspace.

### Log tools

| Tool | Purpose |
|---|---|
| `log_add(title, kind?, ...kind-specific args, dryRun?)` | Append a `decision` / `note` / `test` entry. See `log-add.mjs` for argument shapes. |
| `log_read(n?, since?, kind?)` | Read entries; slice / date-filter / kind-filter. |

### PlatformIO tools

All wrap `agent-script/pio.sh` and `agent-script/pio-monitor.sh` so humans and the agent hit the same logging convention.

| Tool | Purpose | Notes |
|---|---|---|
| `pio_port()` | List connected serial devices via `pio device list`. | Read-only; safe. Call first to discover ports. |
| `pio_compile(env?, clean?)` | `pio run` (no hardware). | Full output appended to `pio-log.txt`. |
| `pio_upload(env?, port)` | `pio run --target upload`. | **Destructive — only when user explicitly asks.** |
| `pio_monitor(port, baud?, env?, logPath?)` | Start `pio device monitor` in background; output captured to a per-session log file. | **Destructive — only when user explicitly asks.** |
| `pio_monitor_stop(pid?)` | Stop one or all running monitors. | Default stops all. |

The MCP tools return only the wrapper's one-line summary (`OK ...` or `ERR(N) ...`). Full output is in `pio-log.txt` for build/upload runs, and in the per-session `serial-log-<port>-<ts>.txt` for monitor runs. Use `read_file` or `grep_search` on those files; the agent does **not** read them itself.

## PlatformIO wrappers

These thin bash wrappers give humans and the agent one consistent logging convention. Every `pio` invocation (whether from a terminal or via MCP) lands in `<repo>/pio-log.txt` (or a per-session `serial-log-*.txt`).

### `pio.sh` — invoke `pio` with logging

```bash
./pio.sh device list
./pio.sh run --target upload --upload-port /dev/cu.usbserial-11330
./pio.sh run --target clean
```

Appends a timestamped header + `pio` invocation to `pio-log.txt`, then prints a one-line summary (`OK`/`ERR(N)`) to your terminal. Exit code reflects `pio`'s.

### `pio-monitor.sh` — capture serial monitor output to a file

```bash
./pio-monitor.sh bg --port /dev/cu.usbserial-11330               # detached; PID + log path printed
./pio-monitor.sh fg --port /dev/cu.usbserial-11330 --baud 115200 # foreground, also captures to a log
./pio-monitor.sh list                                             # show active background monitors
./pio-monitor.sh stop                                             # stop all
./pio-monitor.sh stop --pid 12345                                 # stop one
```

Background monitors detach via `nohup` and survive MCP tool return. Active PIDs persist to `agent-script/.pio-monitors.state` (gitignored). Default baud is 76800 (from `platformio.ini`); env defaults to `nodemcuv2`.

## File layout

```
agent-script/
├── README.md             this file
├── package.json          scripts metadata, declares ES module type, npm deps
├── package-lock.json     reproducible installs (committed)
├── .nvmrc                Node.js version pin
├── log-add.mjs           append entry to LOG.md (decision | note | test)
├── log-read.mjs          read recent entries from LOG.md
├── mcp-server.mjs        stdio MCP server (log_add, log_read, pio_*)
├── pio.sh                PlatformIO wrapper (logs to <repo>/pio-log.txt)
└── pio-monitor.sh        serial-monitor wrapper (logs to per-session file)
```
