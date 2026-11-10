#include <huxerui/testing/ui_test.h>
#include <huxerui/tsx.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

using namespace huxerui;

namespace {

tsx::Runtime* g_runtime = nullptr;

View App() {
  return g_runtime->Root();
}

[[noreturn]] void Fail(const char* message) {
  std::fprintf(stderr, "tsx test failure: %s\n", message);
  std::exit(1);
}

void Check(bool condition, const char* message) {
  if (!condition) {
    Fail(message);
  }
}

void RunCase(
    const char* name,
    std::string bundle,
    const std::function<void(testing::UiTest&, tsx::Runtime&)>& check
) {
  std::fprintf(stderr, "== %s\n", name);
  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = std::move(bundle)});
  const auto loaded = runtime.LoadConfiguredBundle();
  if (!loaded.Succeeded()) {
    std::fprintf(stderr, "bundle error: %s\n", loaded.Error().Message().c_str());
    std::exit(1);
  }
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  check(ui, runtime);
}

void CompiledTsx(const char* path) {
  std::fprintf(stderr, "== compiled tsx (%s)\n", path);
  tsx::Runtime runtime;
  const auto loaded = runtime.LoadFile(path);
  if (!loaded.Succeeded()) {
    std::fprintf(stderr, "bundle error: %s\n", loaded.Error().Message().c_str());
    std::exit(1);
  }
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  Check(ui.Find(testing::UiSelector::Text("Count: 0")).Exists(), "compiled TSX should start at zero");
  ui.Find(testing::UiSelector::Key("increment")).Tap();
  Check(ui.Find(testing::UiSelector::Text("Count: 1")).Exists(), "compiled TSX increment should work");
  ui.Find(testing::UiSelector::Key("decrement")).Tap();
  Check(ui.Find(testing::UiSelector::Text("Count: 0")).Exists(), "compiled TSX decrement should work");
}

void PreviewBundle(const char* path) {
  std::fprintf(stderr, "== preview bundle (%s)\n", path);
  tsx::Runtime runtime;
  const auto loaded = runtime.LoadFile(path);
  if (!loaded.Succeeded()) {
    std::fprintf(stderr, "bundle error: %s\n", loaded.Error().Message().c_str());
    std::exit(1);
  }
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  Check(ui.Find(testing::UiSelector::Text("HuxerUI TSX")).Exists(), "preview should render its title");
  Check(ui.Find(testing::UiSelector::Text("Overview")).Exists(), "preview should render the selected tab");
  ui.Find(testing::UiSelector::Key("primary-action")).Tap();
  Check(ui.Find(testing::UiSelector::Text("Overview ✓")).Exists(), "preview state should update");

  const auto reloaded = runtime.ReloadFile(path);
  Check(reloaded.Succeeded(), "preview bundle should reload");
  ui.Pump(std::chrono::milliseconds(50));
  Check(ui.Find(testing::UiSelector::Text("Overview")).Exists(), "preview reload should reset state");
  Check(ui.Find(testing::UiSelector::Text("HuxerUI TSX")).Exists(), "preview reload should keep the layout");
}

void ShowcaseBundle(const char* path) {
  std::fprintf(stderr, "== showcase bundle (%s)\n", path);
  tsx::Runtime runtime;
  const auto loaded = runtime.LoadFile(path);
  if (!loaded.Succeeded()) {
    std::fprintf(stderr, "bundle error: %s\n", loaded.Error().Message().c_str());
    std::exit(1);
  }
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  Check(ui.Find(testing::UiSelector::Text("HuxerUI TSX")).Exists(),
        "showcase should render its title");
  Check(ui.Find(testing::UiSelector::Text("Overview")).Exists(),
        "showcase should render the overview page");

  ui.Find(testing::UiSelector::Text("Dialogs")).Tap();
  ui.Pump();
  if (const auto d = runtime.LastDiagnostic()) {
    std::fprintf(stderr, "showcase diagnostic: %s\n", d->description.c_str());
  }
  Check(ui.Find(testing::UiSelector::Text("Presentation APIs")).Exists(),
        "showcase should navigate to dialogs");

  ui.Find(testing::UiSelector::Text("Forms")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Key("showcase-field")).Exists(),
        "showcase should render the controlled TextField");
}

void Counter() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, Button, useState } from "@huxerui/tsx";

    function App() {
      const [count, setCount] = useState(0);
      return h(Column, {padding: 24, spacing: 12, crossAlign: "start"},
        h(Text, {role: "title", key: "count"}, "Count: " + count),
        h(Button, {key: "increment", onClick: () => setCount(count + 1)}, "Increment"),
        h(Button, {key: "update", onClick: () => setCount((value) => value + 10)}, "Increment by ten")
      );
    }

    mount(h(App));
  )JS";

  RunCase("counter", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    Check(ui.Find(testing::UiSelector::Text("Count: 0")).Exists(), "initial count should be zero");
    ui.Find(testing::UiSelector::Key("increment")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Count: 1")).Exists(), "click should increment count");
    ui.Find(testing::UiSelector::Key("update")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Count: 11")).Exists(), "updater should see the current value");
  });
}

void NestedComponentState() {
  const char* bundle = R"JS(
    import { mount, h, Column, Row, Text, Button, useState } from "@huxerui/tsx";

    function Counter(props) {
      const [value, setValue] = useState(0);
      return h(Row, {spacing: 8, key: "counter"},
        h(Text, {key: "label"}, props.label + ": " + value),
        h(Button, {key: "bump", onClick: () => setValue(value + 1)}, "+1")
      );
    }

    function App() {
      const [outer, setOuter] = useState(0);
      return h(Column, {spacing: 8},
        h(Button, {key: "outer", onClick: () => setOuter(outer + 1)}, "Outer: " + outer),
        h(Counter, {label: "Inner"})
      );
    }

    mount(h(App));
  )JS";

  RunCase("nested state", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    Check(ui.Find(testing::UiSelector::Text("Outer: 0")).Exists(), "outer should start at zero");
    Check(ui.Find(testing::UiSelector::Text("Inner: 0")).Exists(), "inner should start at zero");
    ui.Find(testing::UiSelector::Key("bump")).Tap();
    ui.Find(testing::UiSelector::Key("bump")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Inner: 2")).Exists(), "inner counter should be two");
    ui.Find(testing::UiSelector::Key("outer")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Outer: 1")).Exists(), "outer should be one");
    Check(ui.Find(testing::UiSelector::Text("Inner: 2")).Exists(), "rerendering the parent must preserve child state");
  });
}

