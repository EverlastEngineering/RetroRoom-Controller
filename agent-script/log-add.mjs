#!/usr/bin/env node
// log-add.mjs — append a new entry to LOG.md in MADR format.
// Usage: node log-add.mjs --title "..." --context "..." --decision "..." [--consequences "..."] [--dry-run]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const LOG_PATH = path.resolve(__dirname, '..', 'LOG.md');
const INSERT_MARKER = '<!-- insert-below -->';

const USAGE = `Usage:
  node log-add.mjs --title "..." [--kind decision|note|test] [kind-specific args] [--dry-run]

Kind=decision (default):
  --title          Short title for the entry heading
  --context        What situation prompted the decision
  --decision       What was chosen
  [--consequences  Tradeoffs, followups, or "revisit if X"]

Kind=note:
  --title          Short title for the entry heading
  --note           The note text

Kind=test:
  --title          Short title for the entry heading
  --run            What was executed
  --result         What was observed
  [--consequences  Tradeoffs, followups, or "revisit if X"]

Global:
  --kind           decision | note | test   (default: decision)
  --dry-run        Print the entry that would be written, do not modify any file
`;

const args = process.argv.slice(2);
const opts = {};
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  switch (a) {
    case '--title':        opts.title = args[++i]; break;
    case '--kind':         opts.kind = args[++i]; break;
    case '--context':      opts.context = args[++i]; break;
    case '--decision':     opts.decision = args[++i]; break;
    case '--consequences': opts.consequences = args[++i]; break;
    case '--note':         opts.note = args[++i]; break;
    case '--run':          opts.run = args[++i]; break;
    case '--result':       opts.result = args[++i]; break;
    case '--dry-run':      opts.dryRun = true; break;
    case '-h':
    case '--help':
      console.log(USAGE);
      process.exit(0);
    default:
      console.error(`Unknown argument: ${a}\n`);
      console.error(USAGE);
      process.exit(1);
  }
}

const VALID_KINDS = ['decision', 'note', 'test'];
const kind = opts.kind || 'decision';
if (!VALID_KINDS.includes(kind)) {
  console.error(`--kind must be one of: ${VALID_KINDS.join(', ')}\n`);
  console.error(USAGE);
  process.exit(1);
}

if (!opts.title) {
  console.error('Missing required argument: --title\n');
  console.error(USAGE);
  process.exit(1);
}

const missing = [];
if (kind === 'decision') {
  if (!opts.context)  missing.push('--context');
  if (!opts.decision) missing.push('--decision');
} else if (kind === 'note') {
  if (!opts.note)     missing.push('--note');
} else if (kind === 'test') {
  if (!opts.run)      missing.push('--run');
  if (!opts.result)   missing.push('--result');
}
if (missing.length > 0) {
  console.error(`Missing required argument(s) for kind=${kind}: ${missing.join(', ')}\n`);
  console.error(USAGE);
  process.exit(1);
}

const timestamp = new Date().toISOString();
const bodyLines = [];
if (kind === 'note') {
  bodyLines.push(`**Kind:** ${kind}`);
  bodyLines.push(`**Note:** ${opts.note}`);
} else if (kind === 'test') {
  bodyLines.push(`**Kind:** ${kind}`);
  bodyLines.push(`**Run:** ${opts.run}`);
  bodyLines.push('');
  bodyLines.push(`**Result:** ${opts.result}`);
  if (opts.consequences) {
    bodyLines.push('');
    bodyLines.push(`**Consequences:** ${opts.consequences}`);
  }
} else {
  // decision (default)
  bodyLines.push(`**Context:** ${opts.context}`);
  bodyLines.push('');
  bodyLines.push(`**Decision:** ${opts.decision}`);
  if (opts.consequences) {
    bodyLines.push('');
    bodyLines.push(`**Consequences:** ${opts.consequences}`);
  }
}

const entry = [
  `## ${timestamp} — ${opts.title}`,
  '',
  ...bodyLines,
  '',
  '---',
  '',
].join('\n');

if (opts.dryRun) {
  console.log(`Would append to ${LOG_PATH}:\n`);
  console.log(entry);
  process.exit(0);
}

let content;
try {
  content = fs.readFileSync(LOG_PATH, 'utf8');
} catch (err) {
  if (err.code === 'ENOENT') {
    console.error(`LOG.md not found at ${LOG_PATH}. Create it before adding entries.`);
    process.exit(2);
  }
  throw err;
}

const markerIndex = content.indexOf(INSERT_MARKER);
if (markerIndex === -1) {
  console.error(`Insertion marker "${INSERT_MARKER}" not found in LOG.md. Add it where new entries should be inserted.`);
  process.exit(3);
}

const insertAt = markerIndex + INSERT_MARKER.length;
const before = content.slice(0, insertAt);
const after = content.slice(insertAt);
const updated = before + '\n' + entry + after;

fs.writeFileSync(LOG_PATH, updated, 'utf8');
console.log(`Appended entry to ${LOG_PATH}`);
