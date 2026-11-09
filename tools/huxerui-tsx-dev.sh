#!/bin/sh
# Start one TSX layout-preview session: an esbuild watcher and the native app.
# The app's built-in watcher reloads the bundle after each rebuild.
#
#   tools/huxerui-tsx-dev.sh \
#       --entry examples/preview/src/App.tsx \
#       --out examples/preview/build/app.js \
#       --app examples/preview/build/huxerui_tsx_preview
#
# Ctrl+C stops both processes. No Node.js is required.

set -u

usage() {
    if [ -n "${1:-}" ]; then
        echo "huxerui-tsx-dev: $1" >&2
    fi
    echo "usage: huxerui-tsx-dev.sh --entry <App.tsx> --out <app.js> --app <executable> [--esbuild <path>]" >&2
    exit 2
}

entry=
out=
app=
esbuild=${HUXERUI_ESBUILD:-}

while [ $# -gt 0 ]; do
    case "$1" in
        --entry) [ $# -ge 2 ] || usage "missing entry"; entry=$2; shift 2 ;;
        --out) [ $# -ge 2 ] || usage "missing output"; out=$2; shift 2 ;;
        --app) [ $# -ge 2 ] || usage "missing app"; app=$2; shift 2 ;;
        --esbuild) [ $# -ge 2 ] || usage "missing esbuild path"; esbuild=$2; shift 2 ;;
        -h|--help) usage ;;
        *) usage "unknown argument $1" ;;
    esac
done

[ -n "$entry" ] || usage "entry, output, and app are required"
[ -n "$out" ] || usage "entry, output, and app are required"
[ -n "$app" ] || usage "entry, output, and app are required"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "$esbuild" ]; then
    case "$(uname -s)" in
        Linux*) os=linux ;;
        Darwin*) os=darwin ;;
        FreeBSD*) os=freebsd ;;
        OpenBSD*) os=openbsd ;;
        NetBSD*) os=netbsd ;;
        SunOS*) os=sunos ;;
        AIX*) os=aix ;;
        *) os= ;;
    esac
    case "$(uname -m)" in
        x86_64|amd64) arch=x64 ;;
        aarch64|arm64) arch=arm64 ;;
        armv7l|armv6l|arm) arch=arm ;;
        i386|i486|i586|i686) arch=ia32 ;;
        ppc64le|ppc64|powerpc64|powerpc64le) arch=ppc64 ;;
        s390x) arch=s390x ;;
        loongarch64|loong64) arch=loong64 ;;
        riscv64) arch=riscv64 ;;
        mips64el) arch=mips64el ;;
        *) arch= ;;
    esac
    if [ -n "$os" ] && [ -n "$arch" ]; then
        esbuild="$root/third_party/esbuild/$os-$arch/esbuild"
    fi
fi
if [ ! -x "$esbuild" ]; then
    echo "huxerui-tsx-dev: esbuild is not executable: $esbuild" >&2
    exit 2
fi

# Resolve relative output against the caller's directory before changing
# anything, so the app and the watcher agree on one path.
case "$out" in
    /*) ;;
    *) out=$PWD/$out ;;
esac
mkdir -p "$(dirname "$out")"

bundle() {
    "$esbuild" "$entry" \
        --bundle \
        --format=esm \
        --platform=neutral \
        --jsx=automatic \
        --jsx-import-source=@huxerui/tsx \
        --target=es2020 \
        "--outfile=$out" \
        --log-level=warning \
        "--external:@huxerui/tsx" \
        "--external:@huxerui/tsx/jsx-runtime" \
        "$@"
}

# Ensure the app has a bundle before it starts; the watcher takes over after.
bundle || exit $?

# Start esbuild directly (not through a shell function) so $! is the esbuild
# PID and cleanup can terminate the actual watcher.
"$esbuild" "$entry" \
    --bundle \
    --format=esm \
    --platform=neutral \
    --jsx=automatic \
    --jsx-import-source=@huxerui/tsx \
    --target=es2020 \
    "--outfile=$out" \
    --log-level=warning \
    "--external:@huxerui/tsx" \
    "--external:@huxerui/tsx/jsx-runtime" \
    --watch=forever </dev/null &
watcher=$!

HUXERUI_TSX_BUNDLE=$out "$app" &
app_pid=$!

stopping=0
cleanup() {
    if [ "$stopping" -eq 1 ]; then
        return
    fi
    stopping=1
    kill -TERM "$watcher" "$app_pid" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
    wait "$app_pid" 2>/dev/null || true
    # esbuild can outlive SIGTERM while a watch build is in flight.
    kill -KILL "$watcher" "$app_pid" 2>/dev/null || true
}

trap 'cleanup; exit 0' INT TERM
trap 'cleanup' EXIT

wait "$app_pid"
status=$?
cleanup
exit "$status"
