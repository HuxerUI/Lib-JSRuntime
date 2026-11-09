#!/usr/bin/env node
// Bundles one TSX entry into the single ES module that HuxerUI::TSX evaluates.
//
// Usage:
//   node tools/huxerui-tsx.mjs src/App.tsx -o build/app.js [--minify] [--sourcemap]
//
// esbuild resolution order: --esbuild <path>, HUXERUI_ESBUILD, the vendored
// binary under third_party/esbuild/, `esbuild` on PATH, then the project's
// node_modules. @huxerui/tsx stays external because the native runtime serves
// it.

import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

function usage(message) {
  if (message) {
    process.stderr.write(`huxerui-tsx: ${message}\n`);
  }
  process.stderr.write(
    "usage: node tools/huxerui-tsx.mjs <entry.tsx> -o <output.js> " +
      "[--minify] [--sourcemap] [--watch] [--esbuild <path>] [--external <module>]...\n"
  );
  process.exit(2);
}

const argv = process.argv.slice(2);
let entry = null;
let outfile = null;
let esbuild = process.env.HUXERUI_ESBUILD || null;
let minify = false;
let sourcemap = false;
let watch = false;
const externals = new Set();

for (let index = 0; index < argv.length; index += 1) {
  const argument = argv[index];
  if (argument === "-o" || argument === "--outfile") {
    outfile = argv[++index] ?? usage("missing output path");
  } else if (argument === "--minify") {
    minify = true;
  } else if (argument === "--sourcemap") {
    sourcemap = true;
  } else if (argument === "--watch") {
    watch = true;
  } else if (argument === "--esbuild") {
    esbuild = argv[++index] ?? usage("missing esbuild path");
  } else if (argument === "--external") {
    externals.add(argv[++index] ?? usage("missing external module"));
  } else if (argument === "-h" || argument === "--help") {
    usage();
  } else if (argument.startsWith("-")) {
    usage(`unknown option ${argument}`);
  } else if (entry === null) {
    entry = argument;
  } else {
    usage(`unexpected argument ${argument}`);
  }
}

if (!entry) {
  usage("missing entry TSX file");
}
if (!outfile) {
  usage("missing -o <output.js>");
}
if (!existsSync(entry)) {
  usage(`entry does not exist: ${entry}`);
}

function works(binary) {
  if (!binary) {
    return false;
  }
  const probe = spawnSync(binary, ["--version"], { stdio: "ignore" });
  return probe.status === 0;
}

function vendoredEsbuild() {
  const platform = [
    "win32",
    "darwin",
    "linux",
    "freebsd",
    "openbsd",
    "netbsd",
    "sunos",
    "aix",
  ].includes(process.platform)
    ? process.platform
    : null;
  if (platform === null) {
    return null;
  }
  const arch = [
    "x64",
    "arm64",
    "arm",
    "ia32",
    "ppc64",
    "s390x",
    "loong64",
    "riscv64",
    "mips64el",
  ].includes(process.arch)
    ? process.arch
    : null;
  if (arch === null) {
    return null;
  }
  const executable = process.platform === "win32" ? "esbuild.exe" : "esbuild";
  const binary = path.join(
    path.dirname(fileURLToPath(import.meta.url)),
    "..",
    "third_party",
    "esbuild",
    `${platform}-${arch}`,
    executable
  );
  return existsSync(binary) ? binary : null;
}

function locateEsbuild() {
  if (works(esbuild)) {
    return esbuild;
  }
  const vendored = vendoredEsbuild();
  if (works(vendored)) {
    return vendored;
  }
  if (works("esbuild")) {
    return "esbuild";
  }
  try {
    const require = createRequire(path.join(process.cwd(), "package.json"));
    const packageJson = require.resolve("esbuild/package.json");
    const binary = path.join(path.dirname(packageJson), "bin", "esbuild");
    if (works(binary)) {
      return binary;
    }
  } catch {
    // fall through
  }
  return null;
}

const binary = locateEsbuild();
if (!binary) {
  process.stderr.write(
    "huxerui-tsx: esbuild was not found. Install it (npm install --save-dev esbuild) " +
      "or set HUXERUI_ESBUILD to its binary.\n"
  );
  process.exit(2);
}

const args = [
  entry,
  "--bundle",
  "--format=esm",
  "--platform=neutral",
  "--jsx=automatic",
  "--jsx-import-source=@huxerui/tsx",
  "--target=es2020",
  `--outfile=${outfile}`,
  "--log-level=warning",
  "--external:@huxerui/tsx",
  "--external:@huxerui/tsx/jsx-runtime",
];

if (minify) {
  args.push("--minify");
}
if (sourcemap) {
  args.push("--sourcemap=inline");
}
if (watch) {
  args.push("--watch=forever");
}
for (const external of externals) {
  args.push(`--external:${external}`);
}

const result = spawnSync(binary, args, { stdio: "inherit" });
if (result.error) {
  process.stderr.write(`huxerui-tsx: ${result.error.message}\n`);
  process.exit(1);
}
process.exit(result.status ?? 1);
