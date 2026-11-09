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

`Column`, `Row`, `Flow`, `Stack`, `Theme`, `Text`, `Button`, `IconButton`, `Image`, `Divider`,
`Spacer`, `ScrollView`, `SelectionArea`, `TextField`, `Checkbox`, `RadioButton`, `Switch`,
`Slider`, `ProgressBar`, `ProgressCircle`, `Chip`, `VirtualList`, `VirtualGrid`, `Tabs`,
`SegmentedButton`, `NavigationPane`, `NavigationBar`, `IndexedPages`.

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

## Toolchain

- CMake: `huxerui_tsx_bundle`, `huxerui_tsx_embed_header`, `huxerui_use_library`.
- One-command preview: `cmake --build <build> --target tsx_preview`.
- Direct esbuild needs `--jsx=automatic --jsx-import-source=@huxerui/tsx`.
- Externalize `@huxerui/tsx` and `@huxerui/tsx/jsx-runtime` when the runtime loader will serve them.
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
8. Source-map locations are still generated `app.js` positions; original `.tsx` line numbers are TODO.

## Validation

```sh
cmake -S . -B build -DHUXERUI_HOME="$HOME/.local/share/HuxerUI" \
  -DHUXERUI_TSX_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
node <typescript>/bin/tsc -p examples/showcase/tsconfig.json
```
