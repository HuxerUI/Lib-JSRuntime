# Vendored QuickJS

This directory contains the QuickJS engine sources used by Lib-QuickJS.

| Field | Value |
| --- | --- |
| Project | [quickjs-ng](https://github.com/quickjs-ng/quickjs) |
| Version | `v0.18.0` (`d7215b0ad3fc2c562e8317fd9bca058fd4863804`, 2026-11-01) |
| License | MIT; see `LICENSE` in this directory |

Only the core engine is vendored. `quickjs-libc`, the `qjs` CLI, the bytecode compiler, tests, and the
test262 runner are not part of this library.

Files:

- `quickjs.c`, `quickjs.h`, `quickjs-atom.h`, `quickjs-opcode.h`, `quickjs-c-atomics.h`
- `cutils.h`, `list.h`
- `dtoa.c`, `dtoa.h`
- `libregexp.c`, `libregexp.h`, `libregexp-opcode.h`
- `libunicode.c`, `libunicode.h`, `libunicode-table.h`
- Generated builtins included by `quickjs.c`: `builtin-array-fromasync.h`, `builtin-iterator-zip.h`,
  `builtin-iterator-zip-keyed.h`

Upstream copyright notices remain in the source files. The MIT license in this directory covers the vendored
engine as published by quickjs-ng.

Upstream is not patched. Update by replacing these files from a tagged quickjs-ng release and rerunning the
Lib-QuickJS tests.
