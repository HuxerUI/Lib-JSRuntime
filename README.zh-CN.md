[English](README.md) | [简体中文](README.zh-CN.md)

# Lib-JSRuntime — 让 TSX 成为 HuxerUI 的 UI 语言

`Lib-JSRuntime` 把 TSX 作为 HuxerUI 的一等 UI 描述方式。一个 TSX 函数组件对应 HuxerUI 的
composition scope，`useState` 对应原生 state slot，宿主标签对应 HuxerUI `View`。TSX 只是
作者层；布局、渲染、输入、无障碍仍然由 HuxerUI 原生引擎负责。

库的主 target 是 **`HuxerUI::TSX`**（别名 **`HuxerUI::JSRuntime`**），公开头文件是
`<huxerui/tsx.h>`。QuickJS 已经直接编译进 `HuxerUI::TSX`，不再依赖外部 Lib-QuickJS；
`HuxerUI::QuickJS` 仅作为兼容 alias。

```text
App.tsx ──内置 esbuild──▶ app.js (ES module) ──内置 QuickJS──▶ VNode tree
                                                              │
                                          HuxerUI Scope / State / View
```

## 能力

| 能力 | 入口 |
| --- | --- |
| 执行 TSX bundle 并挂载根树 | `tsx::Runtime`, `RuntimeOptions`, `Load()`, `LoadFile()`, `LoadConfiguredBundle()` |
| 布局热重载（每次重置 state） | `Reload()`, `ReloadFile()`, `ReloadConfiguredBundle()` |
| 渲染进 HuxerUI 应用 | `Runtime::Root()`, `tsx::TsxRoot(Runtime&)` |
| 函数组件与稳定 state | `useState`, `useReducer`, `useRef`, `useMemo`, `useCallback` |
| 副作用 | `useEffect`, `useLayoutEffect` |
| 原生宿主元素 | `Column`, `Row`, `Flow`, `Stack`, `Text`, `Button`, `TextField`, `Slider` 等 |
| 嵌套组件 / 列表 / 条件渲染 | 函数组件、数组 children、`#fragment` |
| 模块与宿主函数 | `SetModuleLoader()`, `RegisterFunction()`, `Context()` |
| 结构化诊断 | `Diagnostic`, `on_diagnostic`, `LastDiagnostic()`, 组件栈 |
| 执行预算 | `evaluation_time_limit`, `operation_time_limit`, `should_interrupt`, `stack_limit_bytes` |
| 定时器 | `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, `queueMicrotask` |

## 构建依赖

- HuxerUI SDK 或源码树（`HUXERUI_HOME`）
- C++20 编译器、C11 编译器（QuickJS）
- CMake ≥ 3.20，推荐 Ninja
- Linux 上由 HuxerUI 间接引入 GTK4、epoxy、gio、pangoft2、fontconfig、libsoup-3.0
- esbuild 已内置；默认构建、watch、preview 不需要 Node/npm

## 集成

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::TSX
    PATH "${CMAKE_CURRENT_SOURCE_DIR}/path/to/Lib-JSRuntime"
)
```

公开头文件需要显式 include；它不在 `<huxerui/huxerui.h>` 里：

```cpp
#include <huxerui/tsx.h>
```

## TSX 工具链

TypeScript 和 JSX 在构建期转换成 QuickJS 可执行的 JavaScript。仓库内置 esbuild 0.25.0 的
22 个原生 host binary，CMake 根据 `CMAKE_HOST_SYSTEM_NAME` / `CMAKE_HOST_SYSTEM_PROCESSOR`
自动选择，不需要 Node：

```sh
third_party/esbuild/linux-x64/esbuild src/App.tsx \
  --bundle --format=esm --platform=neutral --jsx=automatic \
  --jsx-import-source=@huxerui/tsx --target=es2020 \
  --external:@huxerui/tsx --external:@huxerui/tsx/jsx-runtime \
  --outfile=build/app.js
```

支持的 host：

- Linux：x64、arm64、arm、ia32、ppc64、s390x、loong64、riscv64、mips64el
- macOS：x64、arm64
- Windows：x64、arm64、ia32
- FreeBSD、OpenBSD、NetBSD：x64、arm64
- SunOS x64、AIX ppc64

Android 不在内置列表：`@esbuild/android-*` 是 Node/WASM shim，不是原生 esbuild。

可选覆盖：

```sh
-DHUXERUI_TSX_ESBUILD_PLATFORM=linux-arm64
-DHUXERUI_TSX_ESBUILD=/path/to/esbuild
```

`HUXERUI_TSX_ESBUILD_PLATFORM` 强制平台 id；`HUXERUI_TSX_ESBUILD` 直接指定 binary。

### CMake helper

```cmake
huxerui_tsx_bundle("${CMAKE_CURRENT_BINARY_DIR}/app.js"
    ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/src/App.tsx"
)

huxerui_tsx_embed_header("${CMAKE_CURRENT_BINARY_DIR}/app_bundle.h"
    "${CMAKE_CURRENT_BINARY_DIR}/app.js"
    NAMESPACE counter
)
```

