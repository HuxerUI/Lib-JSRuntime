#!/usr/bin/env node
// Starts one TSX layout-preview session: an esbuild watcher and the native app.
// The app's built-in watcher reloads the bundle after each rebuild, so one
// command is enough for the whole dev loop.
//
//   node tools/huxerui-tsx-dev.mjs \
//       --entry examples/preview/src/App.tsx \
//       --out examples/preview/build/app.js \
//       --app examples/preview/build/huxerui_tsx_preview
//
// Ctrl+C stops both processes.

import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

function usage(message) {
  if (message) {
    process.stderr.write(`huxerui-tsx-dev: ${message}\n`);
  }
  process.stderr.write(
    "usage: node tools/huxerui-tsx-dev.mjs --entry <App.tsx> --out <app.js> " +
      "--app <executable> [--esbuild <path>]\n"
  );
  process.exit(2);
}

const argv = process.argv.slice(2);
let entry = null;
let out = null;
let app = null;
let esbuild = process.env.HUXERUI_ESBUILD || null;

for (let index = 0; index < argv.length; index += 1) {
  const argument = argv[index];
  if (argument === "--entry") {
    entry = argv[++index] ?? usage("missing entry");
  } else if (argument === "--out") {
    out = argv[++index] ?? usage("missing output");
  } else if (argument === "--app") {
    app = argv[++index] ?? usage("missing app");
  } else if (argument === "--esbuild") {
    esbuild = argv[++index] ?? usage("missing esbuild path");
  } else if (argument === "-h" || argument === "--help") {
    usage();
  } else {
    usage(`unknown argument ${argument}`);
  }
}

if (!entry || !out || !app) {
  usage("entry, output, and app are required");
}

const tool = path.join(path.dirname(fileURLToPath(import.meta.url)), "huxerui-tsx.mjs");
const toolArguments = [tool, entry, "-o", out];
if (esbuild) {
  toolArguments.push("--esbuild", esbuild);
}

// Make sure the app has a bundle before it starts; the watcher takes over after.
const initial = spawnSync(process.execPath, toolArguments, { stdio: "inherit" });
if (initial.error) {
  process.stderr.write(`huxerui-tsx-dev: ${initial.error.message}\n`);
  process.exit(1);
}
if (initial.status !== 0) {
  process.exit(initial.status ?? 1);
}

const watcher = spawn(
  process.execPath,
  [...toolArguments, "--watch"],
  { stdio: "inherit" }
);

const appProcess = spawn(app, [], {
  stdio: "inherit",
  env: { ...process.env, HUXERUI_TSX_BUNDLE: path.resolve(out) },
});

let stopping = false;

function stop(code) {
  if (stopping) {
    return;
  }
  stopping = true;
  for (const child of [appProcess, watcher]) {
    if (child && child.exitCode === null && child.signalCode === null) {
      child.kill("SIGTERM");
    }
  }
  setTimeout(() => process.exit(code), 150);
}

process.on("SIGINT", () => stop(0));
process.on("SIGTERM", () => stop(0));

appProcess.on("exit", (code, signal) => {
  stop(code ?? (signal ? 1 : 0));
});

watcher.on("exit", (code, signal) => {
  if (!stopping) {
    process.stderr.write(
      `huxerui-tsx-dev: watcher exited (${code ?? signal ?? "unknown"})\n`
    );
    stop(code ?? 1);
  }
});
