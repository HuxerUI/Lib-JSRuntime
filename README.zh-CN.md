# Lib-JSRuntime — TSX for HuxerUI

[English](README.md) | [简体中文](README.zh-CN.md)

`Lib-JSRuntime` 把 TSX 作为 HuxerUI 的一等 UI 描述方式。一个 TSX 函数组件对应 HuxerUI 的
composition scope，每个 `useState` 对应原生 state slot，每个宿主标签对应一个 HuxerUI
`View`。DSL 仍然是 HuxerUI 的：`Column`、`Text`、`Spacer`、修饰符和原生事件；TSX 只是
作者层。

库发布 **`HuxerUI::TSX`**（别名 **`HuxerUI::JSRuntime`**）和公开头文件
`<huxerui/tsx.h>`。TypeScript 模块 `/app/main.js` 从 `@huxerui/tsx` 导入运行时组件，
该模块由 native module loader 从内嵌 ES module 提供。

```text
App.tsx ──esbuild──▶ app.js (ES module) ──QuickJS──▶ VNode tree
                                                        │
                                    HuxerUI Scope / State / View
```

- [能力](#能力)
- [集成](#集成)
- [TSX 工具链](#tsx-工具链)
- [最小应用](#最小应用)
- [运行时模型](#运行时模型)
- [布局热重载](#布局热重载)
- [宿主元素](#宿主元素)
- [Props 与修饰符](#props-与修饰符)
- [Hooks](#hooks)
- [导航](#导航)
- [事件](#事件)
- [模块与宿主函数](#模块与宿主函数)
- [宿主服务](#宿主服务)
- [限制](#限制)
- [仓库结构](#仓库结构)
- [验证](#验证)
- [Agent skill](skills/lib-jsruntime/SKILL.md)
- [Showcase](examples/showcase)

## 能力

| 能力 | 入口 |
| --- | --- |
| 执行 TSX bundle 并挂载一棵根树 | `tsx::Runtime`, `RuntimeOptions`, `Runtime::Load()`, `Runtime::LoadFile()`, `Runtime::LoadConfiguredBundle()` |
| 布局热重载（每次 revision 重置 state） | `Runtime::Reload()`, `Runtime::ReloadFile()`, `Runtime::ReloadConfiguredBundle()` |
| 渲染进 HuxerUI 应用 | `Runtime::Root()`, `tsx::TsxRoot(Runtime&)` |
| 函数组件与稳定的位置式 state | `useState`, `useReducer`, `useRef`, `useMemo`, `useCallback` |
| commit 阶段 effect 与清理 | `useEffect`, `useLayoutEffect`，由 `huxerui::Lifecycle` 支撑 |
| 原生宿主元素 | `Column`, `Row`, `Flow`, `Stack`, `Theme`, `Text`, `Button`, `IconButton`, `Image`, `Select`, `ComboBox`, `DatePicker`, `TimePicker`, `RefreshBox`, `Pager`, `SplitLayout`, `TopAppBar`, `Divider`, `Spacer`, `ScrollView`, `SelectionArea`, `TextField`, `Checkbox`, `RadioButton`, `Switch`, `Slider`, `ProgressBar`, `ProgressCircle`, `Chip`, `VirtualList`, `VirtualGrid`, `Tabs`, `SegmentedButton`, `NavigationPane`, `NavigationBar`, `IndexedPages` |
| 无障碍 | `semantics` prop 映射到 HuxerUI `Semantics`（role、label、hint、state、identifier） |
| 原生修饰符 | padding、spacing、frame/width/height、background、foreground、border、corner radius、shadow、clip、enabled、focusable、main/cross alignment、align、grow、`onClick` |
| 事件回调 | `onClick`；pointer down/move/up/cancel/intercept；hover enter/move/leave；scroll；key down/up/intercept；focus/blur；context menu；Back；表单控件的 `onChange`/`onSubmit` |
| 宿主扩展 | `Runtime::RegisterFunction()`, `Runtime::SetModuleLoader()`, `Runtime::Evaluate()`, `Runtime::Invoke()` |
| 宿主服务 | `fetch()`, `readClipboard()`/`writeClipboard()`/`clipboardAvailable()`, `applicationLifecycle()`, `quitApplication()` |
| 诊断 | `RuntimeOptions::on_diagnostic`, `on_log`, `on_error`, `Runtime::LastDiagnostic()`, 内联渲染错误 `Text` |
| 执行预算 | `RuntimeOptions::evaluation_time_limit`, `operation_time_limit`, `should_interrupt`, `stack_limit_bytes` |
| 定时器 | `setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`, runtime `queueMicrotask` |
| 浮层 | `useToast()`, `useDialog()`, `useMenu()`, `useSnackBar()`, `useBottomSheet()`, `usePopup()` |

与 QuickJS 和 HuxerUI composition 一样，bridge 是单线程的。在应用线程创建 runtime、执行
bundle、compose view。

## 集成

`Lib-JSRuntime` 需要 HuxerUI SDK（或源码树）。QuickJS 及其 quickjs-ng core 已经内嵌在
本仓库的 [`quickjs/`](quickjs) 下，不需要兄弟目录 `Lib-QuickJS`。把 HuxerUI clone 到应用
旁边，然后获取库 target：

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::TSX
    PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Lib-JSRuntime"
)
```

QuickJS 直接编译进 `HuxerUI::TSX`；`HuxerUI::QuickJS` 仍作为兼容 alias 保留。

公开头文件需要显式 include；它不在 `<huxerui/huxerui.h>` 里：

```cpp
#include <huxerui/tsx.h>
```

## TSX 工具链

库执行的是 JavaScript；TypeScript 和 JSX 在构建期被移除。仓库固定了一个独立 esbuild
binary，CMake 直接调用它；**不需要 Node.js**：

```sh
third_party/esbuild/linux-x64/esbuild src/App.tsx \
  --bundle --format=esm --platform=neutral --jsx=automatic \
  --jsx-import-source=@huxerui/tsx --target=es2020 \
  --external:@huxerui/tsx --external:@huxerui/tsx/jsx-runtime \
  --sourcemap=inline --sources-content=false \
  --outfile=build/app.js
```

`huxerui_tsx_bundle()` 会生成同样的命令。仓库固定了 esbuild 0.25.0 原生 binary，覆盖
Linux x64/arm64/arm/ia32/ppc64/s390x/loong64/riscv64/mips64el、macOS x64/arm64、
Windows x64/arm64/ia32、FreeBSD/OpenBSD/NetBSD、SunOS 和 AIX host。CMake 根据
`CMAKE_HOST_SYSTEM_NAME` 和 `CMAKE_HOST_SYSTEM_PROCESSOR` 从
`third_party/esbuild/<platform>/` 选择 binary；校验和位于
`third_party/esbuild/SHA256SUMS`。

`HUXERUI_TSX_ESBUILD_PLATFORM` 强制指定平台 id；`HUXERUI_TSX_ESBUILD` 指向任意
esbuild binary。可选的 `tools/huxerui-tsx.mjs` wrapper 仍然保留给 Node 用户，并回退到同一
批内置 binary。

这些 flag 让 `@huxerui/tsx` 保持 external（由 native runtime 提供），并启用 automatic
JSX runtime：

```text
--bundle --format=esm --platform=neutral --jsx=automatic
--jsx-import-source=@huxerui/tsx
--external:@huxerui/tsx --external:@huxerui/tsx/jsx-runtime
--sourcemap=inline --sources-content=false
```

`--sourcemap=inline` 会把 source map 内嵌到 bundle；runtime 用它把诊断指回原始 `.tsx`
文件。`--sources-content=false` 不内嵌源码文本，让内嵌 map 保持较小。没有 inline map 的
bundle 仍然可用，只是诊断报告生成后的位置。

推荐的 `tsconfig.json`：

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

Classic JSX（`"jsx": "react"`, `"jsxFactory": "h"`）也支持；从 `@huxerui/tsx` 导入 `h`。

### CMake helpers

`cmake/HuxerUITsx.cmake` 负责打包并内嵌模块：

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

`huxerui_tsx_bundle()` 依次通过 `ESBUILD` 参数、`HUXERUI_TSX_ESBUILD` cache 变量、
`HUXERUI_ESBUILD` 环境变量、`PATH`、内置 `third_party/esbuild/<host>/esbuild` 解析
esbuild，并始终追加 inline source map flag。Node.js 不参与。

## 最小应用

`src/App.tsx`：

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

`src/main.cpp`：

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

`Runtime::Root()` 会在第一次 composition 时自动执行 `RuntimeOptions::bundle`。如果 bundle
来自文件，构造空 runtime，并在应用启动前调用 `LoadFile(path)`。

## 运行时模型

- 一个 TSX **函数组件**就是一个 `huxerui::Scope`。当它自己的某个 state slot commit 时会
  重新执行；嵌套组件 scope 保持身份和 state。
- `useState` 按调用顺序映射到该 scope 拥有的原生 `State<T>` slot。state 值是 JavaScript
  handle，因此对象和数组可以直接存储，无需复制。
- **宿主标签**就是 `View` 声明；修饰符通过 C++ DSL 相同的 `With(...)` API 应用。未改动的
  host element 在每次组件 render 时重新创建，由 HuxerUI reconcile。
- `useEffect` 细化 `huxerui::Lifecycle` 注册。依赖数组用 JavaScript `===` / `Object.is`
  比较；省略依赖每次 render 都执行；`[]` 只执行一次。
- 组件 `key` prop 和兄弟 `key` 会传给 `View::Key`，用于 reconcile。
- 组件返回数组时，兄弟节点会堆叠在 `Column` 中。直接子节点数组（`{items.map(...)}`）会
  拼接进父布局，并保持父布局的轴向。

没有独立的 signals 引擎：读写直接经过 HuxerUI state。只有一份 source of truth 和一个
recomposition scheduler。

## 布局热重载

`Runtime::Reload()` 是开发循环中有意的 state 重置路径：

```cpp
const auto result = runtime.ReloadFile("/path/to/app.js");
if (!result.Succeeded()) {
  // 旧树保持挂载；显示 result.Error()
}
```

每次 reload：

1. 以带 revision 后缀的模块名执行 bundle，避免 ES module cache 返回旧实例。
2. 恢复 `__huxerui_root`；如果执行失败或 bundle 没有调用 `mount()`，保留上一棵布局。
3. 增加 root revision state。root `Scope` 以该 revision 为 key，因此 HUI 会卸载整棵
   TSX 子树并挂载新的子树。
4. 用全新的 `useState`、`useRef`、effect 重新运行每个组件。

这不是 state-preserving HMR。布局、文本、样式、结构和事件处理器会更新；组件 state 会
重置。对 AI 驱动的迭代，这是可靠模式：没有陈旧 hook 图，没有跨 revision 值迁移。

[`examples/preview`](examples/preview) 把它包装成一个小的 dev host：轮询 bundle 的修改
时间，保存后 reload。

限制：在 UI 线程调用 `Reload*`；把应用打包成一个模块，避免旧的相对 import 留在缓存中；
QuickJS module cache 会在 context 生命周期内保留旧 revision 的模块命名空间。这对 dev
preview 可接受，但不适合无界的生产热更新服务。

## 宿主元素

| 标签 | C++ view | 主要 props |
| --- | --- | --- |
| `Column`, `Row`, `Flow`, `Stack` | 同名布局 | children 加共享修饰符 |
| `Theme` | `MaterialTheme` / `MaterialDarkTheme` / `FlatTheme` / `FlatDarkTheme` | `variant`（`material`、`materialDark`、`flat`、`flatDark`）或 `dark`；包裹一个 child（多个 child 会变成 `Column`） |
| `Text` | `Text` | `value`（字符串或 `{ resource: "domain:key" }`）, `role`, `fontSize`, `fontWeight`, `fontFamily`, `italic`, `color`, `decoration`, `textAlign` |
| `Button` | `Button` | `label`（字符串或 `{ resource }`）或文本 children；`onClick` |
| `IconButton` | `IconButton` | `src`（本地文件或 `{ resource }`）, `label` |
| `Image` | `Image` | `src`（本地文件、`{ file }` 或 `{ resource: "domain:key" }`）, `fit`, `tint` |
| `Select` | `Select` | `items`, `selected`, `label`, `validation`, `onChange(index)`, `onExpandedChange(bool)` |
| `ComboBox` | `ComboBox` | `value`, `suggestions`, `label`, `placeholder`, `maxLength`, `validation`, `onChange`, `onSelect(index, value)`, `onSubmit`, `onExpandedChange` |
| `DatePicker` | `DatePicker` | `value` / `min` / `max`（`YYYY-MM-DD`）, `label`, `validation`, `onChange(string)` |
| `TimePicker` | `TimePicker` | `value`（`HH:MM`）, `step` 分钟, `label`, `validation`, `onChange(string)` |
| `RefreshBox` | `RefreshBox` | 一个 child, `refreshing`, `onRefresh` |
| `Pager` | `Pager` | children, `selected`, `axis`, `reverse`, `dragEnabled`, `onChange`, `onSettled` |
| `SplitLayout` | `SplitLayout` | 两个及以上 children, `axis`, `onChange(changes)`（回写 frame/grow） |
| `TopAppBar` | `TopAppBar` | `title`, `leading`, `actions`, `titleAlign` |
| `Divider` | `Divider` | `axis` |
| `Spacer` | `Spacer` | 共享修饰符 |
| `ScrollView` | `ScrollView` | `axis`, `scrollRef`；多个 child 会变成 `Column` |
| `SelectionArea` | `SelectionArea` | 一个 child 或 `Column` |
| `TextField` | `TextField` | `value`, `selection`, `validation`, `label`, `placeholder`, `variant`, `multiline`, `maxLength`, `secure`, `onChange`, `onEdit`, `onSubmit`, `ref` |
| `Checkbox`, `RadioButton`, `Switch` | 同名控件 | `label`, `checked`/`selected`, `onChange(bool)` |
| `Slider` | `Slider` | `value`, `min`, `max`, `step`, `onChange(number)` |
| `ProgressBar`, `ProgressCircle` | 同名控件 | `progress`；省略表示不确定进度 |
| `Chip` | `Chip` | `label`, `selected`, `onChange(bool)` |
| `VirtualList` | `VirtualList` | `items`, `renderItem`, `itemExtent`, `estimatedItemExtent`, `cacheExtent`, `axis`, `scrollRef` |
| `VirtualGrid` | `VirtualGrid` | `items`, `renderItem`, `columns`/`adaptiveColumnWidth`, `rowExtent`, `estimatedRowExtent`, spacing, `itemSpans`, `scrollRef` |
| `Tabs` | `Tabs` | `labels`, `selected`, `onChange(number)` |
| `SegmentedButton` | `SegmentedButton` | `labels`, `selected`, `onChange(number)` |
| `NavigationPane` | `NavigationPane` | `labels`, `selected`, `expanded`, `onChange(number)` |
| `NavigationBar` | `NavigationBar` | `items: { label, src, selectedSrc?, disabled? }[]`（`src` 支持本地文件或 `{ resource }`）, `selected`, `onChange(number)` |
| `IndexedPages` | `IndexedPages` | `selected` 页索引 |

文本 children 按顺序拼接：`<Text>Count: {count}</Text>` 生成 `"Count: 3"`。
`Text`、`Button` 和导航标签也接受 `{ resource: "domain:key" }`；挂载后的 view 会在其生效的
`Locale` 下解析打包字符串。图片源接受本地路径、`{ file }` 或 `{ resource }`；打包的 raster
和编译后的 vector 图片 key 由 HuxerUI 在挂载时解析。

## Props 与修饰符

共享 props 一对一映射到 HuxerUI 修饰符：

| Prop | 修饰符 |
| --- | --- |
| `padding` | `Padding`（number、`[h, v]`、`[l, t, r, b]` 或 `{ left, top, right, bottom }`） |
| `spacing` | `Spacing` |
| `frame` / `width` / `height` | `Frame` |
| `background` | `Background(VisualFill)` |
| `foreground` | `Foreground` |
| `border` | `Border`（颜色或 `{ color, width }`） |
| `cornerRadius` | `CornerRadius` |
| `shadow` | `Shadow`（`{ color, offset: [x, y], blurRadius, spread }`） |
| `clipChildren` | `ClipChildren` |
| `enabled` / `focusable` | `Enabled` / `Focusable` |
| `mainAlign` / `crossAlign` | `MainAlign` / `CrossAlign` |
| `align` | `Align`（`"center"` 或 `{ horizontal, vertical }`） |
| `grow` | `Grow`（`true` 或一个系数） |
| `tooltip` | `Tooltip`（字符串消息） |
| `semantics` | `Semantics`（role、label、value、hint、状态标志、live region） |

颜色接受 `#rgb`、`#rgba`、`#rrggbb`、`#rrggbbaa`、`rgb(...)`、`rgba(...)`、常见颜色名、
`0xRRGGBB`、`[r, g, b, a?]` 或 `{ r, g, b, a? }`。通道范围 0–255；alpha 范围 0–1。

未知标签会渲染一个内联 `Text("Unknown TSX element <...>")` 诊断。未知 props 会被忽略。

## Hooks

```tsx
const [count, setCount] = useState(0);        // value 或 updater
const [state, dispatch] = useReducer(reducer, initial);
const ref = useRef(initial);                  // 稳定对象，修改不会触发 recompose
const value = useMemo(factory, deps);         // 用 Object.is 比较
const callback = useCallback(fn, deps);
useEffect(() => { return () => cleanup(); }, deps);

const field = useFocusRef();
return <TextField ref={field} value={value} onChange={setValue} />;
// field.requestFocus() 为绑定的控件请求原生键盘焦点。

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

// 坐标模式也可用：
// menu.showAt(120, 80, [{ label: "Rename", onSelect: renameItem }]);
```

规则与 React 一致：每次 render 都无条件、按相同顺序调用 hooks。state 在组件 scope 内按
位置确定。提前 return 的组件会改变 hook 顺序，这是 bug。

`useState` 接受惰性初始化：

```tsx
const [data, setData] = useState(() => loadInitialData());
```

## 导航

`useNavigation()` 持有一个页面栈；`NavigationStack` 渲染所有保留页面并选择栈顶页面。隐藏页面
保持挂载，因此 push/pop 不会丢失 hooks 和 effect state。栈容器会把 Back 映射为弹出一页。

```tsx
import { Button, Column, NavigationStack, Text, useNavigation } from "@huxerui/tsx";

function Home({navigation}) {
  return (
    <Column>
      <Text>Home</Text>
      <Button onClick={() => navigation.push(() => <Details navigation={navigation} />)}>
        Details
      </Button>
    </Column>
  );
}

function Details({navigation}) {
  return (
    <Column>
      <Text>Details</Text>
      <Button onClick={() => navigation.pop()}>Back</Button>
    </Column>
  );
}

function App() {
  const navigation = useNavigation(() => <Home navigation={navigation} />);
  return <NavigationStack navigation={navigation} />;
}
```

提供 `push(page)`、`pop()`、`replace(page)`、`reset(page?)`、`depth`、`canPop`、`entries()`
和 `current()`。这是 TSX 页面栈，不是 HuxerUI 原生 routed `NavigationStack`：typed route、
原生页面转场和可序列化 history 不在这个 API 内。隐藏页面仍保持挂载，因此这些页面的 effect
会继续运行。

## 事件

共享事件 props 映射到 HuxerUI 类型化事件：

| TSX prop | HuxerUI 事件 | JS payload / 返回值 |
| --- | --- | --- |
| `onClick` | `ViewEvents::Click` | 无 payload |
| `onPointer`, `onPointerDown`, `onPointerMove`, `onPointerUp`, `onPointerCancel` | `ViewEvents::Pointer` | `PointerInputEvent` |
| `onPointerIntercept` | `ViewEvents::PointerIntercept` | `PointerInputEvent`；返回 `true` 取得所有权 |
| `onHover`, `onPointerEnter`, `onPointerLeave` | `ViewEvents::Hover` | `HoverInputEvent` |
| `onScroll` | `ViewEvents::ScrollInput` | `ScrollInputEvent`；返回 `true` 消费 |
| `onKeyDown`, `onKeyUp` | `ViewEvents::KeyDown` / `KeyUp` | `KeyInputEvent`；返回 `true` 消费 |
| `onKeyIntercept` | `ViewEvents::KeyIntercept` | `KeyInputEvent`；在 focused view 之前返回 `true` 消费 |
| `onFocus`, `onBlur`, `onFocusChanged` | `ViewEvents::FocusChanged` | `onFocusChanged` 可选 `boolean` |
| `onContextMenu` | `ViewEvents::ContextMenuRequested` | `{ x, y }` |
| `onBack` | `ViewEvents::BackRequested` | 无 payload |
| `onDragStart`, `onDrag`, `onDragEnd`, `onDragCancel` | 原生 `DragGesture` + `DragEvents` | `DragInputEvent`；`dragAxis`, `dragThreshold`, `dragDelay` 配置识别 |
| `onLongPressStart`, `onLongPressEnd`, `onLongPressCancel` | 原生 `LongPressGesture` + `LongPressEvents` | `LongPressInputEvent`；`longPressDuration`, `longPressMovement` 配置识别 |
| `onMultiTap` | 原生 `MultiTapGesture` + `MultiTapEvents` | `MultiTapInputEvent`；`multiTapCount`, `multiTapInterval`, `multiTapMovement` 配置识别 |

`onChange` 映射到 `ToggleEvents::Changed`（`bool`）、`SliderEvents::Changed`（`number`）或
`TextFieldEvents::Changed`（`string`）。`onSubmit` 映射到 `TextFieldEvents::Submitted`。

输入事件遵循 HuxerUI 路由，而不是 DOM 冒泡：pointer/key/scroll 直接到达目标或 focused
view，只有带 `bool` 返回值的 handler 可以消费或拦截。没有 `stopPropagation()` /
`preventDefault()` 对象；从相关 handler 返回 `true` 即可。

Handler 在 UI 线程同步运行。抛出的异常会变成结构化的 `DiagnosticPhase::Event` 诊断，
不会展开 HuxerUI 事件派发。

定时器通过 HuxerUI `TaskScope`/`Delay` 使用应用单调时钟；回调在 UI 线程运行。
`setTimeout`/`setInterval` 返回数值 `TimerHandle`；用 `clearTimeout`/`clearInterval`
取消。runtime 销毁时定时器会自动取消。它们需要已初始化的 HuxerUI application，应该从
effect、事件或 timer 回调里调度，而不是在 bundle 执行期间调度。`requestAnimationFrame`
和 `fetch` 尚未提供。

## 模块与宿主函数

`@huxerui/tsx` 和 `@huxerui/tsx/jsx-runtime` 是内置模块。其他 import 依次经过
`RuntimeOptions::module_root` 和 `Runtime::SetModuleLoader()`：

```cpp
runtime.SetModuleLoader([](std::string_view name) -> huxerui::quickjs::Result<std::string> {
  if (name == "/assets/format.js") {
    return huxerui::quickjs::Result<std::string>::Success("export const pad = (v) => String(v).padStart(2, '0');");
  }
  return huxerui::quickjs::Result<std::string>::Failure(
      huxerui::quickjs::Error{huxerui::quickjs::ErrorCode::Module, "Unknown module"});
});
```

向 TSX 暴露应用服务：

```cpp
runtime.RegisterFunction("readClipboard", [](quickjs::Context& context, std::span<const quickjs::Value>) {
  return context.NewString(huxerui::Clipboard::ReadText()).Value();
});
```

Host globals 在 bundle 执行前注册。推荐打包应用，这样运行时不需要 JS module graph。

## 宿主服务

内嵌模块暴露正在运行的 HuxerUI 应用的标准服务。它们需要应用已初始化并在应用线程调用；
没有应用时，clipboard 读取返回 `null`，写入返回 `false`，lifecycle 返回 `"unknown"`，
`fetch()` 会 reject。

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

`fetch(url, options?)` 返回 `Promise<{ url, status, headers, body, byteLength }>`。HTTP 4xx/5xx
正常 resolve；transport、timeout、unsupported transport 和参数错误会 reject，
reason 为 `{ code: "transport" | "timeout" | "unsupported", message }` 或校验错误字符串。

| 选项 | 含义 |
| --- | --- |
| `method` | `GET`（默认）、`HEAD`、`POST`、`PUT`、`PATCH`、`DELETE`、`OPTIONS` |
| `headers` | `Record<string, string>` |
| `body` | UTF-8 请求体字符串 |
| `timeout` | 完整请求超时（毫秒）；默认 30000 |

`readClipboard()`、`writeClipboard(text)` 和 `clipboardAvailable()` 是应用线程上的同步调用；
`clipboard` 对象把它们组合在一起。`applicationLifecycle()` 返回 `"active"`、`"inactive"`、
`"background"` 或 `"unknown"`。`quitApplication()` 请求有序退出。通用异步宿主 I/O 仍通过
`Runtime::RegisterFunction()` 扩展。

## 诊断与执行预算

每个 bridge 失败都会在到达 `on_log` 或 `on_error` 之前规范化为 `tsx::Diagnostic`：

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

`Diagnostic` 包含 `phase`、`code`、`operation`、`message`、`filename`、`line`、`column`、
`generated_filename`、`generated_line`、`generated_column`、`stack`、`component_stack`
和 `description`。当 bundle 内嵌 inline source map 时，`filename`、`line`、`column` 指向
原始 `.tsx` 源文件，来自该 bundle 的 `stack` 帧会被回源，`generated_*` 字段保留生成后的
bundle 位置。没有 source map 时，映射字段等于生成字段。`description` 的格式为
`phase [ErrorCode]: operation: message (file:line:column)`，随后是 JavaScript stack 和
`component stack: App > Child`。`Runtime::LastDiagnostic()` 返回最近一次诊断，方便测试和
错误面板使用。

- `evaluation_time_limit` 限制一次 module load/reload 或 `Runtime::Evaluate()` 调用。
- `operation_time_limit` 限制一次同步组件 render、hook 初始化、effect、事件 handler、
  timer 回调或 `Runtime::Invoke()` 调用。它由 QuickJS interrupt handler 强制执行，死循环
  会变成 `ErrorCode::Interrupted`。
- `should_interrupt` 是更低层的协作式探针，覆盖所有 JavaScript 执行。
- `stack_limit_bytes` 和 `memory_limit_bytes` 透传给 QuickJS。

`on_diagnostic` 在 `on_log` 和 `on_error` 之前运行；诊断回调抛出的异常会被吞掉，不会覆盖
原始 JavaScript 失败。

## 限制

- 没有 React 兼容层：没有 context、suspense、portals、通用 native-view refs 或 concurrent
  rendering。`useRef` 存储 JavaScript 值；`useFocusRef` 为一个绑定的可聚焦宿主元素暴露
  焦点；`useScrollRef` 为一个绑定的 `ScrollView` 暴露控制器。
- 组件返回多个兄弟元素时会堆叠在 `Column` 中；HuxerUI 没有 fragment view。把兄弟数组直接
  放在布局内部以保持轴向。
- 不翻译 `margin`；HuxerUI 使用 padding 和 spacing。
- 事件覆盖 click、pointer、hover、scroll、key、focus、context menu、Back、drag、long
  press 和 multi tap，以及 toggle、slider、chip 和 text-field 变化。
- 定时器覆盖 `setTimeout`、`setInterval` 和 `queueMicrotask`；`requestAnimationFrame` 和
  `fetch()` 之外的通用异步宿主 I/O 尚未暴露。`fetch()` 缓冲完整响应体并暴露 UTF-8 文本；
  流式和二进制 payload 尚未暴露。
- 图片解析本地文件和打包图片资源 key；TSX props 不暴露原始 SVG 文本、网络 URL 和任意程序化矢量构建。
- 原生 routed `NavigationStack`、typed route、页面转场和可序列化 history 尚未桥接。TSX 提供
  `useNavigation()`/`NavigationStack` 页面栈；动画和 canvas 尚未桥接。
- HUI 原生 `Select`、`ComboBox`、`DatePicker`、`TimePicker` 会解析框架内置图片和字符串；
  消费应用需要安装 HuxerUI 资源包。
- Drawer、dock、tree、window title bar 以及任意 `Canvas`/`VectorAsset` 构建尚未暴露为 TSX 标签。
- `VirtualList::renderItem` / `VirtualGrid::renderItem` 是在虚拟测量期间运行的声明式
  回调；它们不能调用 hooks。需要 hook state 时，在返回的 child 里使用函数组件。
- runtime 拥有 JavaScript context。它必须比它产生的每个 `View` 活得久；应用挂载期间不要
  销毁它。
- 函数组件按函数身份加同层出现顺序建立 scope；重排列表请使用显式 `key`。
- 启动成本是一个 QuickJS context 加一次 module evaluation。把重工作移出 UI 线程，并避免
  state 中过大的对象图。
- source map 解析只读取 inline map。不会加载外部 `.map` 文件和 indexed map（`sections`）；
  使用 `--sourcemap=inline` 打包才能回源诊断。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| [`include/huxerui/tsx.h`](include/huxerui/tsx.h) | 公开 `Runtime`、`RuntimeOptions`、`TsxRoot()` 契约 |
| [`src/runtime.cpp`](src/runtime.cpp) | Engine 生命周期、module loader、hooks、VNode 遍历、错误上报 |
| [`src/elements.cpp`](src/elements.cpp) | 宿主标签到 HuxerUI `View` 的构建器与事件转发 |
| [`src/props.cpp`](src/props.cpp) | Prop 解析、颜色、修饰符、key |
| [`src/source_map.cpp`](src/source_map.cpp) | Inline source map 解析与诊断/stack 回源 |
| [`js/runtime.js`](js/runtime.js) | 内嵌 `@huxerui/tsx` 模块：JSX runtime、hooks、宿主标签、`mount()` |
| [`quickjs/`](quickjs) | 内嵌 Lib-QuickJS wrapper 和 quickjs-ng `v0.18.0` engine 源码 |
| [`quickjs/tests`](quickjs/tests) | 内嵌 engine 契约测试 |
| [`types/`](types) | TypeScript element props 与 automatic-JSX 声明 |
| [`tools/huxerui-tsx.mjs`](tools/huxerui-tsx.mjs) | 可选的 Node esbuild wrapper；回退到内置 binary |
| [`tools/huxerui-tsx-dev.sh`](tools/huxerui-tsx-dev.sh) | POSIX 一键 esbuild watcher + app launcher |
| [`third_party/esbuild`](third_party/esbuild) | 固定版本 esbuild 0.25.0 全平台 binary 及校验和 |
| [`.github/workflows/ci.yml`](.github/workflows/ci.yml) | 跨平台 esbuild smoke，以及 native 构建/测试 CI |
| [`cmake/HuxerUITsx.cmake`](cmake/HuxerUITsx.cmake) | `huxerui_tsx_bundle()` 和 `huxerui_tsx_embed_header()` |
| [`examples/counter`](examples/counter) | 可构建的 TSX counter 应用 |
| [`examples/showcase`](examples/showcase) | 完整能力 showcase + 一键 preview |
| [`skills/lib-jsruntime`](skills/lib-jsruntime/SKILL.md) | 面向 agent 的使用 skill 与正确性检查清单 |
| [`examples/preview`](examples/preview) | 实时布局预览 host，带文件监听和 `Runtime::ReloadFile()` |
| [`tests/`](tests) | Headless composition、state、嵌套 scope、effect、compiled TSX 和 source map 测试 |

## 验证

在 Linux 上针对已安装的 HuxerUI 0.4.0 SDK 验证，QuickJS engine 从内嵌 `quickjs/` 源码
构建：

- 库和示例可以配置并构建（GCC 16.2.1，Ninja）。
- `ctest` 通过：内嵌 QuickJS engine 测试、counter/updater state、嵌套组件 state 保持、
  effect 运行/清理、惰性初始化、memo/ref 行为、条件与数组 children、布局热重载（新布局、
  fresh state、失败 reload 回滚）、带组件栈的结构化 render/event/effect/timer 诊断、timer
  回调、执行预算，以及端到端测试：用内置 esbuild 打包 TSX、通过 runtime 的 module loader
  加载产物，并用 `huxerui::testing::UiTest` 驱动。compiled fixture 还会验证 source map
  对主位置、stack 帧和格式化 description 的回源。
- `examples/showcase` 可以构建，其 `tsconfig.json` 通过 `tsc --strict`（`jsx: react-jsx`，
  `jsxImportSource: @huxerui/tsx`）。
- CI 还会打包 `examples/showcase/src/App.tsx` 并在每个支持的 host 上验证 showcase 产物。
- `.github/workflows/ci.yml` 在 Linux x64/arm64、macOS x64/arm64、Windows x64/arm64 上运行
  `ci/esbuild-smoke`，验证每个 host 都能选择内置 binary 并打包 TSX。它还会在 Ubuntu、
  Windows 和 macOS 上针对固定版本的 HuxerUI 源码 checkout 配置、构建并运行测试套件。

本地验证仅限 Linux；Android、iOS 和 Web 构建不在验证范围内。

## 许可证

MIT，见 [LICENSE](LICENSE)。