`huxerui_tsx_bundle()` 解析顺序：`ESBUILD` 参数 → `HUXERUI_TSX_ESBUILD` →
`HUXERUI_ESBUILD` → `PATH` → 内置 binary。

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

namespace {
huxerui::tsx::Runtime& Runtime() {
  static huxerui::tsx::Runtime runtime(huxerui::tsx::RuntimeOptions{
      .bundle = std::string(counter::kBundle),
  });
  return runtime;
}
huxerui::View App() { return Runtime().Root(); }
}

const huxerui::Application application{App};
int main() { return huxerui::RunApplication(); }
```

## 运行时模型

- 函数组件 → `Scope`
- `useState` / `useReducer` / `useRef` → `State<JsSlot>`，按 hook 调用位置稳定
- `useEffect` → `huxerui::Lifecycle`
- 宿主标签 → 原生 `View`
- 数组 children 展开；`#fragment` 直接拼接
- `key` 传给 `View::Key`
- 事件映射到 `Click`、`ToggleEvents::Changed`、`SliderEvents::Changed`、
  `TextFieldEvents::Changed/Submitted` 等

QuickJS context 与 JS value 都是单线程的；`Runtime` 必须比它产生的 View 活得久。

## 布局热重载

`Runtime::ReloadFile()` 重新执行 bundle，并通过 revision key 强制整棵 TSX 树 remount：

- 新布局生效
- state / ref / effect 全部重置
- 新 bundle 失败时保留旧布局
- 新 bundle 没有调用 `mount()` 时保留旧布局

这是**布局预览**，不是 state-preserving HMR。对 AI 驱动的布局迭代，确定性重置比保留旧 hook 图更可靠。

## 宿主元素

`Column`, `Row`, `Flow`, `Stack`, `Theme`, `Text`, `Button`, `IconButton`, `Image`,
`Divider`, `Spacer`, `ScrollView`, `SelectionArea`, `TextField`, `Checkbox`,
`RadioButton`, `Switch`, `Slider`, `ProgressBar`, `ProgressCircle`, `Chip`, `VirtualList`,
`VirtualGrid`, `Tabs`, `SegmentedButton`, `NavigationPane`, `NavigationBar`, `IndexedPages`。

## Props / Hooks / 事件

- 布局：`padding`, `spacing`, `width`, `height`, `grow`, `mainAlign`, `crossAlign`,
  `cornerRadius`, `background`, `border`, `foreground`, `tooltip` 等
- 不支持 `margin`；HuxerUI 用 padding/spacing
- Hooks：`useState`, `useReducer`, `useRef`, `useMemo`, `useCallback`, `useEffect`,
  `useLayoutEffect`, `useFocusRef`, `useScrollRef`
- `useFocusRef()` 返回 `{ id, requestFocus() }`；把返回对象作为 `ref` / `focusRef` 传给一个可聚焦宿主元素，
  即可在事件中请求原生焦点
- `useScrollRef()` 返回 `{ id, scrollTo, scrollBy, scrollToItem, offset, metrics }`；作为 `scrollRef` 绑定到 `ScrollView`
- `useToast()` 返回 `{ id, show(message, duration?), dismiss(layerId) }`；需要已初始化窗口
- `useDialog()` 支持 title/message/positive/negative 和 onPositive/onNegative 回调；`showContent(() => …)` 支持自定义 TSX 内容
- `useMenu()` 支持 `menuAnchor={menu}` + `menu.show(items)`，也支持 `showAt(x, y, items)`；item 支持 label、section、disabled、checked、onSelect
- `useSnackBar()` 支持 `show(message, duration?)`、`showAction(message, action, onAction, duration?)`、`dismiss(layerId)`
- `useBottomSheet()` 支持 `show(() => Child, options?)` 和 `dismiss(layerId)`
- `usePopup()` 支持 `popupAnchor={popup}` + `popup.show(() => Child, options?)` 和 `dismiss(layerId)`
- `TextField`：`value` + `selection` 受控，`validation` 显示校验状态，`onChange(string)` / `onEdit(TextEditingInputEvent)` / `onSubmit`
- 事件：`onClick`、pointer down/move/up/cancel、hover、scroll、key down/up、focus/blur/context-menu/back、drag start/change/end/cancel、long press start/end/cancel、multi tap；表单控件另有 `onChange` / `onSubmit`
- Drag / long press / multi tap 都由原生 gesture 识别，分别支持 `dragAxis`/`dragThreshold`/`dragDelay`、`longPressDuration`/`longPressMovement`、`multiTapCount`/`multiTapInterval`/`multiTapMovement`
- 事件走 HUI 的直接路由，不是 DOM 冒泡；`onPointerIntercept`、`onScroll`、`onKeyDown/Up/Intercept` 返回 `true` 表示消费/拦截
- 事件/定时器回调异常会以 `DiagnosticPhase::Event` / `Timer` 上报，不会展开 HUI 事件派发
- 定时器：`setTimeout` / `setInterval` 返回数值 handle，用 `clearTimeout` / `clearInterval` 取消；回调在 UI 线程执行，runtime 销毁时自动取消
- 定时器依赖已初始化的 HuxerUI application，应放在 effect / 事件 / timer 回调里，不建议在 bundle 顶层调用
- `@huxerui/tsx` 的运行时和类型声明都在仓库内，不需要 npm 包

