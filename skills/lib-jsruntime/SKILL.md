---
name: lib-jsruntime
description: Build, debug, and iterate HuxerUI screens written in TSX with Lib-JSRuntime. Use when a task involves @huxerui/tsx, TSX/JSX UI, native HuxerUI events, refs, presentation APIs, or the esbuild + QuickJS toolchain.
---

# Lib-JSRuntime

Use this skill when authoring or debugging a HuxerUI interface in TSX.

## What this is

- `HuxerUI::TSX` embeds QuickJS and runs an esbuild-produced ES module.
- TSX function component -> HuxerUI `Scope`.
- `useState` -> native HuxerUI state slot.
- Host tag -> native HuxerUI `View`.
- The UI is **not** React. No virtual DOM, no React context/suspense/portals.

## Repository map

| Path | Purpose |
| --- | --- |
| `include/huxerui/tsx.h` | C++ `Runtime`, `RuntimeOptions`, diagnostics |
| `js/runtime.js` | embedded `@huxerui/tsx` JS module |
| `types/index.d.ts` | TSX authoring surface |
| `src/elements.cpp` | host tags -> HUX views |
| `src/source_map.cpp` | inline source-map diagnostic remapping |
| `src/events.cpp` | native events -> JS event objects |
| `examples/showcase/src/App.tsx` | complete feature showcase |
| `examples/preview` | one-command live preview |
| `cmake/HuxerUITsx.cmake` | esbuild/bundle helpers |

## Minimal application

```tsx
import { mount, Button, Column, Text, useState } from "@huxerui/tsx";

function App() {
  const [count, setCount] = useState(0);
  return (
    <Column padding={24} spacing={12}>
      <Text role="title">Count: {count}</Text>
      <Button onClick={() => setCount((value) => value + 1)}>Increment</Button>
    </Column>
  );
}

mount(<App />);
```

C++ host:

```cpp
huxerui::tsx::Runtime runtime(huxerui::tsx::RuntimeOptions{
    .entry_name = "app.js",
    .bundle = bundled_source,
    .on_error = [](std::string_view message) { /* log */ },
});
```

`bundle` may be omitted when using `LoadFile()`. `Runtime::Root()` returns the mounted root
`View`; the runtime must outlive every `View` it produced.

## Host tags

`Column`, `Row`, `Flow`, `Stack`, `Theme`, `Text`, `Button`, `IconButton`, `Image`, `Select`,
`ComboBox`, `DatePicker`, `TimePicker`, `RefreshBox`, `Pager`, `SplitLayout`, `TopAppBar`,
`Divider`, `Spacer`, `ScrollView`, `SelectionArea`, `TextField`, `Checkbox`, `RadioButton`,
`Switch`, `Slider`, `ProgressBar`, `ProgressCircle`, `Chip`, `VirtualList`, `VirtualGrid`, `Tabs`,
`SegmentedButton`, `NavigationPane`, `NavigationBar`, `IndexedPages`.

`Select`, `ComboBox`, `DatePicker`, and `TimePicker` require the HuxerUI framework resource package.
Every host element accepts a `semantics` object (role, label, hint, state flags, identifier) and
`fileDrop` (`true` or filter options) with `onFileDrop`/enter/move/exit/failed callbacks.

`Theme` selects `material`, `materialDark`, `flat`, or `flatDark` with `variant` (or `dark`).
Text/button/navigation labels accept `{ resource: "domain:key" }`; image `src` accepts a local
path, `{ file }`, or `{ resource }`. Packaged strings resolve in the mounted `Locale`.

`padding`, `spacing`, `frame`, `width`, `height`, `grow`, `background`, `foreground`, `border`,
`cornerRadius`, `shadow`, `clipChildren`, `enabled`, `focusable`, `tooltip`, `align`,
`mainAlign`, `crossAlign` are shared. `margin` is unsupported.

## Hooks

`useState`, `useReducer`, `useRef`, `useMemo`, `useCallback`, `useEffect`, `useLayoutEffect`,
`useFocusRef`, `useScrollRef`.

Hooks are positional. Call them unconditionally, in the same order, on every render. Do not call
hooks inside `VirtualList` / `VirtualGrid` `renderItem`; return a function component there if state
is required.

## Events

`onClick`; pointer (`onPointer`, `onPointerDown/Move/Up/Cancel`, `onPointerIntercept`); hover
(`onHover`, `onPointerEnter/Leave`); `onScroll`; key (`onKeyDown/Up/Intercept`); focus
(`onFocus`, `onBlur`, `onFocusChanged`); `onContextMenu`; `onBack`; drag
(`onDragStart/Drag/DragEnd/DragCancel`); long press (`onLongPressStart/End/Cancel`); multi tap
(`onMultiTap`).

