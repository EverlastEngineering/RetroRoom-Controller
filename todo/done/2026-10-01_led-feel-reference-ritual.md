# Make the led-feel reference a ritual, not a memory test

**Status:** open
**File anchor:** `agent-script/led-feel-dump.cpp`,
`agent-script/led-feel-dump.sh`, `example-configurations/example6-all-options.json`

## What

After finishing a piece of work, regenerate the `led` block reference
and diff it. Not when a field is added -- routinely, at the end of
work, so an option that was added and never given a default shows up as
a diff rather than as a device that lights differently from the file.

The command is `./agent-script/led-feel-dump.sh -o <file>`, and it is
genuinely easy to forget. Twice in one session the shipped default
config and the reference drifted apart and the only reason it was
noticed is that a new field had not been given a value anywhere.

## Why

`defaultLedFeel()` in `lib/ConsoleConfig/src/LedFeelDefaults.cpp` is the
single answer to "what is a field's default if the file does not say".
The reference file is a *copy* of it. A copy drifts, and the drift is
invisible until a cabinet behaves differently from its own
documentation.

This is the same class of problem as the two already fixed in the
session:

- `configSave()` wrote a non-`led` setting into the `led` block
  because a bare path resolved to the first block.
- The shipped default had no `menu` array, so a fresh cabinet could not
  reach the radio switch by the knob.

Both were silent, both were "two places state the same thing", and
neither had anything that would have noticed.

## How

- Add the regeneration to the end-of-work checklist in `AGENT.md`,
  next to the build and test steps, with the exact command.
- Better than a checklist: a wrapper that regenerates into a temp file,
  diffs against the checked-in one, and exits non-zero on a difference,
  so it can be run in the same breath as the tests. Failing the build
  on a stale reference is the only version that survives contact with a
  busy week.
- Decide what the reference is *for*, because that decides where it is
  generated from. Two candidates, and they are not the same thing:
  - **A defaults reference**, generated from `defaultLedFeel()`. What
    `led-feel-dump` does today. Useful for "what does an absent field
    mean".
  - **A template**, generated from `src/factory-config.json`, i.e. a
    copy of the config a fresh cabinet actually runs. Useful for "what
    should I write in a config".

  These agree today and will not always. The shipped config's `led`
  block is currently hand-copied from the example file, and
  `test_the_shipped_config_parses` checks that it loads -- not that its
  `led` block still matches the defaults. That is the remaining hole in
  the anti-drift work done in the last commit, and this item is where
  it gets closed.
- Whichever it is, the example config and the shipped config should
  stop being two hand-maintained copies of the same document.

## Not decided

- Whether the example files should be generated at all, or whether one
  canonical file should be generated and the rest hand-written as
  deliberate variations. Right now there are seven and at least two
  pairs that could disagree without anyone noticing.
- Where the generated output lives: checked in (diffable, drifts
  silently) or generated at build time (never drifts, never diffable).
  Checked-in-and-verified is the usual answer and probably right here,
  but the verification step is the whole point of this item.