void Effects() {
  const char* bundle = R"JS(
    import { mount, h, Button, useState, useEffect } from "@huxerui/tsx";

    function App() {
      const [value, setValue] = useState(0);
      useEffect(() => {
        globalThis.__effect_runs = (globalThis.__effect_runs || 0) + 1;
        return () => {
          globalThis.__effect_cleanups = (globalThis.__effect_cleanups || 0) + 1;
        };
      }, [value]);
      return h(Button, {key: "bump", onClick: () => setValue(value + 1)}, "Value: " + value);
    }

    mount(h(App));
  )JS";

  RunCase("effects", bundle, [](testing::UiTest& ui, tsx::Runtime& runtime) {
    auto read = [&runtime](const char* name) -> double {
      const auto result = runtime.Evaluate(name);
      if (!result.Succeeded()) {
        return -1.0;
      }
      const auto number = result.Value().ToNumber();
      return number.Succeeded() ? number.Value() : -1.0;
    };
    Check(read("globalThis.__effect_runs") == 1.0, "effect should run once after mount");
    ui.Find(testing::UiSelector::Key("bump")).Tap();
    Check(read("globalThis.__effect_runs") == 2.0, "effect should rerun after dependency change");
    Check(read("globalThis.__effect_cleanups") == 1.0, "previous effect should clean up first");
  });
}

void LayoutReload() {
  const char* first = R"JS(
    import { mount, h, Column, Text, Button, useState } from "@huxerui/tsx";

    function App() {
      const [count, setCount] = useState(0);
      return h(Column, {key: "root", padding: 8, spacing: 4},
        h(Text, {key: "version"}, "Version A"),
        h(Text, {key: "count"}, "Count: " + count),
        h(Button, {key: "bump", onClick: () => setCount(count + 1)}, "Bump")
      );
    }

    mount(h(App));
  )JS";

  const char* second = R"JS(
    import { mount, h, Column, Text, Button, useState } from "@huxerui/tsx";

    function App() {
      const [count, setCount] = useState(0);
      return h(Column, {key: "root", padding: 32, spacing: 4},
        h(Text, {key: "version"}, "Version B"),
        h(Text, {key: "count"}, "Count: " + count),
        h(Button, {key: "bump", onClick: () => setCount(count + 1)}, "Bump")
      );
    }

    mount(h(App));
  )JS";

  RunCase("layout reload", first, [second](testing::UiTest& ui, tsx::Runtime& runtime) {
    Check(ui.Find(testing::UiSelector::Text("Version A")).Exists(), "first layout should be mounted");
    ui.Find(testing::UiSelector::Key("bump")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Count: 1")).Exists(), "state should work before reload");
    const auto before = ui.Find(testing::UiSelector::Key("version")).One().bounds;

    const auto reloaded = runtime.Reload(second, "app.layout.1.js");
    if (!reloaded.Succeeded()) {
      std::fprintf(stderr, "reload error: %s\n", reloaded.Error().Message().c_str());
    }
    Check(reloaded.Succeeded(), "layout reload should succeed");
    ui.Pump(std::chrono::milliseconds(50));

    Check(ui.Find(testing::UiSelector::Text("Version B")).Exists(), "new layout should be mounted");
    Check(!ui.Find(testing::UiSelector::Text("Version A")).Exists(), "old layout should be unmounted");
    Check(ui.Find(testing::UiSelector::Text("Count: 0")).Exists(), "reload should reset component state");
    ui.Find(testing::UiSelector::Key("bump")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Count: 1")).Exists(), "new component hooks should work");
    const auto after = ui.Find(testing::UiSelector::Key("version")).One().bounds;
    Check(before.x != after.x || before.y != after.y, "padding change should move the layout");

    const auto failed = runtime.Reload("this is not javascript", "app.layout.bad.js");
    Check(!failed.Succeeded(), "invalid bundle should fail");
    ui.Pump(std::chrono::milliseconds(50));
    Check(ui.Find(testing::UiSelector::Text("Version B")).Exists(), "failed reload must keep the previous layout");
  });
}

void MemoAndRefs() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, Button, useState, useMemo, useRef } from "@huxerui/tsx";

    function App() {
      const [value, setValue] = useState(0);
      const [lazy] = useState(() => {
        globalThis.__lazy_calls = (globalThis.__lazy_calls || 0) + 1;
        return 7;
      });
      const renders = useRef(0);
      renders.current += 1;
      const doubled = useMemo(() => "Doubled: " + value * 2, [value]);
      return h(Column, {spacing: 8},
        h(Button, {key: "bump", onClick: () => setValue(value + 1)}, "Value: " + value),
        h(Text, {key: "memo"}, doubled),
        h(Text, {key: "ref"}, "Renders: " + renders.current),
        h(Text, {key: "lazy"}, "Lazy: " + lazy)
      );
    }

    mount(h(App));
  )JS";

  RunCase("memo and refs", bundle, [](testing::UiTest& ui, tsx::Runtime& runtime) {
    auto read = [&runtime](const char* name) -> double {
      const auto result = runtime.Evaluate(name);
      if (!result.Succeeded()) {
        return -1.0;
      }
      const auto number = result.Value().ToNumber();
      return number.Succeeded() ? number.Value() : -1.0;
    };
    Check(ui.Find(testing::UiSelector::Text("Doubled: 0")).Exists(), "memo should start at zero");
    Check(ui.Find(testing::UiSelector::Text("Lazy: 7")).Exists(), "lazy initializer value should be stored");
    Check(read("globalThis.__lazy_calls") == 1.0, "lazy initializer should run once");
    ui.Find(testing::UiSelector::Key("bump")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Doubled: 2")).Exists(), "memo should follow its dependency");
    Check(ui.Find(testing::UiSelector::Text("Renders: 2")).Exists(), "ref should persist across renders");
    Check(read("globalThis.__lazy_calls") == 1.0, "lazy initializer must not rerun");
  });
}

