# HuxerUI TSX Showcase

A runnable TSX page that exercises the current `Lib-JSRuntime` surface:

- layout and native host elements
- `Tabs`, `NavigationPane`, `NavigationBar`
- `VirtualList`, `VirtualGrid`
- controlled `TextField` with selection, validation, focus ref, and `onEdit`
- pointer, hover, scroll, key, focus, drag, long press, and multi tap events
- `useToast`, `useDialog`, `useMenu`, `usePopup`, `useSnackBar`, `useBottomSheet`
- native `useFocusRef` and `useScrollRef`

Build and run the live preview:

```sh
cmake -S examples/showcase -B build-showcase -G Ninja \
  -DHUXERUI_HOME="$HOME/.local/share/HuxerUI"
cmake --build build-showcase --target tsx_showcase
```

Type-check only:

```sh
tsc -p examples/showcase/tsconfig.json
```

`App.tsx` is intentionally self-contained except for `@huxerui/tsx`; the runtime serves that module
from its embedded JS source.
