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

## File layout

```
agent-script/
├── README.md       this file
├── package.json    scripts metadata, declares ES module type
├── .nvmrc          Node.js version pin
├── log-add.mjs     append entry to LOG.md
└── log-read.mjs    read recent entries from LOG.md
```