void ConditionalChildren() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, Button, useState } from "@huxerui/tsx";

    function App() {
      const [shown, setShown] = useState(false);
      return h(Column, {spacing: 8},
        h(Button, {key: "toggle", onClick: () => setShown(!shown)}, "Toggle"),
        shown ? h(Text, {key: "visible"}, "Visible now") : null,
        [h(Text, {key: "row-1"}, "One"), h(Text, {key: "row-2"}, "Two")]
      );
    }

    mount(h(App));
  )JS";

  RunCase("conditional and array children", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    Check(!ui.Find(testing::UiSelector::Text("Visible now")).Exists(), "conditional child should start hidden");
    Check(ui.Find(testing::UiSelector::Text("One")).Exists(), "array child one should render");
    Check(ui.Find(testing::UiSelector::Text("Two")).Exists(), "array child two should render");
    ui.Find(testing::UiSelector::Key("toggle")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Visible now")).Exists(), "conditional child should appear");
  });
}

void ThemeVariants() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, Theme } from "@huxerui/tsx";

    function App() {
      return h(Column, null,
        h(Theme, {key: "material", variant: "material"}, h(Text, null, "material")),
        h(Theme, {key: "material-dark", variant: "materialDark"}, h(Text, null, "material-dark")),
        h(Theme, {key: "flat", variant: "flat"}, h(Text, null, "flat")),
        h(Theme, {key: "flat-dark", variant: "flatDark"}, h(Text, null, "flat-dark")),
        h(Theme, {key: "dark-flag", dark: true}, h(Text, null, "dark-flag"))
      );
    }

    mount(h(App));
  )JS";

  RunCase("theme variants", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    Check(ui.Find(testing::UiSelector::Text("material")).Exists(), "MaterialTheme should render");
    Check(ui.Find(testing::UiSelector::Text("material-dark")).Exists(), "MaterialDarkTheme should render");
    Check(ui.Find(testing::UiSelector::Text("flat")).Exists(), "FlatTheme should render");
    Check(ui.Find(testing::UiSelector::Text("flat-dark")).Exists(), "FlatDarkTheme should render");
    Check(ui.Find(testing::UiSelector::Text("dark-flag")).Exists(), "dark prop should render");
  });
}

void TooltipProp() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text } from "@huxerui/tsx";

    function App() {
      return h(Column, {
        key: "tooltip-anchor",
        frame: { width: 200, height: 100 },
        background: "#ffffff",
        tooltip: "More information",
      }, h(Text, null, "Hover me"));
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "tooltip bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.SendPointer(PointerEvent{
      .type = PointerEventType::Move,
      .pointer_id = 17,
      .position = {20.0F, 20.0F},
      .device_kind = PointerDeviceKind::Mouse,
  });
  ui.PumpUntil([&] {
    return ui.Find(testing::UiSelector::Text("More information")).Exists();
  }, {.timeout = std::chrono::seconds(2)});
  Check(ui.Find(testing::UiSelector::Text("More information")).Exists(),
        "tooltip prop should present its message after hover delay");
}

void HoverInputEvent() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, useState } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("idle");
      return h(Column, {
        key: "hover",
        frame: { width: 200, height: 200 },
        background: "#ffffff",
        onHover: (event) => setStatus("hover:" + event.type),
        onPointerEnter: () => setStatus("enter"),
        onPointerLeave: () => setStatus("leave"),
      }, h(Text, null, status));
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "hover bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.SendPointer(PointerEvent{
      .type = PointerEventType::Move,
      .pointer_id = 13,
      .position = {20.0F, 20.0F},
      .device_kind = PointerDeviceKind::Mouse,
  });
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("enter")).Exists(),
        "mouse movement should enter a hover target");
}

void LongPressGestureEvent() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, useState } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("idle");
      return h(Column, {
        key: "long",
        frame: { width: 200, height: 200 },
        background: "#ffffff",
        longPressDuration: 0.1,
        onLongPressStart: () => setStatus("long-start"),
        onLongPressEnd: () => setStatus("long-end"),
        onLongPressCancel: () => setStatus("long-cancel"),
      }, h(Text, null, status));
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "long press bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.SendPointer(PointerEvent{
      .type = PointerEventType::Down,
      .pointer_id = 11,
      .position = {20.0F, 20.0F},
      .device_kind = PointerDeviceKind::Mouse,
      .changed_button = PointerButton::Primary,
      .pressed_buttons = PointerButton::Primary,
  });
  ui.Pump(std::chrono::milliseconds(250));
  ui.SendPointer(PointerEvent{
      .type = PointerEventType::Up,
      .pointer_id = 11,
      .position = {20.0F, 20.0F},
      .device_kind = PointerDeviceKind::Mouse,
      .changed_button = PointerButton::Primary,
      .pressed_buttons = PointerButton::None,
  });
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("long-end")).Exists(),
        "long press should emit started and ended");
}

void MultiTapGestureEvent() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, useState } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("idle");
      return h(Column, {
        key: "multi",
        frame: { width: 200, height: 200 },
        background: "#ffffff",
        multiTapCount: 2,
        onMultiTap: (event) => setStatus("multi:" + event.count),
      }, h(Text, null, status));
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "multi tap bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.TapAt({20.0F, 20.0F});
  ui.TapAt({20.0F, 20.0F});
  Check(ui.Find(testing::UiSelector::Text("multi:2")).Exists(),
        "multi tap gesture should report the configured tap count");
}

void DragGestureEvents() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, useState } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("idle");
      return h(Column, {
        key: "drag",
        frame: { width: 200, height: 200 },
        background: "#ffffff",
        dragThreshold: 2,
        onDragStart: () => setStatus("start"),
        onDrag: () => setStatus("drag"),
        onDragEnd: () => setStatus("end"),
        onDragCancel: () => setStatus("cancel"),
      }, h(Text, null, status));
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "drag bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.Drag({20.0F, 20.0F}, {120.0F, 20.0F}, {
      .duration = std::chrono::milliseconds(100),
      .segments = 4,
  });
  Check(ui.Find(testing::UiSelector::Text("end")).Exists(),
        "native drag gesture should emit started/changed/ended");
}

