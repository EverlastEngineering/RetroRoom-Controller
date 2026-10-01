# The OpenAPI spec describes a config format several commits behind

**Status:** open
**File anchor:** `src/html/openapi.yaml` (`components.schemas.ConsoleConfig`)

## What

`ConsoleConfig` describes `irCodes`, `consoleNames`, `consoles` and
`Console`. Nothing else. The format those four cover is the format as it
stood before the `led` block was configuration-driven, and it is missing
every block added since:

- `shelves[]` — `{id, fromLed, toLed}`, and which the commit's explosion
  is clamped to
- `led` — the strip's whole feel, around thirty fields
- `lcd` — `backlightOffAfterMs`
- `network` — `showWIFIConnectionFailureMessage`, `disable`
- `menu` — the operator's own settings array, and the flags in it
- per-console `tagline` and `shelf`

It also has no `additionalProperties: false` on anything, so the schema
does not say which unknown keys are tolerated. They are, deliberately —
`applyConfigEdits` round-trips the document and writes back every key it
does not understand, and that is load-bearing.

## Why

Found by mistake, and the mistake is the argument.

Someone opened `/openapi`, clicked into the `ConsoleConfig` example, and
reasonably concluded the firmware had written a corrupt config — three
`irCodes` all set to `0x430`, three `consoleNames` all the same, and a
single console at `ledPosition: 0` when the shipped default has three
consoles at 1/7/13.

It had not. That document is Swagger UI's own synthesised sample,
built from `additionalProperties` (hence the literal `additionalProp1..3`)
and from the per-field `example:` values (hence `NES`, `Video`, and the
`minimum:` values used as defaults, which is where `ledPosition: 0` came
from). Fixed cosmetically by giving `irCodes` and `consoleNames` explicit
maps — real keys beat invented ones on a page someone will read.

But the underlying complaint was right and is still open. The page
presents itself as the API for the config endpoint, and it describes a
format that stopped being current several commits ago. Anyone using
"Try it out" to discover what a config may contain is being told
something false by omission, which is worse than the invented keys were.

## How

- Describe the missing blocks. `led` is the bulk of the work and the
  part most likely to drift — the field table in
  `lib/ConsoleConfig/src/LedFieldTable.cpp` is the source of truth for
  which keys exist and what ranges they hold, so anything hand-written
  here will go stale the way a hand-written menu did. If a generator
  falls out of that table, prefer it.
- Say explicitly that unknown keys are preserved, because that is
  load-bearing behaviour and not an accident.
- The three `examples:` under `/consoles.json` use
  `externalValue:` into `example-configurations/`. Reuse that rather
  than inlining a second copy of a config, for the same anti-drift
  reason.
- Decide whether the serial channel needs documenting here at all. It is
  not HTTP, so it does not belong in this file — but `GET`/`PUT CONFIG`
  and `POST /consoles.json` have different ergonomics (length-prefixed
  versus a `CONFIG DONE` terminator) and a reader comparing them should
  not have to guess. See
  `todo/open/2026-10-01_serial-mode-docs.md`.

## Not decided

- Whether the spec should pin exact key order or say "any order". The
  firmware preserves bytes, so any order is valid, and saying so stops
  someone writing a validator that rejects a valid document.
- Whether to describe the `menu` array's flags (`preview`, `type`,
  `min`/`max`/`step`) in the spec or leave them to
  `example-configurations/README.md`, which is where they are documented
  today and arguably where they belong.