## 模块 / 宿主函数

`@huxerui/tsx` 与 `@huxerui/tsx/jsx-runtime` 由 native module loader 内置提供。其他模块：

```cpp
runtime.SetModuleLoader([](std::string_view name) -> quickjs::Result<std::string> {
  // 返回 ESM source，或 Failure
});

runtime.RegisterFunction("hostCall", [](quickjs::Context&, std::span<const quickjs::Value>) {
  // ...
});
```

## 诊断与执行预算

bridge 的所有失败都会先整理成 `tsx::Diagnostic`，再交给 `on_log` / `on_error`：

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
`stack`、`component_stack`、`description`。`description` 的格式为：

```text
phase [ErrorCode]: operation: message (file:line:column)
    at Component (file:line:column)
component stack: App > Child
```

`Runtime::LastDiagnostic()` 返回最近一次诊断，方便测试和错误面板使用。

- `evaluation_time_limit`：限制一次 module load/reload 或 `Runtime::Evaluate()`。
- `operation_time_limit`：限制一次同步组件渲染、hook 初始化、effect、事件回调、timer 回调或
  `Runtime::Invoke()`。由 QuickJS interrupt handler 强制执行，死循环会变成
  `ErrorCode::Interrupted`。
- `should_interrupt`：更底层的协作式中断钩子，覆盖所有 JS 执行。
- `memory_limit_bytes` / `stack_limit_bytes`：透传给 QuickJS。

`on_diagnostic` 在 `on_log` 和 `on_error` 之前调用；诊断回调自己抛异常会被吞掉，不会覆盖原始
JavaScript 错误。

## 限制

- QuickJS 是当前唯一引擎；不再考虑替换 JS runtime
- **TODO**：source map 回源。当前诊断位置是生成的 bundle 行号（`app.js:line:column`），不是原始 `.tsx`
- 布局热重载会重置 state；没有 state-preserving HMR
- 事件已覆盖 click、pointer、hover、scroll、key、focus、context menu、Back、native drag、long press、multi tap
- `VirtualList` / `VirtualGrid` 的 `renderItem` 在虚拟测量期间运行，必须是声明式回调，不能调用 hooks；需要 hook state 时在返回的 child 里放函数组件
- 定时器只有 `setTimeout` / `setInterval` / `queueMicrotask`；`requestAnimationFrame`、`fetch` 和通用异步 I/O 还未暴露
- esbuild 只做转译/打包，不做 TypeScript 类型检查；`tsc` 仍是可选检查
- Android host 没有原生 esbuild；需要外部 binary 或 Node/WASM 方案
- 非 Linux-x64 平台需要对应内置 binary；当前已覆盖 22 个原生 host

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `include/huxerui/tsx.h` | 公开 `Runtime` / `RuntimeOptions` / `TsxRoot()` |
| `src/runtime.cpp` | QuickJS 生命周期、模块加载、hooks、VNode 遍历 |
| `src/elements.cpp` | 宿主标签到 `View` 的映射与事件 |
| `src/props.cpp` | props、颜色、modifier、key |
| `js/runtime.js` | 内置 `@huxerui/tsx` 模块：JSX runtime、hooks、宿主标签、`mount()` |
| `quickjs/` | 内置 Lib-QuickJS wrapper、quickjs-ng `v0.18.0`、engine tests |
| `third_party/esbuild/` | esbuild 0.25.0 全平台原生 binary + `SHA256SUMS` |
| `cmake/HuxerUITsx.cmake` | `huxerui_tsx_bundle()` / `huxerui_tsx_embed_header()` |
| `ci/esbuild-smoke/` | 跨平台 esbuild 内置 smoke 工程 |
| `examples/counter` | 可构建 TSX counter |
| `examples/preview` | 实时布局预览 + `Runtime::ReloadFile()` |
| `tests/` | state、嵌套 scope、effect、layout reload、compiled TSX |

## 验证

- Linux 本地构建 + `ctest` 4/4 通过（tsx tests、compiled bundle、embedded QuickJS tests）
- 测试覆盖 render/event/effect/timer 结构化诊断、组件栈、定时器、执行预算
- `examples/counter` / `examples/preview` 构建通过
- preview bundle 通过 headless `UiTest` 渲染、点击、重载检查
- GitHub Actions matrix：Linux x64/arm64、macOS x64/arm64、Windows x64/arm64
  按平台选择内置 esbuild，执行 TSX bundle 并验证产物
- TypeScript 声明可用 `tsc --strict` 检查；不是构建依赖

macOS、Windows、Android、iOS、Web 的完整 HuxerUI 应用构建仍需各自平台工具链验证。