void VirtualListElement() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, VirtualList, Text,
    } from "@huxerui/tsx";

    const items = Array.from({ length: 100 }, (_, index) => "row " + index);

    function App() {
      return h(Column, { frame: { width: 200, height: 200 } },
        h(VirtualList, {
          items,
          itemExtent: 40,
          renderItem: (item, index) => h(Text, { key: index }, item),
        })
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "virtual list bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  Check(ui.Find(testing::UiSelector::Text("row 0")).Exists(),
        "VirtualList should materialize the first item");
}

void PopupPresentation() {
  const char* bundle = R"JS(
    import {
      mount, h, Button, Text, usePopup,
    } from "@huxerui/tsx";

    function App() {
      const popup = usePopup();
      return h(Button, {
        key: "popup",
        popupAnchor: popup,
        onClick: () => popup.show(() => h(Text, null, "Popup content")),
      }, "Open popup");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "popup bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("popup")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("Popup content")).Exists(),
        "Popup should render anchored TSX content");
}

void BottomSheetPresentation() {
  const char* bundle = R"JS(
    import {
      mount, h, Button, Text, useBottomSheet,
    } from "@huxerui/tsx";

    function App() {
      const sheet = useBottomSheet();
      return h(Button, {
        key: "bottom-sheet",
        onClick: () => sheet.show(() => h(Text, null, "Sheet content")),
      }, "Open sheet");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "bottom sheet bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("bottom-sheet")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("Sheet content")).Exists(),
        "BottomSheet should render custom TSX content");
}

void SnackBarPresentation() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Button, Text, useState, useSnackBar,
    } from "@huxerui/tsx";

    function App() {
      const snackBar = useSnackBar();
      const [status, setStatus] = useState("idle");
      return h(Column, null,
        h(Button, {
          key: "snackbar",
          onClick: () => snackBar.showAction(
            "Deleted",
            "Undo",
            () => setStatus("undo"),
            2
          ),
        }, "Show snackbar"),
        h(Text, { key: "snackbar-status" }, status)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "snackbar bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("snackbar")).Tap();
  ui.Pump();
  Check(!runtime.LastDiagnostic().has_value(), "snackbar showAction should not report an error");
}

void AnchoredMenuPresentation() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Button, Text, useState, useMenu,
    } from "@huxerui/tsx";

    function App() {
      const menu = useMenu();
      const [status, setStatus] = useState("idle");
      return h(Column, null,
        h(Button, {
          key: "menu-anchor",
          menuAnchor: menu,
          onClick: () => {
            const layer = menu.show([
              { label: "Rename", onSelect: () => setStatus("rename") },
            ]);
            if (layer > 0) {
              setStatus("opened");
            }
          },
        }, "Open anchored menu"),
        h(Text, { key: "anchored-status" }, status)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "anchored menu bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("menu-anchor")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("opened")).Exists(),
        "anchored MenuRef.show should return a live layer id");
}

void MenuPresentation() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Button, Text, useState, useMenu,
    } from "@huxerui/tsx";

    function App() {
      const menu = useMenu();
      const [status, setStatus] = useState("idle");
      return h(Column, null,
        h(Button, {
          key: "menu",
          onClick: () => {
            const layer = menu.showAt(20, 20, [
              { label: "Rename", onSelect: () => setStatus("rename") },
              { section: true },
              { label: "Delete", disabled: true },
            ]);
            if (layer > 0) {
              setStatus("opened");
            }
          },
        }, "Open menu"),
        h(Text, { key: "menu-status" }, status)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "menu bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("menu")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("opened")).Exists(),
        "Menu showAt should return a live layer id");
}

void CustomDialogContent() {
  const char* bundle = R"JS(
    import {
      mount, h, Button, Text, useDialog,
    } from "@huxerui/tsx";

    function App() {
      const dialog = useDialog();
      return h(Button, {
        key: "custom-dialog",
        onClick: () => dialog.showContent(() => h(Text, null, "Custom dialog")),
      }, "Open custom dialog");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "custom dialog bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("custom-dialog")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("Custom dialog")).Exists(),
        "Dialog showContent should render its TSX content");
}

void DialogPresentation() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Button, Text, useState, useDialog,
    } from "@huxerui/tsx";

    function App() {
      const dialog = useDialog();
      const [status, setStatus] = useState("idle");
      return h(Column, null,
        h(Button, {
          key: "dialog",
          onClick: () => dialog.show({
            title: "Delete?",
            message: "This cannot be undone.",
            positive: "Delete",
            negative: "Cancel",
            onPositive: () => setStatus("positive"),
            onNegative: () => setStatus("negative"),
          }),
        }, "Open dialog"),
        h(Text, { key: "dialog-status" }, status)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "dialog bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("dialog")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("Delete?")).Exists(),
        "Dialog should present its title");
  ui.Find(testing::UiSelector::Text("Delete")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("positive")).Exists(),
        "Dialog positive action should invoke its JS callback");
}

void ToastPresentation() {
  const char* bundle = R"JS(
    import { mount, h, Button, useToast } from "@huxerui/tsx";

    function App() {
      const toast = useToast();
      return h(Button, {
        key: "toast",
        onClick: () => toast.show("saved", 1),
      }, "Show toast");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "toast bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("toast")).Tap();
  ui.Pump();
  Check(!runtime.LastDiagnostic().has_value(), "toast show should not report an error");
}

void VirtualGridElement() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, VirtualGrid, Text,
    } from "@huxerui/tsx";

    const items = Array.from({ length: 100 }, (_, index) => "tile " + index);

    function App() {
      return h(Column, { frame: { width: 300, height: 300 } },
        h(VirtualGrid, {
          items,
          columns: 2,
          rowExtent: 50,
          rowSpacing: 4,
          columnSpacing: 4,
          renderItem: (item, index) => h(Text, { key: index }, item),
        })
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "virtual grid bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  Check(ui.Find(testing::UiSelector::Text("tile 0")).Exists(),
        "VirtualGrid should materialize the first tile");
}

void WriteNavigationIcon() {
  static const unsigned char kPng[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
      0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x04, 0x00, 0x00, 0x00, 0xB5, 0x1C, 0x0C, 0x02,
      0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54,
      0x78, 0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00, 0x01, 0x05,
      0x01, 0x01, 0x27, 0x18, 0xE3, 0x66, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
  };
  std::ofstream output("/tmp/huxerui-tsx-navigation-icon.png", std::ios::binary);
  output.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
}

