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
  node log-add.mjs --title "..." --context "..." --decision "..." [--consequences "..."] [--dry-run]

Required:
  --title         Short title for the entry heading
  --context       What situation prompted the decision
  --decision      What was chosen

Optional:
  --consequences  Tradeoffs, followups, or "revisit if X"
  --dry-run       Print the entry that would be written, do not modify any file
`;

const args = process.argv.slice(2);
const opts = {};
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  switch (a) {
    case '--title':        opts.title = args[++i]; break;
    case '--context':      opts.context = args[++i]; break;
    case '--decision':     opts.decision = args[++i]; break;
    case '--consequences': opts.consequences = args[++i]; break;
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

if (!opts.title || !opts.context || !opts.decision) {
  console.error('Missing required arguments.\n');
  console.error(USAGE);
  process.exit(1);
}

const timestamp = new Date().toISOString();
const entry = [
  `## ${timestamp} — ${opts.title}`,
  '',
  `**Context:** ${opts.context}`,
  '',
  `**Decision:** ${opts.decision}`,
  ...(opts.consequences ? ['', `**Consequences:** ${opts.consequences}`] : []),
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
