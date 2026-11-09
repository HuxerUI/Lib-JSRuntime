# Vendored esbuild

This directory pins standalone esbuild **0.25.0** binaries so CMake can invoke the compiler
directly without Node or npm.

- Source: npm packages `@esbuild/<platform>@0.25.0`
- License: MIT (see `LICENSE.md`)
- Checksums: `SHA256SUMS`

## Platforms

| Host | Platform id |
| --- | --- |
| Linux x64 | `linux-x64` |
| Linux arm64 | `linux-arm64` |
| Linux armv7 | `linux-arm` |
| Linux ia32 | `linux-ia32` |
| Linux ppc64 | `linux-ppc64` |
| Linux s390x | `linux-s390x` |
| Linux loong64 | `linux-loong64` |
| Linux riscv64 | `linux-riscv64` |
| Linux mips64el | `linux-mips64el` |
| macOS x64 | `darwin-x64` |
| macOS arm64 | `darwin-arm64` |
| Windows x64 | `win32-x64` |
| Windows arm64 | `win32-arm64` |
| Windows ia32 | `win32-ia32` |
| FreeBSD x64 | `freebsd-x64` |
| FreeBSD arm64 | `freebsd-arm64` |
| OpenBSD x64 | `openbsd-x64` |
| OpenBSD arm64 | `openbsd-arm64` |
| NetBSD x64 | `netbsd-x64` |
| NetBSD arm64 | `netbsd-arm64` |
| SunOS x64 | `sunos-x64` |
| AIX ppc64 | `aix-ppc64` |

Android is not listed: `@esbuild/android-*` packages are Node/WebAssembly shims, not native
esbuild executables. Use `HUXERUI_TSX_ESBUILD` for an Android-host workaround.

## Resolution

`cmake/HuxerUITsx.cmake` selects `third_party/esbuild/<platform>/esbuild` (or `esbuild.exe` on
Windows) from the build host. Override with:

- `-DHUXERUI_TSX_ESBUILD_PLATFORM=<platform-id>`
- `-DHUXERUI_TSX_ESBUILD=/path/to/esbuild`

The binary is a build tool only. It is not linked into `libhuxerui_tsx`, and it never runs inside
QuickJS.