void NavigationBarElement() {
  WriteNavigationIcon();
  const char* bundle = R"JS(
    import {
      mount, h, Column, NavigationBar, Text, useState,
    } from "@huxerui/tsx";

    const items = [
      { label: "Home", src: "/tmp/huxerui-tsx-navigation-icon.png" },
      { label: "Settings", src: "/tmp/huxerui-tsx-navigation-icon.png" },
    ];

    function App() {
      const [selected, setSelected] = useState(0);
      return h(Column, null,
        h(NavigationBar, {
          items,
          selected,
          onChange: setSelected,
        }),
        h(Text, { key: "navigation-bar" }, "selected:" + selected)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "navigation bar bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Text("Settings")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("selected:1")).Exists(),
        "NavigationBar should report controlled selection changes");
}

void NavigationPaneElement() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, NavigationPane, Text, useState,
    } from "@huxerui/tsx";

    function App() {
      const [selected, setSelected] = useState(0);
      return h(Column, null,
        h(NavigationPane, {
          labels: ["Home", "Settings"],
          expanded: true,
          selected,
          onChange: setSelected,
        }),
        h(Text, { key: "navigation" }, "selected:" + selected)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "navigation pane bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Text("Settings")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("selected:1")).Exists(),
        "NavigationPane should report controlled selection changes");
}

void TabsAndSegmentedButton() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Tabs, SegmentedButton, Text, useState,
    } from "@huxerui/tsx";

    function App() {
      const [tab, setTab] = useState(0);
      const [segment, setSegment] = useState(0);
      return h(Column, null,
        h(Tabs, {
          labels: ["One", "Two"],
          selected: tab,
          onChange: setTab,
        }),
        h(SegmentedButton, {
          labels: ["Day", "Week"],
          selected: segment,
          onChange: setSegment,
        }),
        h(Text, { key: "selection" }, "tab:" + tab + " seg:" + segment)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "tabs bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.Find(testing::UiSelector::Text("Two")).Tap();
  ui.Pump();
  ui.Find(testing::UiSelector::Text("Week")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("tab:1 seg:1")).Exists(),
        "Tabs and SegmentedButton should report controlled index changes");
}

void TextFieldEditEvent() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Button, TextField, Text, useState, useFocusRef,
    } from "@huxerui/tsx";

    function App() {
      const field = useFocusRef();
      const [value, setValue] = useState("ab");
      const [edit, setEdit] = useState("none");
      return h(Column, null,
        h(Button, {
          key: "focus-field",
          onClick: () => field.requestFocus(),
        }, "Focus"),
        h(TextField, {
          key: "field",
          ref: field,
          value,
          selection: { anchor: 2, active: 2 },
          onChange: setValue,
          onEdit: (event) => setEdit("edit:" + event.text),
        }),
        h(Text, { key: "edit-status" }, edit)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "TextField edit bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.Find(testing::UiSelector::Key("focus-field")).Tap();
  ui.Pump();
  const auto session = ui.ActiveTextInput();
  Check(session.has_value(), "focused TextField should expose an active input session");

  TextInputCommand commit;
  commit.kind = TextInputCommandKind::CommitText;
  commit.text = "x";
  const TextInputCommandBatch batch{session->session_id, {commit}};
  const auto applied = ui.SendTextInput(batch);
  Check(applied.result_code == TextInputResultCode::Ok, "text input commit should be accepted");
  ui.Pump();

  Check(ui.Find(testing::UiSelector::Key("field")).One().value == std::optional<std::string>("abx"),
        "TextField onChange should write the new complete value");
  Check(ui.Find(testing::UiSelector::Text("edit:abx")).Exists(),
        "TextField onEdit should expose the complete edited text");
}

void ScrollRef() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, ScrollView, Text, Button, useState, useScrollRef,
    } from "@huxerui/tsx";

    function App() {
      const scroll = useScrollRef();
      const [status, setStatus] = useState("idle");
      return h(Column, null,
        h(Button, {
          key: "scroll-button",
          onClick: () => {
            const metrics = scroll.metrics();
            setStatus(
              scroll.scrollTo(50) &&
              metrics !== undefined &&
              metrics.connected &&
              metrics.maxOffset > 0
                ? "moved"
                : "idle"
            );
          },
        }, "Scroll"),
        h(ScrollView, {
          key: "scroll",
          frame: { width: 100, height: 100 },
          scrollRef: scroll,
        },
          h(Column, { frame: { width: 100, height: 500 } },
            h(Text, null, "content")
          )
        ),
        h(Text, { key: "scroll-status" }, status)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "scroll ref bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("scroll-button")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("moved")).Exists(),
        "useScrollRef().scrollTo should move a bound ScrollView");
}

void FormProps() {
  const char* bundle = R"JS(
    import { mount, h, TextField } from "@huxerui/tsx";

    function App() {
      return h(TextField, {
        key: "field",
        value: "abc",
        label: "Name",
        selection: { anchor: 1, active: 2, affinity: "upstream" },
        validation: { status: "invalid", message: "Required" },
      });
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "form props bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  const auto field = ui.Find(testing::UiSelector::Key("field")).One();
  Check(field.value == std::optional<std::string>("abc"),
        "TextField value should be controlled by TSX");
}

void InputEvents() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text, useState } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("idle");
      const [key, setKey] = useState("none");
      return h(Column, {
        key: "surface",
        frame: { width: 200, height: 200 },
        padding: 10,
        focusable: true,
        background: "#ffffff",
        onPointerDown: (event) => {
          setStatus("pointer:" + event.type + ":" + event.device + ":" +
                    event.button + ":" + event.buttons);
        },
        onScroll: (event) => {
          setStatus("scroll:" + event.deltaY);
          return true;
        },
        onKeyDown: (event) => {
          setKey("key:" + event.type + ":" + event.key);
          return true;
        },
      },
        h(Text, { key: "status" }, status),
        h(Text, { key: "key" }, key)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "input event bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.TapAt({30.0F, 30.0F}, {.device_kind = PointerDeviceKind::Mouse, .pointer_id = 7});
  Check(ui.Find(testing::UiSelector::Text("pointer:down:mouse:primary:1")).Exists(),
        "pointer event should expose phase, device, and button");

  const auto consumed = ui.SendScroll({
      .position = {30.0F, 30.0F},
      .delta_y = 12.0F,
  });
  ui.Pump();
  Check(consumed.y == 12.0F, "scroll handler should consume the requested delta");
  Check(ui.Find(testing::UiSelector::Text("scroll:12")).Exists(),
        "scroll event should expose deltas");

  ui.SendKey(KeyEvent{.type = KeyEventType::Down, .key = Key::A, .text = "a"});
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("key:down:a")).Exists(),
        "key event should reach the focused view");
}