HuxerUI routing is direct, not DOM bubbling. Return `true` from `onPointerIntercept`, `onScroll`,
or key handlers to consume/intercept. There is no `stopPropagation()` / `preventDefault()`.

## Refs and presentation

```tsx
const focus = useFocusRef();
<TextField ref={focus} value={text} onChange={setText} />;

const scroll = useScrollRef();
<VirtualList items={items} scrollRef={scroll} renderItem={(item, i) => <Text key={i}>{item}</Text>} />;

const toast = useToast();
toast.show("Saved");

const dialog = useDialog();
dialog.show({ title: "Delete?", message: "Cannot undo", positive: "Delete" });

const menu = useMenu();
<Button menuAnchor={menu} onClick={() => menu.show([{ label: "Rename", onSelect: rename }])} />;

const popup = usePopup();
<Button popupAnchor={popup} onClick={() => popup.show(() => <Text>Popup</Text>)} />;

const snack = useSnackBar();
snack.showAction("Deleted", "Undo", restore);

const sheet = useBottomSheet();
sheet.show(() => <Text>Sheet content</Text>);
```

Custom presentation content factories run outside the declaring component's hook scope. They must
not call hooks; return a function component instead.

## Navigation

```tsx
const navigation = useNavigation(() => <Home navigation={navigation} />);
return <NavigationStack navigation={navigation} />;
```

`push(page)`, `pop()`, `replace(page)`, `reset(page?)`; `depth`/`canPop`/`entries()`/`current()`.
`NavigationStack` keeps every retained page mounted (state survives) and maps Back to `pop()`.
This is a TSX page stack, not the native routed `NavigationStack`.

## Animation and canvas

- `opacity`, `scale`, and `rotation` accept a number or
  `{ to, duration(ms), easing: "linear"|"easeIn"|"easeOut"|"easeInOut", spring: { stiffness, dampingRatio }, delay(ms), iterations, repeat: "restart"|"reverse" }`.
- `offset` accepts `[x, y]`, `{ x, y }`, or `{ to: [x, y], ... }`; `transformOrigin` sets the
  scale/rotation pivot fractions.
- `<Canvas onPaint={(ctx, size) => ctx.fillRect(...)}/>` supports `fillRect`, `strokeRect`, `line`,
  `circle`, `arc`, `pushClipRect`/`popClip`, and `pushTranslate`/`popTransform`.

## Host services

- `fetch(url, options?)` returns `Promise<{ url, status, headers, body, byteLength }>`; transport,
  timeout, and unsupported transport reject with `{ code, message }`.
- `readClipboard()`, `writeClipboard(text)`, `clipboardAvailable()`, `applicationLifecycle()`, and
  `quitApplication()` are synchronous application-thread services.
- All of them require a live HuxerUI application; outside one, reads return `null`, writes return
  `false`, lifecycle reports `"unknown"`, and `fetch` rejects.

## Toolchain

- CMake: `huxerui_tsx_bundle`, `huxerui_tsx_embed_header`, `huxerui_use_library`.
- One-command preview: `cmake --build <build> --target tsx_preview`.
- Direct esbuild needs `--jsx=automatic --jsx-import-source=@huxerui/tsx`.
- Externalize `@huxerui/tsx` and `@huxerui/tsx/jsx-runtime` when the runtime loader will serve them.
- `huxerui_tsx_bundle` adds `--sourcemap=inline --sources-content=false`; direct esbuild should do
  the same so diagnostics resolve to original `.tsx` locations.
- `tsc` is optional type checking only; esbuild erases types without checking.

## Debug checklist

1. Check `RuntimeOptions::on_diagnostic` / `Runtime::LastDiagnostic()`.
2. `component_stack` shows the TSX path; `phase` identifies load/render/effect/event/timer.
3. Icons must be valid local PNG/JPEG files for `Image`, `IconButton`, `NavigationBar`.
4. Timers require an active HuxerUI application; schedule them from effects/events, not bundle top level.
5. `VirtualList`/`VirtualGrid` need stable keys and a valid `itemExtent`/`rowExtent`.
6. Function components are scoped by function identity + sibling occurrence. Give reordered lists
   explicit `key` values.
7. A failed reload keeps the previous tree; a successful reload resets state.
8. With an inline source map, `Diagnostic::filename`/`line`/`column` and bundle stack frames point
   at the original `.tsx`; `generated_*` keeps the bundle location. Without a map, locations are
   generated `app.js` positions.

## Validation

```sh
cmake -S . -B build -DHUXERUI_HOME="$HOME/.local/share/HuxerUI" \
  -DHUXERUI_TSX_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
node <typescript>/bin/tsc -p examples/showcase/tsconfig.json
```
