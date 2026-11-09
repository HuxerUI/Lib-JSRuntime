# TSX Preview

A live layout preview for TSX components. The application loads `app.js` from
the build directory, watches its modification time, and calls
`huxerui::tsx::Runtime::ReloadFile()` whenever esbuild writes a new bundle.

This is **layout hot reload**, not state-preserving HMR: every reload remounts
the TSX tree with fresh `useState` / `useRef` / effects. That is intentional; it
makes layout iteration deterministic and avoids stale hook graphs.

## Build and run

One command starts the app, the initial bundle build, and the esbuild watcher.
Ctrl+C stops all of them. esbuild is vendored; Node.js is not required:

```sh
cmake -S . -B build -G Ninja \
  -DHUXERUI_HOME=$HOME/.local/share/HuxerUI
cmake --build build --target tsx_preview
```

Edit `src/App.tsx` while the window is open; esbuild rewrites `build/app.js`,
the app notices the new timestamp, and the window updates with the new layout
and reset state. The console prints `[tsx] reloaded ...` for each revision.

If you prefer two terminals, build and run `./build/huxerui_tsx_preview`, then
run `cmake --build build --target tsx_watch` in the other one.

The bundle path defaults to `build/app.js`; override it at run time with:

```sh
HUXERUI_TSX_BUNDLE=/tmp/app.js ./build/huxerui_tsx_preview
```

`HUXERUI_TSX_PREVIEW_ESBUILD` is optional. The default resolution order is
`HUXERUI_TSX_PREVIEW_ESBUILD`, `HUXERUI_ESBUILD`, `esbuild` on `PATH`, then the
pinned binary under `third_party/esbuild/`.

## What reloads

- component structure and children
- `padding`, `spacing`, `frame`, colors, borders, corner radii, alignment
- text, font size/weight, images
- event handlers

State is reset on every reload. If you need to inspect state behavior, use the
same component without editing the bundle; state works normally between
reloads.

## Headless layout feedback for AI

The same TSX can be mounted without a native window through
`huxerui::testing::UiTest`. A small tool can render the bundle and print
`UiSnapshot::ToString()` or specific `UiNodeInfo::bounds`, giving an AI agent
deterministic layout feedback without screenshots:

```cpp
Application app{App};
testing::UiTest ui(app, {.viewport = {390.0F, 844.0F}});
std::puts(ui.CaptureSnapshot().ToString().c_str());
```

`UiSnapshot::Diff()` reports moved, resized, inserted, and removed nodes between
two edits.

## Manual demo script

Build both the app and the initial bundle:

```sh
cd Lib-JSRuntime

cmake -S examples/preview -B examples/preview/build -G Ninja \
  -DHUXERUI_HOME=$HOME/.local/share/HuxerUI

cmake --build examples/preview/build -j8
```

The build uses the vendored esbuild binary. To use a different one, pass
`-DHUXERUI_TSX_PREVIEW_ESBUILD=/path/to/esbuild`; no `npm install` is needed.

Start the app and watcher together (Ctrl+C stops both):

```sh
cmake --build examples/preview/build --target tsx_preview
```

Alternatively run `./examples/preview/build/huxerui_tsx_preview` and
`cmake --build examples/preview/build --target tsx_watch` in two terminals.

### 1. See state working

Click `Activity` and `Settings` in the sidebar; the content title follows.
Click `Apply`; the title becomes `Overview ✓` (or `<tab> ✓`).

### 2. See layout hot reload

Edit `examples/preview/src/App.tsx` and save:

```diff
- <Row key="preview-root" padding={24} spacing={24} crossAlign="stretch">
+ <Row key="preview-root" padding={48} spacing={40} crossAlign="stretch">
```

Within about 250 ms the window reflows without restarting. The console prints:

```text
[tsx] reloaded .../examples/preview/build/app.js
```

Try changing the title text, `cornerRadius`, `background`, card `grow`, or adding a
new `Stat`; each edit remeasures the native layout.

### 3. See state reset

Click `Apply` so the title is `Overview ✓`, then save any TSX edit. The app
remounts with fresh hooks, so the title returns to `Overview`. This is expected
for layout-preview mode.

### 4. See failed-reload rollback

With the app running, overwrite the generated bundle with invalid JavaScript
(the watcher only rebuilds when the TSX source changes):

```sh
cp examples/preview/build/app.js /tmp/app.good.js
printf 'this is not javascript\n' > examples/preview/build/app.js
```

The app logs a reload error and keeps the previous layout on screen. Restore:

```sh
cp /tmp/app.good.js examples/preview/build/app.js
```

### 5. Run the headless checks

```sh
cmake -S . -B build -G Ninja \
  -DHUXERUI_HOME=$HOME/.local/share/HuxerUI \
  -DHUXERUI_TSX_BUILD_TESTS=ON

cmake --build build -j8
ctest --test-dir build --output-on-failure
```

To run the preview bundle through the headless layout harness explicitly:

```sh
HUXERUI_TSX_PREVIEW_BUNDLE=$PWD/examples/preview/build/app.js \
  ./build/tests/huxerui_tsx_tests
```

This checks rendering, `Apply` state, `ReloadFile()` remount, and layout
preservation for the same TSX the window uses.

### Notes

- If no window appears on a Wayland desktop, try `GDK_BACKEND=x11`.
- The app reads `build/app.js` by default. Override with
  `HUXERUI_TSX_BUNDLE=/path/to/app.js`.
- `tsx_watch` runs `esbuild --watch=forever`; stop it with `Ctrl+C`.