void FocusRef() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Button, TextField, useFocusRef,
    } from "@huxerui/tsx";

    function App() {
      const field = useFocusRef();
      return h(Column, null,
        h(Button, {
          key: "focus",
          onClick: () => field.requestFocus(),
        }, "Focus"),
        h(TextField, {
          key: "field",
          ref: field,
        }, "")
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "focus ref bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  ui.Find(testing::UiSelector::Key("focus")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Key("field")).One().focused,
        "useFocusRef().requestFocus should focus the bound TextField");
}

void ConditionalComponentIdentity() {
  const char* bundle = R"JS(
    import { mount, h, Button, Column, Text, useState } from "@huxerui/tsx";

    function A() {
      const [value] = useState("A-state");
      return h(Text, null, value);
    }

    function B() {
      const [value] = useState("B-state");
      return h(Text, null, value);
    }

    function App() {
      const [showA, setShowA] = useState(true);
      return h(Column, null,
        h(Button, { key: "switch", onClick: () => setShowA(!showA) }, "Switch"),
        showA ? h(A) : h(B)
      );
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "conditional identity bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  Check(ui.Find(testing::UiSelector::Text("A-state")).Exists(),
        "first component should own its state");
  ui.Find(testing::UiSelector::Key("switch")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("B-state")).Exists(),
        "switching component type should not reuse the previous hook state");
  ui.Find(testing::UiSelector::Key("switch")).Tap();
  ui.Pump();
  Check(ui.Find(testing::UiSelector::Text("A-state")).Exists(),
        "switching back should create the first component state again");
}

void RenderDiagnostic() {
  const char* bundle = R"JS(
    import { mount, h, Column, Text } from "@huxerui/tsx";

    function Leaf() {
      throw new Error("leaf boom");
    }

    function App() {
      return h(Column, null,
        h(Text, null, "before"),
        h(Leaf)
      );
    }

    mount(h(App));
  )JS";

  std::optional<tsx::Diagnostic> observed;
  std::string formatted;
  tsx::RuntimeOptions options;
  options.bundle = bundle;
  options.on_diagnostic = [&](const tsx::Diagnostic& value) { observed = value; };
  options.on_error = [&](std::string_view value) { formatted = std::string(value); };
  tsx::Runtime runtime(std::move(options));
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "render diagnostic bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "render failure should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Render, "render failure phase should be render");
  Check(diagnostic->code == quickjs::ErrorCode::Exception, "render failure should be a JS exception");
  Check(diagnostic->message.find("leaf boom") != std::string::npos, "render failure should keep the message");
  Check(diagnostic->component_stack.find("App") != std::string::npos,
        "render diagnostic should include the outer component");
  Check(diagnostic->component_stack.find("Leaf") != std::string::npos,
        "render diagnostic should include the failing component");
  Check(diagnostic->description.find("component stack:") != std::string::npos,
        "formatted diagnostic should include the component stack");
  Check(observed.has_value(), "on_diagnostic should receive the structured failure");
  Check(observed->phase == tsx::DiagnosticPhase::Render,
        "on_diagnostic should receive the right phase");
  Check(formatted == diagnostic->description,
        "on_error should receive the same formatted description");
}

void EventDiagnostic() {
  const char* bundle = R"JS(
    import { mount, h, Button } from "@huxerui/tsx";

    function App() {
      return h(Button, {
        key: "explode",
        onClick: () => { throw new Error("click boom"); },
      }, "Explode");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "event diagnostic bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.Find(testing::UiSelector::Key("explode")).Tap();

  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "event failure should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Event, "event failure phase should be event");
  Check(diagnostic->message.find("click boom") != std::string::npos,
        "event failure should keep the message");
  Check(diagnostic->component_stack.find("App") != std::string::npos,
        "event diagnostic should include the owning component");
}

void EffectDiagnostic() {
  const char* bundle = R"JS(
    import { mount, h, Text, useEffect } from "@huxerui/tsx";

    function App() {
      useEffect(() => {
        throw new Error("effect boom");
      }, []);
      return h(Text, null, "effect");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "effect diagnostic bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "effect failure should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Effect, "effect failure phase should be effect");
  Check(diagnostic->message.find("effect boom") != std::string::npos,
        "effect failure should keep the message");
  Check(diagnostic->component_stack.find("App") != std::string::npos,
        "effect diagnostic should include the owning component");
}

void LoadDiagnostic() {
  tsx::Runtime runtime;
  const auto loaded = runtime.Load("const = ;", "broken.js");
  Check(!loaded.Succeeded(), "syntax error should fail");
  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "load failure should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Load, "load failure phase should be load");
  Check(diagnostic->code == quickjs::ErrorCode::Syntax, "load failure should be a syntax error");
  Check(diagnostic->filename == "broken.js", "load diagnostic should preserve the filename");
  Check(diagnostic->line > 0, "load diagnostic should preserve the line");
  Check(diagnostic->generated_filename == "broken.js",
        "unmapped diagnostics should keep the generated filename");
}

void SourceMapDiagnostic(const char* path) {
  std::fprintf(stderr, "== source map diagnostic (%s)\n", path);
  tsx::Runtime runtime;
  const auto loaded = runtime.LoadFile(path);
  Check(loaded.Succeeded(), "source map bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  const std::string generated_filename = std::filesystem::path(path).filename().string();
  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "source map failure should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Render,
        "source map failure phase should be render");
  Check(diagnostic->message.find("mapped boom") != std::string::npos,
        "source map failure should keep the message");
  Check(diagnostic->filename.find("source_map.tsx") != std::string::npos,
        "source map diagnostic should point at the original TSX file");
  Check(diagnostic->line == 4, "source map diagnostic should point at the throw line");
  Check(diagnostic->column > 0, "source map diagnostic should keep a source column");
  Check(diagnostic->generated_filename == generated_filename,
        "source map diagnostic should retain the generated filename");
  Check(diagnostic->generated_line > 0,
        "source map diagnostic should retain the generated line");
  Check(diagnostic->stack.find("source_map.tsx") != std::string::npos,
        "remapped stack should name the original TSX file");
  Check(diagnostic->stack.find(generated_filename + ":") == std::string::npos,
        "remapped stack should not keep generated bundle frames");
  Check(diagnostic->description.find("source_map.tsx") != std::string::npos,
        "formatted diagnostic should name the original TSX file");
}

void ServiceBridge() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Text, useState, useEffect,
      clipboardAvailable, readClipboard, applicationLifecycle,
    } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("pending");
      useEffect(() => {
        const available = clipboardAvailable();
        const read = readClipboard();
        const lifecycle = applicationLifecycle();
        setStatus(
          typeof available === "boolean" &&
          (read === null || typeof read === "string") &&
          typeof lifecycle === "string" &&
          lifecycle.length > 0
            ? "services-ok"
            : "services-failed"
        );
      }, []);
      return h(Column, null, h(Text, {key: "services-status"}, status));
    }

    mount(h(App));
  )JS";

  RunCase("HUI services", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    ui.Pump();
    Check(ui.Find(testing::UiSelector::Text("services-ok")).Exists(),
          "clipboard and application lifecycle services should be callable");
  });
}

