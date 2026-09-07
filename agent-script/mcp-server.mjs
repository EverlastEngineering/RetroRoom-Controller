#!/usr/bin/env node
// mcp-server.mjs — stdio MCP server exposing the agent-script CLIs as tools.
// Run via `npm --prefix agent-script run mcp` and configure in .vscode/mcp.json.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { z } from "zod";

const HERE = path.dirname(fileURLToPath(import.meta.url));

// Run one of the existing CLI scripts and return its stdout.
function runScript(script, args) {
  return new Promise((resolve, reject) => {
    const proc = spawn(process.execPath, [path.join(HERE, script), ...args], {
      stdio: ["ignore", "pipe", "pipe"],
    });
    let out = "";
    let err = "";
    proc.stdout.on("data", (b) => (out += b.toString()));
    proc.stderr.on("data", (b) => (err += b.toString()));
    proc.on("close", (code) => {
      if (code === 0) {
        resolve(out);
      } else {
        reject(new Error(err.trim() || `log script exited with code ${code}`));
      }
    });
    proc.on("error", reject);
  });
}

const server = new McpServer({
  name: "retroroom-agent-scripts",
  version: "1.0.0",
});

server.tool(
  "log_add",
  "Append a MADR-formatted entry to LOG.md. kind=decision (default) requires context+decision; kind=note requires note; kind=test requires run+result.",
  {
    title: z.string().describe("Short title for the entry heading"),
    kind: z
      .enum(["decision", "note", "test"])
      .optional()
      .describe("Entry kind. Default 'decision'."),
    context: z
      .string()
      .optional()
      .describe("decision: what situation prompted the choice"),
    decision: z
      .string()
      .optional()
      .describe("decision: what was chosen"),
    consequences: z
      .string()
      .optional()
      .describe("decision|test: tradeoffs, followups, or 'revisit if X'"),
    note: z
      .string()
      .optional()
      .describe("note: the note text (used when kind=note)"),
    run: z
      .string()
      .optional()
      .describe("test: what was executed"),
    result: z
      .string()
      .optional()
      .describe("test: what was observed"),
    dryRun: z
      .boolean()
      .optional()
      .describe("Print the entry that would be written without modifying any file"),
  },
  async ({ title, kind, context, decision, consequences, note, run, result, dryRun }) => {
    const args = ["--title", title];
    if (kind) args.push("--kind", kind);
    if (context) args.push("--context", context);
    if (decision) args.push("--decision", decision);
    if (consequences) args.push("--consequences", consequences);
    if (note) args.push("--note", note);
    if (run) args.push("--run", run);
    if (result) args.push("--result", result);
    if (dryRun) args.push("--dry-run");
    const text = await runScript("log-add.mjs", args);
    return { content: [{ type: "text", text: text.trim() }] };
  }
);

server.tool(
  "log_read",
  "Read recent LOG.md entries.",
  {
    n: z
      .number()
      .int()
      .positive()
      .optional()
      .describe("Number of most-recent entries to return"),
    since: z
      .string()
      .optional()
      .describe("ISO date YYYY-MM-DD; only entries on or after this date"),
    kind: z
      .enum(["decision", "note", "test"])
      .optional()
      .describe("Filter by entry kind"),
  },
  async ({ n, since, kind }) => {
    const args = [];
    if (n !== undefined) args.push("--n", String(n));
    if (since) args.push("--since", since);
    if (kind) args.push("--kind", kind);
    const text = await runScript("log-read.mjs", args);
    return { content: [{ type: "text", text: text.trim() }] };
  }
);

await server.connect(new StdioServerTransport());
