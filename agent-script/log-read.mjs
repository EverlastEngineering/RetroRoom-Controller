#!/usr/bin/env node
// log-read.mjs — read recent LOG.md entries.
// Usage: node log-read.mjs [--n N] [--since YYYY-MM-DD]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const LOG_PATH = path.resolve(__dirname, '..', 'LOG.md');
const INSERT_MARKER = '<!-- insert-below -->';

const USAGE = `Usage:
  node log-read.mjs                            # read all entries
  node log-read.mjs --n N                      # read the N most recent
  node log-read.mjs --since YYYY-MM-DD         # entries on or after the date
  node log-read.mjs --kind decision|note|test  # filter by entry kind
`;

const VALID_KINDS = ['decision', 'note', 'test'];

const args = process.argv.slice(2);
const opts = { n: Infinity, since: null, kind: null };
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  switch (a) {
    case '--n':     opts.n = parseInt(args[++i], 10); break;
    case '--since': opts.since = args[++i]; break;
    case '--kind':  opts.kind = args[++i]; break;
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

if (opts.kind && !VALID_KINDS.includes(opts.kind)) {
  console.error(`--kind must be one of: ${VALID_KINDS.join(', ')}\n`);
  console.error(USAGE);
  process.exit(1);
}

let content;
try {
  content = fs.readFileSync(LOG_PATH, 'utf8');
} catch (err) {
  if (err.code === 'ENOENT') {
    console.error(`LOG.md not found at ${LOG_PATH}.`);
    process.exit(2);
  }
  throw err;
}

const markerIndex = content.indexOf(INSERT_MARKER);
if (markerIndex === -1) {
  console.error(`Insertion marker "${INSERT_MARKER}" not found in LOG.md.`);
  process.exit(3);
}

const entriesRegion = content.slice(markerIndex + INSERT_MARKER.length);
const entries = entriesRegion
  .split(/^---$/m)
  .map(s => s.trim())
  .filter(s => s.length > 0);

function entryKind(entry) {
  const m = entry.match(/^\*\*Kind:\*\*\s*(\w+)/m);
  return m ? m[1] : 'decision'; // entries without explicit Kind default to decision
}

let filtered = entries;
if (opts.since) {
  filtered = filtered.filter(e => {
    const m = e.match(/^## (\d{4}-\d{2}-\d{2})/);
    return m && m[1] >= opts.since;
  });
}
if (opts.kind) {
  filtered = filtered.filter(e => entryKind(e) === opts.kind);
}

const slice = filtered.slice(0, opts.n);

if (slice.length === 0) {
  console.log('(no entries)');
  process.exit(0);
}

console.log(slice.join('\n\n---\n\n'));