void NavigationStackTest() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Text, Button, useState,
      useNavigation, NavigationStack,
    } from "@huxerui/tsx";

    function Home({navigation}) {
      const [count, setCount] = useState(0);
      return h(Column, null,
        h(Text, {key: "home"}, "Home " + count),
        h(Button, {key: "increment", onClick: () => setCount(count + 1)}, "Increment"),
        h(Button, {
          key: "push",
          onClick: () => navigation.push(() => h(Detail, {navigation})),
        }, "Details")
      );
    }

    function Detail({navigation}) {
      return h(Column, null,
        h(Text, {key: "detail"}, "Detail"),
        h(Button, {key: "pop", onClick: () => navigation.pop()}, "Back")
      );
    }

    function App() {
      const navigation = useNavigation(() => h(Home, {navigation}));
      return h(NavigationStack, {navigation});
    }

    mount(h(App));
  )JS";

  RunCase("navigation stack", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    Check(ui.Find(testing::UiSelector::Text("Home 0")).Exists(),
          "navigation stack should mount its root page");
    ui.Find(testing::UiSelector::Key("increment")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Home 1")).Exists(),
          "root page state should update before navigation");
    ui.Find(testing::UiSelector::Key("push")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Detail")).Exists(),
          "push should add and select a page");
    ui.Find(testing::UiSelector::Key("pop")).Tap();
    Check(ui.Find(testing::UiSelector::Text("Home 1")).Exists(),
          "pop should return to the retained root page with its state");
  });
}

void FormComponents() {
  const char* bundle = R"JS(
    import {
      mount, h, Column, Text, RefreshBox, Pager, SplitLayout, TopAppBar,
    } from "@huxerui/tsx";

    function App() {
      return h(Column, {padding: 8, spacing: 8},
        h(TopAppBar, {key: "bar", title: "Components"}),
        h(RefreshBox, {key: "refresh", refreshing: false},
          h(Text, {key: "refresh-child"}, "Refresh content")
        ),
        h(Pager, {key: "pager", selected: 0},
          h(Text, {key: "page-1"}, "Page one"),
          h(Text, {key: "page-2"}, "Page two")
        ),
        h(SplitLayout, {key: "split", axis: "horizontal"},
          h(Text, {key: "pane-1", grow: 1}, "Pane one"),
          h(Text, {key: "pane-2", grow: 1}, "Pane two")
        )
      );
    }

    mount(h(App));
  )JS";

  RunCase("form components", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    Check(ui.Find(testing::UiSelector::Key("refresh")).Exists(), "RefreshBox should render");
    Check(ui.Find(testing::UiSelector::Key("pager")).Exists(), "Pager should render");
    Check(ui.Find(testing::UiSelector::Key("split")).Exists(), "SplitLayout should render");
    Check(ui.Find(testing::UiSelector::Text("Components")).Exists(),
          "TopAppBar should render its title");
    Check(ui.Find(testing::UiSelector::Text("Refresh content")).Exists(),
          "RefreshBox should render its content");
    Check(ui.Find(testing::UiSelector::Text("Page one")).Exists(),
          "Pager should render its selected page");
    Check(ui.Find(testing::UiSelector::Text("Pane one")).Exists(),
          "SplitLayout should render its first pane");
    Check(ui.Find(testing::UiSelector::Text("Pane two")).Exists(),
          "SplitLayout should render its second pane");
  });
}

void SemanticsModifier() {
  const char* bundle = R"JS(
    import { mount, h, Text } from "@huxerui/tsx";

    function App() {
      return h(Text, {
        key: "semantic-save",
        semantics: {
          role: "button",
          label: "Save changes",
          hint: "Writes the file",
          identifier: "save",
        },
      }, "Save");
    }

    mount(h(App));
  )JS";

  RunCase("semantics modifier", bundle, [](testing::UiTest& ui, tsx::Runtime&) {
    const auto match = ui.FindSemantics(testing::UiSemanticSelector::AllOf(
        testing::UiSemanticSelector::Role(SemanticRole::Button),
        testing::UiSemanticSelector::Label("Save changes"),
        testing::UiSemanticSelector::Identifier("save")
    ));
    Check(match.Exists(), "semantics should publish role, label, and identifier");
  });
}

