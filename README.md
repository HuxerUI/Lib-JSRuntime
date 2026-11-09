# Lib-JSRuntime — TSX for HuxerUI

`Lib-JSRuntime` runs TSX as a first-class HuxerUI UI language. A TSX component becomes a HuxerUI
composition scope, every `useState` becomes a native state slot, and every host tag becomes a
HuxerUI `View`. The DSL is still HuxerUI's: `Column`, `Text`, `Spacer`, modifiers and native events;
TSX is only the authoring surface.

The library publishes **`HuxerUI::TSX`** (alias **`HuxerUI::JSRuntime`**) and the public header
`<huxerui/tsx.h>`. The TypeScript module `/app/main.js` imports runtime pieces from `@huxerui/tsx`,
which the native module loader serves from an embedded ES module.

```text
App.tsx ──esbuild──▶ app.js (ES module) ──QuickJS──▶ VNode tree
                                                        │
                                    HuxerUI Scope / State / View
```

- [Capabilities](#capabilities)
- [Integration](#integration)
- [TSX toolchain](#tsx-toolchain)
- [Minimal application](#minimal-application)
- [Runtime model](#runtime-model)
- [Layout hot reload](#layout-hot-reload)
- [Host elements](#host-elements)
- [Props and modifiers](#props-and-modifiers)
- [Hooks](#hooks)
- [Events](#events)
- [Modules and host functions](#modules-and-host-functions)
- [Limits](#limits)
- [Repository layout](#repository-layout)
- [Validation](#validation)

## Capabilities

| Capability | Entry points |
| --- | --- |
| Evaluate a TSX bundle and mount one root tree | `tsx::Runtime`, `RuntimeOptions`, `Runtime::Load()`, `Runtime::LoadFile()`, `Runtime::LoadConfiguredBundle()` |
| Layout hot reload (fresh state per revision) | `Runtime::Reload()`, `Runtime::ReloadFile()`, `Runtime::ReloadConfiguredBundle()` |
| Render into a HuxerUI application | `Runtime::Root()`, `tsx::TsxRoot(Runtime&)` |
| Function components with stable, positional state | `useState`, `useReducer`, `useRef`, `useMemo`, `useCallback` |
| Commit-time effects and cleanup | `useEffect`, `useLayoutEffect` backed by `huxerui::Lifecycle` |
| Native host elements | `Column`, `Row`, `Flow`, `Stack`, `Theme`, `Text`, `Button`, `IconButton`, `Image`, `Divider`, `Spacer`, `ScrollView`, `SelectionArea`, `TextField`, `Checkbox`, `RadioButton`, `Switch`, `Slider`, `ProgressBar`, `ProgressCircle`, `Chip`, `IndexedPages` |
| Native modifiers | padding, spacing, frame/width/height, background, foreground, border, corner radius, shadow, clip, enabled, focusable, main/cross alignment, align, grow, `onClick` |
| Event callbacks | `onClick` everywhere; `onChange` on toggles, sliders, chips, text fields; `onSubmit` on text fields |
| Host extension | `Runtime::RegisterFunction()`, `Runtime::SetModuleLoader()`, `Runtime::Evaluate()`, `Runtime::Invoke()` |
| Diagnostics | `RuntimeOptions::on_log`, `RuntimeOptions::on_error`, inline render error `Text` |

The bridge is single-threaded, like QuickJS and HuxerUI composition. Create the runtime, evaluate the
bundle, and compose views on the application thread.

## Integration

`Lib-JSRuntime` needs a HuxerUI SDK (or source tree). QuickJS and its quickjs-ng core are embedded
in this repository under [`quickjs/`](quickjs); no sibling `Lib-QuickJS` checkout is required.
Clone HuxerUI beside the application, then acquire the library target:

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::TSX
    PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Lib-JSRuntime"
)
```

QuickJS is compiled directly into `HuxerUI::TSX`; `HuxerUI::QuickJS` remains an alias for
compatibility.

Include the public header explicitly; it is not part of `<huxerui/huxerui.h>`:

```cpp
#include <huxerui/tsx.h>
```

## TSX toolchain

The library executes JavaScript; TypeScript and JSX are removed at build time. The repository
pins a standalone esbuild binary and CMake invokes it directly; **Node.js is not required**:

```sh
third_party/esbuild/linux-x64/esbuild src/App.tsx \
  --bundle --format=esm --platform=neutral --jsx=automatic \
  --jsx-import-source=@huxerui/tsx --target=es2020 \
  --external:@huxerui/tsx --external:@huxerui/tsx/jsx-runtime \
  --outfile=build/app.js
```

`huxerui_tsx_bundle()` emits the same command. The repository pins native esbuild 0.25.0
binaries for Linux x64/arm64/arm/ia32/ppc64/s390x/loong64/riscv64/mips64el, macOS x64/arm64,
Windows x64/arm64/ia32, FreeBSD/OpenBSD/NetBSD, SunOS, and AIX hosts. CMake selects the binary
from `third_party/esbuild/<platform>/` using `CMAKE_HOST_SYSTEM_NAME` and
`CMAKE_HOST_SYSTEM_PROCESSOR`; checksums are in `third_party/esbuild/SHA256SUMS`.

`HUXERUI_TSX_ESBUILD_PLATFORM` forces a platform id, and `HUXERUI_TSX_ESBUILD` points at an
arbitrary esbuild binary. The optional `tools/huxerui-tsx.mjs` wrapper remains for Node users and
falls back to the same vendored binaries.

The flags keep `@huxerui/tsx` external, because the native runtime serves it, and select the
automatic JSX runtime:

```text
--bundle --format=esm --platform=neutral --jsx=automatic
--jsx-import-source=@huxerui/tsx
--external:@huxerui/tsx --external:@huxerui/tsx/jsx-runtime
```

Recommended `tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "jsx": "react-jsx",
    "jsxImportSource": "@huxerui/tsx",
    "paths": {
      "@huxerui/tsx": ["../Lib-JSRuntime/types/index.d.ts"],
      "@huxerui/tsx/jsx-runtime": ["../Lib-JSRuntime/types/jsx-runtime.d.ts"]
    }
  }
}
```

Classic JSX (`"jsx": "react"`, `"jsxFactory": "h"`) is also supported; import `h` from
`@huxerui/tsx`.

### CMake helpers

`cmake/HuxerUITsx.cmake` bundles and embeds the module:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/../Lib-JSRuntime/cmake/HuxerUITsx.cmake")

huxerui_tsx_bundle("${CMAKE_CURRENT_BINARY_DIR}/app.js"
    ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/src/App.tsx"
)

huxerui_tsx_embed_header("${CMAKE_CURRENT_BINARY_DIR}/app_bundle.h"
    "${CMAKE_CURRENT_BINARY_DIR}/app.js"
    NAMESPACE counter
)

target_include_directories(my_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_sources(my_app PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/app.js"
    "${CMAKE_CURRENT_BINARY_DIR}/app_bundle.h"
)
```

`huxerui_tsx_bundle()` resolves esbuild through the `ESBUILD` argument, the
`HUXERUI_TSX_ESBUILD` cache variable, the `HUXERUI_ESBUILD` environment variable, `PATH`, then
the vendored `third_party/esbuild/<host>/esbuild`. Node.js is not involved.

## Minimal application

`src/App.tsx`:

```tsx
import { mount, Button, Column, Text, useState } from "@huxerui/tsx";

function App() {
  const [count, setCount] = useState(0);

  return (
    <Column padding={24} spacing={12} crossAlign="start">
      <Text role="title">Count: {count}</Text>
      <Button key="increment" onClick={() => setCount(count + 1)}>Increment</Button>
    </Column>
  );
}

mount(<App />);
```

`src/main.cpp`:

```cpp
#include <huxerui/huxerui.h>
#include <huxerui/tsx.h>

#include <string>

#include "app_bundle.h"

static huxerui::tsx::Runtime& Runtime() {
  static huxerui::tsx::Runtime runtime(huxerui::tsx::RuntimeOptions{
      .entry_name = "App.tsx",
      .bundle = std::string(app::kBundle),
      .on_error = [](std::string_view message) { /* log */ },
  });
  return runtime;
}

huxerui::View App() {
  return Runtime().Root();
}

const huxerui::Application application{
    App,
    {.window = {.title = "TSX Counter", .initial_size = {480.0F, 360.0F}}},
};

int main() {
  return huxerui::RunApplication();
}
```

`Runtime::Root()` auto-evaluates `RuntimeOptions::bundle` on first composition. For a file-backed
bundle, construct the runtime empty and call `LoadFile(path)` before the application starts.

## Runtime model

- A TSX **function component** is a `huxerui::Scope`. It re-runs when one of its own state slots
  commits; nested component scopes keep their identity and state.
- `useState` calls map, in order, to native `State<T>` slots owned by that scope. State values are
  JavaScript handles, so objects and arrays can be stored without copying.
- A **host tag** is a `View` declaration; modifiers are applied with the same `With(...)` API the C++
  DSL uses. Untouched host elements are re-created on each component render and reconciled by
  HuxerUI.
- `useEffect` refines a `huxerui::Lifecycle` registration. Dependency arrays are compared with
  JavaScript `===` / `Object.is`; omitted dependencies re-run on every render; `[]` runs once.
- Component `key` props and sibling keys are forwarded to `View::Key` for reconciliation.
- Returning an array from a component is supported by stacking the siblings in a `Column`. Direct
  children arrays (`{items.map(...)}`) splice into the parent layout and keep the parent's axis.

There is no separate signals engine: reads and writes go directly through HuxerUI state. That keeps
one source of truth and one recomposition scheduler.

## Layout hot reload

`Runtime::Reload()` is the deliberate state-reset path for dev loops:

```cpp
const auto result = runtime.ReloadFile("/path/to/app.js");
if (!result.Succeeded()) {
  // The previous tree stays mounted; show result.Error()
}
```

Each reload:

1. Evaluates the bundle under a revision-suffixed module name, so the ES module
   cache cannot return the previous instance.
2. Restores `__huxerui_root` and leaves the previous layout mounted if evaluation
   fails or the bundle does not call `mount()`.
3. Bumps a root revision state. The root `Scope` is keyed by that revision, so
   HUI unmounts the whole TSX subtree and mounts the new one.
4. Re-runs every component with fresh `useState`, `useRef`, and effects.

This is intentionally not state-preserving HMR. Layout, text, styles, structure,
and event handlers update; component state resets. For AI-driven iteration this
is the reliable mode: no stale hook graphs, no cross-revision value migration.

The [`examples/preview`](examples/preview) application wraps this in a small dev
host that polls the bundle's modification time and reloads on save.

Limits: call `Reload*` on the UI thread; bundle the application into one module
so old relative imports cannot stay cached; the QuickJS module cache retains old
revision module namespaces for the lifetime of the context, which is acceptable
for a dev preview but not for an unbounded production hot-update service.

## Host elements

| Tag | C++ view | Notable props |
| --- | --- | --- |
| `Column`, `Row`, `Flow`, `Stack` | same layout | children plus shared modifiers |
| `Theme` | `MaterialTheme` | wraps one child (multiple children become a `Column`) |
| `Text` | `Text` | `value`, `role`, `fontSize`, `fontWeight`, `fontFamily`, `italic`, `color`, `decoration`, `textAlign` |
| `Button` | `Button` | `label`, or text children; `onClick` |
| `IconButton` | `IconButton` | `src`, `label` |
| `Image` | `Image` | `src` (local file), `fit`, `tint` |
| `Divider` | `Divider` | `axis` |
| `Spacer` | `Spacer` | shared modifiers |
| `ScrollView` | `ScrollView` | `axis`; multiple children become a `Column` |
| `SelectionArea` | `SelectionArea` | one child or a `Column` |
| `TextField` | `TextField` | `value`, `label`, `placeholder`, `variant`, `multiline`, `maxLength`, `secure`, `onChange`, `onSubmit` |
| `Checkbox`, `RadioButton`, `Switch` | same control | `label`, `checked`/`selected`, `onChange(bool)` |
| `Slider` | `Slider` | `value`, `min`, `max`, `step`, `onChange(number)` |
| `ProgressBar`, `ProgressCircle` | same control | `progress`; omitted means indeterminate |
| `Chip` | `Chip` | `label`, `selected`, `onChange(bool)` |
| `IndexedPages` | `IndexedPages` | `selected` page index |

Text children are concatenated in order: `<Text>Count: {count}</Text>` produces `"Count: 3"`.

## Props and modifiers

Shared props map one-to-one onto HuxerUI modifiers:

| Prop | Modifier |
| --- | --- |
| `padding` | `Padding` (number, `[h, v]`, `[l, t, r, b]`, or `{ left, top, right, bottom }`) |
| `spacing` | `Spacing` |
| `frame` / `width` / `height` | `Frame` |
| `background` | `Background(VisualFill)` |
| `foreground` | `Foreground` |
| `border` | `Border` (color or `{ color, width }`) |
| `cornerRadius` | `CornerRadius` |
| `shadow` | `Shadow` (`{ color, offset: [x, y], blurRadius, spread }`) |
| `clipChildren` | `ClipChildren` |
| `enabled` / `focusable` | `Enabled` / `Focusable` |
| `mainAlign` / `crossAlign` | `MainAlign` / `CrossAlign` |
| `align` | `Align` (`"center"` or `{ horizontal, vertical }`) |
| `grow` | `Grow` (`true` or a factor) |

Colors accept `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb(...)`, `rgba(...)`, common names,
`0xRRGGBB`, `[r, g, b, a?]`, or `{ r, g, b, a? }`. Channels are 0–255; alpha is 0–1.

Unknown tags render an inline `Text("Unknown TSX element <...>")` diagnostic. Unknown props are
ignored.

## Hooks

```tsx
const [count, setCount] = useState(0);        // value or updater
const [state, dispatch] = useReducer(reducer, initial);
const ref = useRef(initial);                  // stable object, mutations do not recompose
const value = useMemo(factory, deps);         // compared with Object.is
const callback = useCallback(fn, deps);
useEffect(() => { return () => cleanup(); }, deps);
```

Rules match React: call hooks unconditionally and in the same order on every render. State is
positional within its component scope. A component that returns early still changes the hook order
and is a bug.

`useState` accepts a lazy initializer:

```tsx
const [data, setData] = useState(() => loadInitialData());
```

## Events

- `onClick` is available on every host element and maps to `ViewEvents::Click`.
- `onChange` maps to `ToggleEvents::Changed` (`bool`), `SliderEvents::Changed` (`number`), or
  `TextFieldEvents::Changed` (`string`).
- `onSubmit` maps to `TextFieldEvents::Submitted`.

Handlers run synchronously on the UI thread. A thrown exception is reported through
`RuntimeOptions::on_error` and does not unwind the HuxerUI event dispatch.

## Modules and host functions

`@huxerui/tsx` and `@huxerui/tsx/jsx-runtime` are built in. Other imports go through
`RuntimeOptions::module_root`, then `Runtime::SetModuleLoader()`:

```cpp
runtime.SetModuleLoader([](std::string_view name) -> huxerui::quickjs::Result<std::string> {
  if (name == "/assets/format.js") {
    return huxerui::quickjs::Result<std::string>::Success("export const pad = (v) => String(v).padStart(2, '0');");
  }
  return huxerui::quickjs::Result<std::string>::Failure(
      huxerui::quickjs::Error{huxerui::quickjs::ErrorCode::Module, "Unknown module"});
});
```

Expose application services to TSX:

```cpp
runtime.RegisterFunction("readClipboard", [](quickjs::Context& context, std::span<const quickjs::Value>) {
  return context.NewString(huxerui::Clipboard::ReadText()).Value();
});
```

Host globals are registered before bundle evaluation. Bundling is recommended so the application
does not need a JS module graph at runtime.

## Limits

- No React compatibility layer: no context, suspense, portals, refs to native views, or concurrent
  rendering. `useRef` stores a JavaScript value; it does not expose a host view.
- A component that returns several sibling elements is stacked in a `Column`; HuxerUI has no
  fragment view. Put sibling arrays directly inside a layout to keep its axis.
- `margin` is not translated; HuxerUI uses padding and spacing.
- Events currently cover click, toggle, slider, chip, and text-field changes. Pointer, key, focus,
  drag, and scroll callbacks are not forwarded yet.
- Images resolve local files through `ImageAsset::FromFile`; packaged resource IDs, network URLs,
  and vector assets are not exposed.
- `VirtualList`/`VirtualGrid`, `Tabs`, `SegmentedButton`, navigation, menus, dialogs, animation, and
  canvas are not bridged yet.
- The runtime owns the JavaScript context. It must outlive every `View` it produced; do not destroy
  it while the application is mounted.
- Startup cost is one QuickJS context plus module evaluation. Keep heavy work off the UI thread and
  avoid large object graphs in state.

## Repository layout

| Path | Contents |
| --- | --- |
| [`include/huxerui/tsx.h`](include/huxerui/tsx.h) | Public `Runtime`, `RuntimeOptions`, `TsxRoot()` contracts |
| [`src/runtime.cpp`](src/runtime.cpp) | Engine ownership, module loader, hooks, VNode traversal, error reporting |
| [`src/elements.cpp`](src/elements.cpp) | Host-tag to HuxerUI `View` builders and event forwarding |
| [`src/props.cpp`](src/props.cpp) | Prop parsing, colors, modifiers, keys |
| [`js/runtime.js`](js/runtime.js) | Embedded `@huxerui/tsx` module: JSX runtimes, hooks, host tags, `mount()` |
| [`quickjs/`](quickjs) | Embedded Lib-QuickJS wrapper and quickjs-ng `v0.18.0` engine sources |
| [`quickjs/tests`](quickjs/tests) | Embedded engine contract tests |
| [`types/`](types) | TypeScript element props and automatic-JSX declarations |
| [`tools/huxerui-tsx.mjs`](tools/huxerui-tsx.mjs) | Optional Node wrapper around esbuild; falls back to the vendored binary |
| [`tools/huxerui-tsx-dev.sh`](tools/huxerui-tsx-dev.sh) | POSIX one-command esbuild watcher + app launcher |
| [`third_party/esbuild`](third_party/esbuild) | Pinned standalone esbuild 0.25.0 binaries for all supported hosts, plus checksums |
| [`.github/workflows/ci.yml`](.github/workflows/ci.yml) | Cross-platform esbuild smoke CI |
| [`cmake/HuxerUITsx.cmake`](cmake/HuxerUITsx.cmake) | `huxerui_tsx_bundle()` and `huxerui_tsx_embed_header()` |
| [`examples/counter`](examples/counter) | Buildable TSX counter application |
| [`examples/preview`](examples/preview) | Live layout-preview host with file watching and `Runtime::ReloadFile()` |
| [`tests/`](tests) | Headless composition, state, nested-scope, effect, and compiled-TSX tests |

## Validation

Validated on Linux against the installed HuxerUI 0.4.0 SDK, with the QuickJS engine built from
the embedded `quickjs/` sources:

- Library and example configure and build (GCC 16.2.1, Ninja).
- `ctest` passes: embedded QuickJS engine tests, counter/updater state, nested component state
  preservation, effect run/cleanup, lazy initializers, memo/ref behavior, conditional and array
  children, layout hot reload (new layout, fresh state, failed-reload rollback), plus an optional
  end-to-end test that bundles the example TSX with the vendored esbuild, loads the emitted module
  through the runtime's module loader, and drives it with `huxerui::testing::UiTest`.
- The TypeScript declarations type-check the example and a hook/form API sample with `tsc --strict`
  (`jsx: react-jsx`, `jsxImportSource: @huxerui/tsx`).
- `.github/workflows/ci.yml` runs `ci/esbuild-smoke` on Linux x64/arm64, macOS x64/arm64, and
  Windows x64/arm64 to verify the vendored binary is selected and bundles TSX on each host.

Android, iOS, macOS, Windows, and Web builds were not exercised here.
