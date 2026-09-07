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

// Run an arbitrary command and return { stdout, stderr, exit } so tools can
// decide what to surface. Used by the pio_* tools which shell out to bash
// wrappers (pio.sh, pio-monitor.sh) that handle their own logging conventions.
function runCapture(cmd, args) {
  return new Promise((resolve, reject) => {
    const proc = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"] });
    let out = "";
    let err = "";
    proc.stdout.on("data", (b) => (out += b.toString()));
    proc.stderr.on("data", (b) => (err += b.toString()));
    proc.on("close", (code) => resolve({ stdout: out, stderr: err, exit: code ?? -1 }));
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

// ---------------------------------------------------------------------------
// PlatformIO tools (port / compile / upload / monitor). All shell out to the
// agent-script/pio.sh and agent-script/pio-monitor.sh wrappers so that humans,
// the agent, and CI hit the same logging convention. The wrappers are also
// invokable from any terminal — see agent-script/README.md.
// ---------------------------------------------------------------------------

const PIO_SH = path.join(HERE, "pio.sh");
const PIO_MONITOR_SH = path.join(HERE, "pio-monitor.sh");

server.tool(
  "pio_port",
  "List connected serial devices (via `pio device list`). Call this first if you don't know which /dev/cu.* path to use.",
  {},
  async () => {
    const { stdout, stderr, exit } = await runCapture("bash", [PIO_SH, "device", "list"]);
    return {
      content: [{ type: "text", text: stdout || stderr || `(exit ${exit}; no output)` }],
    };
  }
);

server.tool(
  "pio_compile",
  "Compile firmware via `pio run` (no hardware interaction). Full output is appended to <repo>/pio-log.txt; the wrapper's summary line is returned here.",
  {
    env: z.string().optional().describe("PlatformIO environment name (default from platformio.ini)."),
    clean: z.boolean().optional().describe("Run `pio run --target clean` first to wipe the build cache."),
  },
  async ({ env, clean }) => {
    const args = ["run"];
    if (clean) args.push("--target", "clean");
    if (env) args.push("--environment", env);
    const { stdout, stderr, exit } = await runCapture("bash", [PIO_SH, ...args]);
    return {
      content: [{
        type: "text",
        text: stdout || stderr || `(exit ${exit}; no output)`,
      }],
    };
  }
);

server.tool(
  "pio_upload",
  "Build and flash firmware via `pio run --target upload`. **Only call when the user has explicitly asked you to flash hardware.** Full output is appended to <repo>/pio-log.txt.",
  {
    env: z.string().optional().describe("PlatformIO environment name (default from platformio.ini)."),
    port: z.string().describe("Serial device path, e.g. /dev/cu.usbserial-11330. Use pio_port to discover connected devices."),
  },
  async ({ env, port }) => {
    if (!port) {
      return {
        content: [{
          type: "text",
          text: "port is required. Call pio_port first to discover connected devices.",
        }],
      };
    }
    const args = ["run", "--target", "upload", "--upload-port", port];
    if (env) args.push("--environment", env);
    const { stdout, stderr, exit } = await runCapture("bash", [PIO_SH, ...args]);
    return {
      content: [{
        type: "text",
        text: stdout || stderr || `(exit ${exit}; no output)`,
      }],
    };
  }
);

server.tool(
  "pio_monitor",
  "Start `pio device monitor` in the background. Output is captured to a per-session log file (path returned). Use pio_monitor_stop to end. **Only call when the user has explicitly asked you to attach a serial monitor.**",
  {
    port: z.string().describe("Serial device path, e.g. /dev/cu.usbserial-11330."),
    baud: z.number().int().positive().optional().describe("Baud rate (default 76800, from platformio.ini)."),
    env: z.string().optional().describe("PlatformIO environment name (default 'nodemcuv2')."),
    logPath: z.string().optional().describe("Override the capture file path. Default: serial-log-<port>-<ts>.txt in repo root."),
  },
  async ({ port, baud = 76800, env, logPath }) => {
    const args = ["bg", "--port", port, "--baud", String(baud)];
    if (env) args.push("--env", env);
    if (logPath) args.push("--log", logPath);
    const { stdout, stderr, exit } = await runCapture("bash", [PIO_MONITOR_SH, ...args]);
    return {
      content: [{
        type: "text",
        text: stdout || stderr || `(exit ${exit}; no output)`,
      }],
    };
  }
);

server.tool(
  "pio_monitor_stop",
  "Stop one or all serial monitors started via pio_monitor.",
  {
    pid: z.number().int().positive().optional().describe("Specific monitor PID to stop. Omit to stop all."),
  },
  async ({ pid }) => {
    const args = ["stop"];
    if (pid !== undefined) args.push("--pid", String(pid));
    const { stdout, stderr, exit } = await runCapture("bash", [PIO_MONITOR_SH, ...args]);
    return {
      content: [{
        type: "text",
        text: stdout || stderr || `(exit ${exit}; no output)`,
      }],
    };
  }
);

await server.connect(new StdioServerTransport());