void FetchRejection() {
  const char* bundle = R"JS(
    import {
      mount, h, Text, useState, useEffect, fetch,
    } from "@huxerui/tsx";

    function App() {
      const [status, setStatus] = useState("pending");
      useEffect(() => {
        fetch("not-a-valid-url").then(
          () => setStatus("resolved"),
          () => setStatus("rejected")
        );
      }, []);
      return h(Text, {key: "fetch-status"}, status);
    }

    mount(h(App));
  )JS";

  RunCase("fetch rejection", bundle, [](testing::UiTest& ui, tsx::Runtime& runtime) {
    for (int attempt = 0; attempt < 5; ++attempt) {
      ui.Pump(std::chrono::milliseconds(20));
    }
    if (const auto diagnostic = runtime.LastDiagnostic()) {
      std::fprintf(stderr, "fetch diagnostic: %s\n", diagnostic->description.c_str());
    }
    Check(ui.Find(testing::UiSelector::Text("rejected")).Exists(),
          "invalid fetch requests should reject their Promise");
  });
}

void TimerCallbacks() {
  const char* bundle = R"JS(
    import {
      mount, h, Text, useEffect, useState,
      setTimeout, clearTimeout, setInterval, clearInterval,
    } from "@huxerui/tsx";

    function App() {
      const [value, setValue] = useState("before");
      useEffect(() => {
        const timeout = setTimeout(() => setValue("after"), 10);
        const interval = setInterval(() => {
          setValue((current) => current === "after" ? "tick" : current);
        }, 20);
        return () => {
          clearTimeout(timeout);
          clearInterval(interval);
        };
      }, []);
      return h(Text, null, "timer: " + value);
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "timer bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  Check(ui.Find(testing::UiSelector::Text("timer: before")).Exists(),
        "timer should start before the timeout");
  ui.PumpUntil([&] {
    return ui.Find(testing::UiSelector::Text("timer: tick")).Exists();
  });
  Check(ui.Find(testing::UiSelector::Text("timer: tick")).Exists(),
        "timeout and interval should update state");
}

void TimerDiagnostic() {
  const char* bundle = R"JS(
    import { mount, h, Text, useEffect } from "@huxerui/tsx";

    function App() {
      useEffect(() => {
        setTimeout(() => { throw new Error("timer boom"); }, 1);
      }, []);
      return h(Text, null, "timer");
    }

    mount(h(App));
  )JS";

  tsx::Runtime runtime(tsx::RuntimeOptions{.bundle = bundle});
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "timer diagnostic bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);
  ui.PumpUntil([&] { return runtime.LastDiagnostic().has_value(); });

  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "timer failure should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Timer, "timer failure phase should be timer");
  Check(diagnostic->message.find("timer boom") != std::string::npos,
        "timer failure should keep the message");
  Check(diagnostic->component_stack.find("App") != std::string::npos,
        "timer diagnostic should include the owning component");
}

void OperationBudget() {
  const char* bundle = R"JS(
    import { mount, h, Text } from "@huxerui/tsx";

    function App() {
      while (true) {}
      return h(Text, null, "never");
    }

    mount(h(App));
  )JS";

  tsx::RuntimeOptions options;
  options.bundle = bundle;
  options.operation_time_limit = std::chrono::milliseconds(30);
  tsx::Runtime runtime(std::move(options));
  const auto loaded = runtime.LoadConfiguredBundle();
  Check(loaded.Succeeded(), "operation budget bundle should load");
  g_runtime = &runtime;
  const Application application{App};
  testing::UiTest ui(application);

  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "operation budget should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Render,
        "operation budget failure phase should be render");
  Check(diagnostic->code == quickjs::ErrorCode::Interrupted,
        "operation budget failure should be Interrupted");
}

void ExecutionBudget() {
  tsx::RuntimeOptions options;
  options.bundle = "while (true) {}";
  options.evaluation_time_limit = std::chrono::milliseconds(30);
  tsx::Runtime runtime(std::move(options));

  const auto loaded = runtime.LoadConfiguredBundle();
  Check(!loaded.Succeeded(), "execution budget should stop an infinite module");
  Check(loaded.Error().Code() == quickjs::ErrorCode::Interrupted,
        "execution budget should report Interrupted");
  const auto diagnostic = runtime.LastDiagnostic();
  Check(diagnostic.has_value(), "execution budget should produce a diagnostic");
  Check(diagnostic->phase == tsx::DiagnosticPhase::Load, "budget failure phase should be load");
  Check(diagnostic->code == quickjs::ErrorCode::Interrupted,
        "budget diagnostic should preserve Interrupted");
}

} // namespace

int main(int argc, char** argv) {
  std::string_view selected;
  if (argc == 3 && std::string_view(argv[1]) == "--case") {
    selected = argv[2];
  } else if (argc != 1) {
    std::fprintf(stderr, "usage: %s [--case NAME]\n", argv[0]);
    return 2;
  }

  struct TestCase {
    const char* name;
    void (*run)();
  };
  static constexpr TestCase kCases[] = {
#define HUXERUI_TSX_TEST(name) {#name, name},
#include "test_cases.inc"
#undef HUXERUI_TSX_TEST
  };

  bool ran = false;
  for (const auto& test : kCases) {
    if (!selected.empty() && selected != test.name) {
      continue;
    }
    test.run();
    ran = true;
  }
  if (!selected.empty()) {
    if (!ran) {
      std::fprintf(
          stderr,
          "unknown test case: %.*s\n",
          static_cast<int>(selected.size()),
          selected.data()
      );
      return 2;
    }
    return 0;
  }

  if (const char* bundle = std::getenv("HUXERUI_TSX_TEST_BUNDLE"); bundle != nullptr && *bundle != '\0') {
    CompiledTsx(bundle);
  }
  if (const char* bundle = std::getenv("HUXERUI_TSX_SOURCE_MAP_BUNDLE"); bundle != nullptr && *bundle != '\0') {
    SourceMapDiagnostic(bundle);
  }
  if (const char* bundle = std::getenv("HUXERUI_TSX_PREVIEW_BUNDLE"); bundle != nullptr && *bundle != '\0') {
    PreviewBundle(bundle);
  }
  if (const char* bundle = std::getenv("HUXERUI_TSX_SHOWCASE_BUNDLE"); bundle != nullptr && *bundle != '\0') {
    ShowcaseBundle(bundle);
  }
  std::fprintf(stderr, "all tsx tests passed\n");
  return 0;
}
