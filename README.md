# Lib-JSRuntime — TSX for HuxerUI

[English](README.md) | [简体中文](README.zh-CN.md)

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
- [Events and timers](#events-and-timers)
- [Modules and host functions](#modules-and-host-functions)
- [Limits](#limits)
- [Repository layout](#repository-layout)
- [Validation](#validation)
- [Agent skill](skills/lib-jsruntime/SKILL.md)
- [Showcase](examples/showcase)

## Capabilities

| Capability | Entry points |
| --- | --- |
| Evaluate a TSX bundle and mount one root tree | `tsx::Runtime`, `RuntimeOptions`, `Runtime::Load()`, `Runtime::LoadFile()`, `Runtime::LoadConfiguredBundle()` |
| Layout hot reload (fresh state per revision) | `Runtime::Reload()`, `Runtime::ReloadFile()`, `Runtime::ReloadConfiguredBundle()` |
| Render into a HuxerUI application | `Runtime::Root()`, `tsx::TsxRoot(Runtime&)` |
| Function components with stable, positional state | `useState`, `useReducer`, `useRef`, `useMemo`, `useCallback` |
| Commit-time effects and cleanup | `useEffect`, `useLayoutEffect` backed by `huxerui::Lifecycle` |
| Native host elements | `Column`, `Row`, `Flow`, `Stack`, `Theme`, `Text`, `Button`, `IconButton`, `Image`, `Divider`, `Spacer`, `ScrollView`, `SelectionArea`, `TextField`, `Checkbox`, `RadioButton`, `Switch`, `Slider`, `ProgressBar`, `ProgressCircle`, `Chip`, `VirtualList`, `VirtualGrid`, `Tabs`, `SegmentedButton`, `NavigationPane`, `NavigationBar`, `IndexedPages` |
| Native modifiers | padding, spacing, frame/width/height, background, foreground, border, corner radius, shadow, clip, enabled, focusable, main/cross alignment, align, grow, `onClick` |
| Event callbacks | `onClick`; pointer down/move/up/cancel/intercept; hover enter/move/leave; scroll; key down/up/intercept; focus/blur; context menu; Back; `onChange`/`onSubmit` on form controls |
| Host extension | `Runtime::RegisterFunction()`, `Runtime::SetModuleLoader()`, `Runtime::Evaluate()`, `Runtime::Invoke()` |
| Host services | `fetch()`, `readClipboard()`/`writeClipboard()`/`clipboardAvailable()`, `applicationLifecycle()`, `quitApplication()` |
| Diagnostics | `RuntimeOptions::on_diagnostic`, `on_log`, `on_error`, `Runtime::LastDiagnostic()`, inline render error `Text` |
| Execution budgets | `RuntimeOptions::evaluation_time_limit`, `operation_time_limit`, `should_interrupt`, `stack_limit_bytes` |
| Timers | `setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`, runtime `queueMicrotask` |
| Presentation | `useToast()`, `useDialog()`, `useMenu()`, `useSnackBar()`, `useBottomSheet()`, `usePopup()` |

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
  --sourcemap=inline --sources-content=false \
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
--sourcemap=inline --sources-content=false
```

`--sourcemap=inline` embeds a source map in the bundle; the runtime uses it to report diagnostics
against the original `.tsx` file. `--sources-content=false` keeps the embedded map small by
omitting source text. Bundles without an inline map remain supported and report generated
positions.

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
| `Theme` | `MaterialTheme` / `MaterialDarkTheme` / `FlatTheme` / `FlatDarkTheme` | `variant` (`material`, `materialDark`, `flat`, `flatDark`) or `dark`; wraps one child (multiple children become a `Column`) |
| `Text` | `Text` | `value` (string or `{ resource: "domain:key" }`), `role`, `fontSize`, `fontWeight`, `fontFamily`, `italic`, `color`, `decoration`, `textAlign` |
| `Button` | `Button` | `label` (string or `{ resource }`), or text children; `onClick` |
| `IconButton` | `IconButton` | `src` (local file or `{ resource }`), `label` |
| `Image` | `Image` | `src` (local file, `{ file }`, or `{ resource: "domain:key" }`), `fit`, `tint` |
| `Divider` | `Divider` | `axis` |
| `Spacer` | `Spacer` | shared modifiers |
| `ScrollView` | `ScrollView` | `axis`, `scrollRef`; multiple children become a `Column` |
| `SelectionArea` | `SelectionArea` | one child or a `Column` |
| `TextField` | `TextField` | `value`, `selection`, `validation`, `label`, `placeholder`, `variant`, `multiline`, `maxLength`, `secure`, `onChange`, `onEdit`, `onSubmit`, `ref` |
| `Checkbox`, `RadioButton`, `Switch` | same control | `label`, `checked`/`selected`, `onChange(bool)` |
| `Slider` | `Slider` | `value`, `min`, `max`, `step`, `onChange(number)` |
| `ProgressBar`, `ProgressCircle` | same control | `progress`; omitted means indeterminate |
| `Chip` | `Chip` | `label`, `selected`, `onChange(bool)` |
| `VirtualList` | `VirtualList` | `items`, `renderItem`, `itemExtent`, `estimatedItemExtent`, `cacheExtent`, `axis`, `scrollRef` |
| `VirtualGrid` | `VirtualGrid` | `items`, `renderItem`, `columns`/`adaptiveColumnWidth`, `rowExtent`, `estimatedRowExtent`, spacing, `itemSpans`, `scrollRef` |
| `Tabs` | `Tabs` | `labels`, `selected`, `onChange(number)` |
| `SegmentedButton` | `SegmentedButton` | `labels`, `selected`, `onChange(number)` |
| `NavigationPane` | `NavigationPane` | `labels`, `selected`, `expanded`, `onChange(number)` |
| `NavigationBar` | `NavigationBar` | `items: { label, src, selectedSrc?, disabled? }[]` (`src` accepts local files or `{ resource }`), `selected`, `onChange(number)` |
| `IndexedPages` | `IndexedPages` | `selected` page index |

Text children are concatenated in order: `<Text>Count: {count}</Text>` produces `"Count: 3"`.
`Text`, `Button`, and navigation labels also accept `{ resource: "domain:key" }`; the mounted view
resolves the packaged string in its effective `Locale`. Image sources accept a local path, `{ file }`,
or `{ resource }`; packaged raster and compiled-vector image keys are resolved by HuxerUI at mount.

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
| `tooltip` | `Tooltip` (string message) |

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

const field = useFocusRef();
return <TextField ref={field} value={value} onChange={setValue} />;
// field.requestFocus() requests native keyboard focus for the bound control.

const scroll = useScrollRef();
return <ScrollView scrollRef={scroll}>{content}</ScrollView>;
// scroll.scrollTo(120), scroll.scrollBy(40), scroll.offset(), scroll.metrics()

const toast = useToast();
toast.show("Changes saved", 2);

const dialog = useDialog();
dialog.show({
  title: "Delete?",
  message: "This cannot be undone.",
  positive: "Delete",
  negative: "Cancel",
  onPositive: removeItem,
});

dialog.showContent(() => <Text>Custom TSX dialog content</Text>);

const snackBar = useSnackBar();
snackBar.showAction("Deleted", "Undo", restoreItem, 4);

const sheet = useBottomSheet();
sheet.show(() => <Text>Bottom sheet content</Text>);

const popup = usePopup();
<Button popupAnchor={popup} onClick={() => popup.show(() => <Text>Popup</Text>)} />;

const menu = useMenu();
return (
  <Button menuAnchor={menu} onClick={() => menu.show([
    { label: "Rename", onSelect: renameItem },
    { section: true },
    { label: "Delete", disabled: true },
  ])}>
    Actions
  </Button>
);

// Coordinate mode is also available:
// menu.showAt(120, 80, [{ label: "Rename", onSelect: renameItem }]);
```

Rules match React: call hooks unconditionally and in the same order on every render. State is
positional within its component scope. A component that returns early still changes the hook order
and is a bug.

`useState` accepts a lazy initializer:

```tsx
const [data, setData] = useState(() => loadInitialData());
```

## Events and timers

Shared event props map to HuxerUI typed events:

| TSX prop | HuxerUI event | JS payload / return |
| --- | --- | --- |
| `onClick` | `ViewEvents::Click` | no payload |
| `onPointer`, `onPointerDown`, `onPointerMove`, `onPointerUp`, `onPointerCancel` | `ViewEvents::Pointer` | `PointerInputEvent` |
| `onPointerIntercept` | `ViewEvents::PointerIntercept` | `PointerInputEvent`; return `true` to take ownership |
| `onHover`, `onPointerEnter`, `onPointerLeave` | `ViewEvents::Hover` | `HoverInputEvent` |
| `onScroll` | `ViewEvents::ScrollInput` | `ScrollInputEvent`; return `true` to consume |
| `onKeyDown`, `onKeyUp` | `ViewEvents::KeyDown` / `KeyUp` | `KeyInputEvent`; return `true` to consume |
| `onKeyIntercept` | `ViewEvents::KeyIntercept` | `KeyInputEvent`; return `true` to consume before the focused view |
| `onFocus`, `onBlur`, `onFocusChanged` | `ViewEvents::FocusChanged` | optional `boolean` for `onFocusChanged` |
| `onContextMenu` | `ViewEvents::ContextMenuRequested` | `{ x, y }` |
| `onBack` | `ViewEvents::BackRequested` | no payload |
| `onDragStart`, `onDrag`, `onDragEnd`, `onDragCancel` | native `DragGesture` + `DragEvents` | `DragInputEvent`; `dragAxis`, `dragThreshold`, `dragDelay` configure recognition |
| `onLongPressStart`, `onLongPressEnd`, `onLongPressCancel` | native `LongPressGesture` + `LongPressEvents` | `LongPressInputEvent`; `longPressDuration`, `longPressMovement` configure recognition |
| `onMultiTap` | native `MultiTapGesture` + `MultiTapEvents` | `MultiTapInputEvent`; `multiTapCount`, `multiTapInterval`, `multiTapMovement` configure recognition |

`onChange` maps to `ToggleEvents::Changed` (`bool`), `SliderEvents::Changed` (`number`), or
`TextFieldEvents::Changed` (`string`). `onSubmit` maps to `TextFieldEvents::Submitted`.

Input events follow HuxerUI routing, not DOM bubbling: pointer/key/scroll are direct to the target
or focused view, and only the handlers with a `bool` result consume or intercept. There is no
`stopPropagation()`/`preventDefault()` object; return `true` from the relevant handler.

Handlers run synchronously on the UI thread. A thrown exception is reported as a structured
`DiagnosticPhase::Event` diagnostic and does not unwind the HuxerUI event dispatch.

Timers use the application's monotonic timer through HuxerUI `TaskScope`/`Delay`; callbacks run on
the UI thread. `setTimeout`/`setInterval` return a numeric `TimerHandle`; clear it with
`clearTimeout`/`clearInterval`. Timers are cancelled automatically when the runtime is destroyed.
They require an active HuxerUI application and should be scheduled from an effect, event, or timer
callback rather than during bundle evaluation. `requestAnimationFrame` and `fetch` are not provided
yet.

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

## Host services

The embedded module exposes the running HuxerUI application's standard services. They require an
initialized application on the application thread; without one, clipboard reads return `null`,
writes return `false`, lifecycle reports `"unknown"`, and `fetch()` rejects.

```tsx
import {
  fetch, clipboardAvailable, writeClipboard, applicationLifecycle,
} from "@huxerui/tsx";

useEffect(() => {
  fetch("https://example.com/data.json", {timeout: 5000})
    .then((response) => {
      // response: { url, status, headers, body, byteLength }
      setData(JSON.parse(response.body));
    })
    .catch((error) => setError(String(error?.message ?? error)));
}, []);
```

`fetch(url, options?)` returns `Promise<{ url, status, headers, body, byteLength }>`. HTTP 4xx and
5xx responses resolve normally; transport, timeout, unsupported transport, and argument failures
reject with `{ code: "transport" | "timeout" | "unsupported", message }` or a validation string.

| Option | Meaning |
| --- | --- |
| `method` | `GET` (default), `HEAD`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS` |
| `headers` | `Record<string, string>` |
| `body` | UTF-8 request body string |
| `timeout` | Complete request deadline in milliseconds; default 30000 |

`readClipboard()`, `writeClipboard(text)`, and `clipboardAvailable()` are synchronous
application-thread calls; the `clipboard` object groups them. `applicationLifecycle()` reports
`"active"`, `"inactive"`, `"background"`, or `"unknown"`. `quitApplication()` requests orderly
shutdown. General asynchronous host I/O still goes through `Runtime::RegisterFunction()`.

## Diagnostics and execution budgets

Every bridge failure is normalized into `tsx::Diagnostic` before it reaches `on_log` or `on_error`:

```cpp
tsx::RuntimeOptions options{
    .entry_name = "app.js",
    .evaluation_time_limit = std::chrono::seconds(2),
    .operation_time_limit = std::chrono::milliseconds(50),
    .on_diagnostic = [](const tsx::Diagnostic& diagnostic) {
      std::fprintf(stderr, "%s\n", diagnostic.description.c_str());
    },
    .on_error = [](std::string_view text) {
      std::fprintf(stderr, "%s\n", std::string(text).c_str());
    },
};
```

`Diagnostic` carries `phase`, `code`, `operation`, `message`, `filename`, `line`, `column`,
`generated_filename`, `generated_line`, `generated_column`, `stack`, `component_stack`, and
`description`. When the bundle embeds an inline source map, `filename`, `line`, and `column`
point at the original `.tsx` source, `stack` frames from the bundle are remapped, and the
`generated_*` fields preserve the bundle location. Without a source map, the mapped fields equal
the generated fields. The description is formatted as
`phase [ErrorCode]: operation: message (file:line:column)` followed by the JavaScript stack and
`component stack: App > Child`. `Runtime::LastDiagnostic()` returns the most recent one, which is
useful for tests and error overlays.

- `evaluation_time_limit` bounds one module load/reload or `Runtime::Evaluate()` call.
- `operation_time_limit` bounds one synchronous component render, hook initializer, effect, event
  handler, timer callback, or `Runtime::Invoke()` call. It is enforced by the QuickJS interrupt
  handler, so a runaway `while (true)` becomes `ErrorCode::Interrupted`.
- `should_interrupt` is a lower-level cooperative probe for all JavaScript execution.
- `stack_limit_bytes` and `memory_limit_bytes` are forwarded to QuickJS.

`on_diagnostic` runs before `on_log` and `on_error`; exceptions thrown by diagnostic callbacks are
contained so they cannot mask the original JavaScript failure.

## Limits

- No React compatibility layer: no context, suspense, portals, general native-view refs, or concurrent
  rendering. `useRef` stores a JavaScript value; `useFocusRef` exposes focus for one bound focusable
  host element, and `useScrollRef` exposes a controller for one bound `ScrollView`.
- A component that returns several sibling elements is stacked in a `Column`; HuxerUI has no
  fragment view. Put sibling arrays directly inside a layout to keep its axis.
- `margin` is not translated; HuxerUI uses padding and spacing.
- Events cover click, pointer, hover, scroll, key, focus, context menu, Back, drag, long press, and
  multi tap, plus toggle, slider, chip, and text-field changes.
- Timers cover `setTimeout`, `setInterval`, and `queueMicrotask`; `requestAnimationFrame` and
  general async host I/O beyond `fetch()` are not exposed. `fetch()` buffers the complete response
  body and exposes UTF-8 text; streaming and binary payloads are not exposed.
- Images resolve local files and packaged image resource keys; raw SVG text, network URLs, and
  arbitrary programmatic vector construction are not exposed through TSX props.
- Routed navigation, animation, and canvas are not bridged yet.
- `VirtualList::renderItem` / `VirtualGrid::renderItem` are declarative callbacks that run during
  virtual measurement; they must not call hooks. Use a function component inside the returned child
  for hook state.
- The runtime owns the JavaScript context. It must outlive every `View` it produced; do not destroy
  it while the application is mounted.
- Function components are scoped by function identity plus sibling occurrence; use explicit `key`
  values for reordered lists.
- Startup cost is one QuickJS context plus module evaluation. Keep heavy work off the UI thread and
  avoid large object graphs in state.
- Source-map resolution reads inline maps only. External `.map` files and indexed maps (`sections`)
  are not loaded; bundle with `--sourcemap=inline` to map diagnostics.

## Repository layout

| Path | Contents |
| --- | --- |
| [`include/huxerui/tsx.h`](include/huxerui/tsx.h) | Public `Runtime`, `RuntimeOptions`, `TsxRoot()` contracts |
| [`src/runtime.cpp`](src/runtime.cpp) | Engine ownership, module loader, hooks, VNode traversal, error reporting |
| [`src/elements.cpp`](src/elements.cpp) | Host-tag to HuxerUI `View` builders and event forwarding |
| [`src/props.cpp`](src/props.cpp) | Prop parsing, colors, modifiers, keys |
| [`src/source_map.cpp`](src/source_map.cpp) | Inline source-map parsing and diagnostic/stack back-mapping |
| [`js/runtime.js`](js/runtime.js) | Embedded `@huxerui/tsx` module: JSX runtimes, hooks, host tags, `mount()` |
| [`quickjs/`](quickjs) | Embedded Lib-QuickJS wrapper and quickjs-ng `v0.18.0` engine sources |
| [`quickjs/tests`](quickjs/tests) | Embedded engine contract tests |
| [`types/`](types) | TypeScript element props and automatic-JSX declarations |
| [`tools/huxerui-tsx.mjs`](tools/huxerui-tsx.mjs) | Optional Node wrapper around esbuild; falls back to the vendored binary |
| [`tools/huxerui-tsx-dev.sh`](tools/huxerui-tsx-dev.sh) | POSIX one-command esbuild watcher + app launcher |
| [`third_party/esbuild`](third_party/esbuild) | Pinned standalone esbuild 0.25.0 binaries for all supported hosts, plus checksums |
| [`.github/workflows/ci.yml`](.github/workflows/ci.yml) | Cross-platform esbuild smoke plus native build/test CI |
| [`cmake/HuxerUITsx.cmake`](cmake/HuxerUITsx.cmake) | `huxerui_tsx_bundle()` and `huxerui_tsx_embed_header()` |
| [`examples/counter`](examples/counter) | Buildable TSX counter application |
| [`examples/showcase`](examples/showcase) | Feature-complete TSX showcase with one-command preview |
| [`skills/lib-jsruntime`](skills/lib-jsruntime/SKILL.md) | Agent-facing usage skill and correctness checklist |
| [`examples/preview`](examples/preview) | Live layout-preview host with file watching and `Runtime::ReloadFile()` |
| [`tests/`](tests) | Headless composition, state, nested-scope, effect, compiled-TSX, and source-map tests |

## Validation

Validated on Linux against the installed HuxerUI 0.4.0 SDK, with the QuickJS engine built from
the embedded `quickjs/` sources:

- Library and example configure and build (GCC 16.2.1, Ninja).
- `ctest` passes: embedded QuickJS engine tests, counter/updater state, nested component state
  preservation, effect run/cleanup, lazy initializers, memo/ref behavior, conditional and array
  children, layout hot reload (new layout, fresh state, failed-reload rollback), structured
  render/event/effect/timer diagnostics with component stacks, timer callbacks, execution budgets,
  plus end-to-end tests that bundle TSX with the vendored esbuild, load the emitted modules through
  the runtime, and drive them with `huxerui::testing::UiTest`. A compiled fixture also verifies
  source-map back-mapping of the primary location, stack frames, and formatted description.
- `examples/showcase` builds and its `tsconfig.json` passes `tsc --strict` (`jsx: react-jsx`,
  `jsxImportSource: @huxerui/tsx`).
- CI also bundles `examples/showcase/src/App.tsx` and verifies the showcase artifact on every
  supported host.
- `.github/workflows/ci.yml` runs `ci/esbuild-smoke` on Linux x64/arm64, macOS x64/arm64, and
  Windows x64/arm64 to verify the vendored binary is selected and bundles TSX on each host. It also
  configures, builds, and runs the test suite against a pinned HuxerUI source checkout on Ubuntu,
  Windows, and macOS.

Local validation was Linux-only; Android, iOS, and Web builds are not exercised here.

## License

MIT. See [LICENSE](LICENSE).
