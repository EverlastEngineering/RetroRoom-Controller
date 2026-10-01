# Complete the config: fill in what is missing, and let a menu row hide itself

**Status:** open — needs a decision on *when* a document gets completed
**File anchors:** `lib/ConsoleConfig/src/` (the core),
`src/consoleconfig_store.cpp` (flash), `src/consoles.cpp::buildShellMenu()`,
`agent-script/e2e-serial.py`

## What

Two related things:

1. **`"visible": true|false` on every `menu` row.** A row with
   `visible: false` stays in `/consoles.json` and does not appear on the
   LCD. The point is that an operator reading their own config can see
   that the setting exists and what it does, without it taking up one of
   the forty rows on a sixteen-column screen.
2. **Completion.** When a document is read out or written, every setting
   in the field registry is present in it — taken from the operator's own
   value where there is one, from the shipped default where there is
   not. The stated reason: firmware gains settings over time, and an old
   file is silent about the new ones.

Absent means `visible: true`, so every config written before this change
behaves exactly as it did.

## When — the decision this note is actually about

**Recommended: complete on output *and* on save. Not on load.**

| Point | For | Against |
|---|---|---|
| On output (`GET /consoles.json`, `GET CONFIG`) | This is the stated goal; costs nothing; a person reading the file sees everything | The file on flash stays incomplete until the next save |
| On save (`configSave()`, POST, `PUT CONFIG`) | The file actually converges, so the *next* boot's output is complete without a round trip | A save rewrites settings the operator never touched — visible in git if they keep the file in version control |
| On load | Nothing; the running cabinet always has every value, because the parser defaults anyway | The running config and the file would disagree, and every save would then write values nobody chose. Also breaks the e2e's byte-exact `GET`/`PUT` round trip |

Both of the first two, not the third.

**The byte-exact round trip is a real cost and worth naming.** The e2e
currently asserts that `GET CONFIG` → `PUT CONFIG` returns the same
bytes. Completion makes that assertion false on the first pass — the
`GET` is complete, so the `PUT` writes the completed document, and the
file genuinely is different. It is also a *better* property to hold:
after one round trip the document is stable, which an incomplete
document never was. The assertion should become "the second `GET`
equals the first", not "the first `GET` equals the file".

## What is well-defined, and what is not

Completion only makes sense for **known settings**. The blocks the
registry covers — `led`, `lcd`, `network`, and whatever comes next — have
a declared default, a range and a name.

It does **not** make sense for the arrays, and this is the boundary of
the idea:

- `consoles` — there is no sensible "default console". An empty array is
  a real state (a locked-down cabinet), and inventing an entry would
  be inventing hardware.
- `shelves`, `irCodes`, `consoleNames` — same. A missing IR code means
  "do not send this", not "send the default".

So "do this for every property" means "for every property in the field
registry", which is a slightly smaller promise and is the one worth
keeping.

## Defaults: one home, not two

The win underneath all of this is that defaults stop being stated twice.

Today a new `led` setting has to be added in three places:

1. `LedFeelDefaults.cpp::defaultLedFeel()` — the value used when the
   document is silent,
2. `LedFieldTable.cpp` — the range, type and `needsRestart` flag,
3. `src/factory-config.json` — the value the shipped document states.

Nothing enforces that they agree, and the last session's
`ringOffDelayMs` / `ringFadeMs` / `brightnessPct` gap was exactly that,
found by a test rather than by reading.

Making `src/factory-config.json` the source of truth means
`defaultLedFeel()` parses the embedded document instead of returning a
hand-written struct. That deletes a whole file's worth of duplicated
numbers. The cost is a `deserializeJson` of an ~8 KB document wherever
a default is wanted, and the callers of `defaultLedFeel()` — the
simulator and the host tests — would have to parse too. That is
probably right anyway (the simulator would be testing the values the
device actually ships rather than a C array that can drift), but it is
a boot-cost and RAM question, not just a deletion.

`LedFieldTable.cpp` stays. It is not a default, it is the *constraint*,
and a constraint cannot live in a JSON document that the operator is
allowed to edit.

## How

- One function in the core, `completeConfigJson(json)`, next to
  `applyConfigEdits()` in `ConfigWrite.cpp`. It round-trips through
  ArduinoJson exactly as that one does, which already preserves key
  order and unknown keys — so completion cannot lose a key the firmware
  does not know about.
- `buildShellMenu()` skips `visible: false` when it copies into the
  fixed shell array. A skipped row therefore **frees a slot**, so
  hiding rows is how a config with more settings than the screen can
  show gets the useful ones on it.
- `src/factory-config.json` ships a row for every registry field with
  `"visible": true`, so the shipped document is the completed shape and
  the test that already checks "every field has a menu row" grows to
  check the flag.
- The OpenAPI schema (`src/html/openapi.yaml`) needs the `visible`
  property and a description of completion; see
  `2026-10-01_openapi-config-schema.md`, which is behind on several
  commits already.

## Open questions for the operator

- Should a completed save preserve the operator's formatting and
  comments? It cannot: ArduinoJson does not keep comments, and
  `applyConfigEdits()` has been reformatting on every save since it was
  written. Completion makes that happen to settings nobody touched,
  which is more visible, not less.
- Should there be a way to *see* that a value was filled in rather
  than chosen? An `_injected` key, or a warning list on `GET`. The
  parser already collects warnings; nothing consumes them on the HTTP
  side today.
- Is `visible` the right name, and should it be `hidden` to match the
  rest of the config's positive naming?
