#include <huxerui/testing/ui_test.h>
#include <huxerui/tsx.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
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

} // namespace

int main() {
  Counter();
  NestedComponentState();
  Effects();
  LayoutReload();
  MemoAndRefs();
  ConditionalChildren();
  if (const char* bundle = std::getenv("HUXERUI_TSX_TEST_BUNDLE"); bundle != nullptr && *bundle != '\0') {
    CompiledTsx(bundle);
  }
  if (const char* bundle = std::getenv("HUXERUI_TSX_PREVIEW_BUNDLE"); bundle != nullptr && *bundle != '\0') {
    PreviewBundle(bundle);
  }
  std::fprintf(stderr, "all tsx tests passed\n");
  return 0;
}
