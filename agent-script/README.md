# agent-script/

Node.js tooling for managing `LOG.md`, `TODO.md`, and other process artifacts in RetroRoom-Controller.

## Prerequisites

- Node.js 18+ (see `.nvmrc`)
- No external dependencies — these scripts use only Node built-ins

## Install

Nothing to install. `cd` into this folder before invoking scripts, or invoke with explicit paths.

## Scripts

### `log-add.mjs` — append a new entry to `LOG.md`

Usage:

```bash
node log-add.mjs --title "Short title here" --context "What situation." --decision "What we chose." --consequences "Tradeoffs / followups."
```

If invoked with no arguments, prints usage instructions and exits with code 1.

Arguments:

| Flag | Required | Description |
|---|---|---|
| `--title` | yes | Short title shown in the entry heading |
| `--context` | yes | What situation prompted the decision |
| `--decision` | yes | What was chosen |
| `--consequences` | no | Tradeoffs, followups, or "revisit if X" |
| `--dry-run` | no | Print the entry that would be written, do not modify any file |

### `log-read.mjs` — read recent LOG.md entries

Usage:

```bash
node log-read.mjs                # read all entries
node log-read.mjs --n 5          # read the 5 most recent
node log-read.mjs --since 2026-09-01   # entries on or after a date
```

If invoked with no arguments, reads all entries.

Arguments:

| Flag | Required | Description |
|---|---|---|
| `--n` | no | Number of most-recent entries to print (default: all) |
| `--since` | no | ISO date `YYYY-MM-DD`; only entries on or after this date |

## How entries are formatted

Entries use [MADR](https://adr.github.io/madr/) format. The script writes:

```markdown
## <ISO-8601 UTC timestamp> — <title>

**Context:** <text>

**Decision:** <text>

**Consequences:** <text>

---
```

A marker comment `<!-- insert-below -->` in `LOG.md` tells the script where to place new entries. The script inserts immediately after this marker.

## File layout

```
agent-script/
├── README.md       this file
├── package.json    scripts metadata, declares ES module type
├── .nvmrc          Node.js version pin
├── log-add.mjs     append entry to LOG.md
└── log-read.mjs    read recent entries from LOG.md
```
