#include <huxerui/quickjs.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quickjs = huxerui::quickjs;

namespace {

#define CHECK(condition)                                                                            \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      std::fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__);           \
      std::abort();                                                                                 \
    }                                                                                               \
  } while (false)

template <class T> T Unwrap(quickjs::Result<T> result) {
  if (!result.Succeeded()) {
    std::fprintf(
        stderr,
        "unexpected failure: %s (%s:%d:%d)\n",
        result.Error().Message().c_str(),
        result.Error().Filename().c_str(),
        result.Error().Line(),
        result.Error().Column()
    );
    std::abort();
  }
  return std::move(result).Value();
}

void Require(quickjs::Result<void> result) {
  if (!result.Succeeded()) {
    std::fprintf(stderr, "unexpected failure: %s\n", result.Error().Message().c_str());
    std::abort();
  }
}

void EvaluateScripts(quickjs::Context& context) {
  {
    auto result = context.Evaluate("1 + 2");
    CHECK(result.Succeeded());
    CHECK(result.Value().IsNumber());
    CHECK(Unwrap(result.Value().ToNumber()) == 3.0);
  }
  {
    auto result = context.Evaluate("'huxerui' + ':' + (6 * 7)");
    CHECK(result.Succeeded());
    CHECK(Unwrap(result.Value().ToString()) == "huxerui:42");
  }
  {
    auto result = context.Evaluate("undefined");
    CHECK(result.Succeeded());
    CHECK(result.Value().IsUndefined());
    CHECK(Unwrap(result.Value().ToString()) == "undefined");
  }
  {
    auto result = context.Evaluate("({ answer: 42, nested: { ok: true } })");
    CHECK(result.Succeeded());
    CHECK(Unwrap(result.Value().ToJson()) == R"({"answer":42,"nested":{"ok":true}})");
    CHECK(Unwrap(result.Value().ToJson("  ")) == "{\n  \"answer\": 42,\n  \"nested\": {\n    \"ok\": true\n  }\n}");
  }
}

void EvaluateErrors(quickjs::Context& context) {
  {
    auto result = context.Evaluate("missingName + 1", "app.js");
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::Exception);
    CHECK(result.Error().Filename() == "app.js");
    CHECK(result.Error().Line() == 1);
    CHECK(result.Error().Message().find("missingName") != std::string::npos);
  }
  {
    auto result = context.Evaluate("function (", "syntax.js");
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::Syntax);
    CHECK(result.Error().Filename() == "syntax.js");
  }
  {
    auto result = context.Evaluate("throw new Error('thrown')", "throw.js");
    CHECK(!result.Succeeded());
    CHECK(result.Error().Message() == "thrown");
    CHECK(result.Error().Stack().find("throw.js") != std::string::npos);
  }
}

void GlobalsAndFunctions(quickjs::Context& context) {
  Require(context.SetGlobal("count", std::int32_t{41}));
  Require(context.SetGlobal("label", "huxerui"));
  Require(context.SetGlobal("enabled", true));
  Require(context.SetGlobal("ratio", 1.5));

  CHECK(Unwrap(Unwrap(context.Global("count")).ToInt32()) == 41);
  CHECK(Unwrap(Unwrap(context.Global("label")).ToString()) == "huxerui");
  CHECK(Unwrap(Unwrap(context.Global("enabled")).ToBoolean()));
  CHECK(Unwrap(Unwrap(context.Global("ratio")).ToNumber()) == 1.5);

  Require(context.SetGlobalFunction(
      "sum",
      [](quickjs::Context& owner, std::span<const quickjs::Value> arguments) -> quickjs::Value {
        if (arguments.size() != 2) {
          throw std::invalid_argument("sum expects two arguments");
        }
        return owner.Number(Unwrap(arguments[0].ToNumber()) + Unwrap(arguments[1].ToNumber()));
      },
      2
  ));
  CHECK(Unwrap(Unwrap(context.Evaluate("sum(20, 22)")).ToNumber()) == 42.0);

  Require(context.SetGlobalFunction(
      "fail",
      [](quickjs::Context&, std::span<const quickjs::Value>) -> quickjs::Value {
        throw std::runtime_error("native failure");
      }
  ));
  CHECK(Unwrap(Unwrap(context.Evaluate("try { fail(); } catch (error) { error.message }")).ToString())
        == "native failure");

  CHECK(Unwrap(Unwrap(context.Evaluate("count += 1; count")).ToInt32()) == 42);
}

void ObjectsAndJson(quickjs::Context& context) {
  auto object = Unwrap(context.NewObject());
  Require(object.SetProperty("name", Unwrap(context.NewString("Ada"))));
  Require(object.SetProperty("active", context.Boolean(true)));

  CHECK(Unwrap(Unwrap(object.GetProperty("name")).ToString()) == "Ada");
  CHECK(Unwrap(object.HasProperty("active")));
  CHECK(!Unwrap(object.HasProperty("missing")));
  CHECK(Unwrap(object.PropertyNames())
        == (std::vector<std::string>{"name", "active"}));

  auto array = Unwrap(context.NewArray(std::vector<quickjs::Value>{object, context.Number(2)}));
  CHECK(Unwrap(array.Length()) == 2);
  CHECK(Unwrap(Unwrap(array.GetElement(1)).ToInt32()) == 2);
  Require(array.SetElement(2, Unwrap(context.NewString("three"))));
  CHECK(Unwrap(array.Length()) == 3);

  auto parsed = Unwrap(context.ParseJson(R"({"answer": 42, "items": [1, 2, 3]})"));
  CHECK(Unwrap(Unwrap(parsed.GetProperty("answer")).ToInt32()) == 42);
  CHECK(Unwrap(Unwrap(parsed.GetProperty("items")).Length()) == 3);
  CHECK(Unwrap(parsed.ToJson()) == R"({"answer":42,"items":[1,2,3]})");

  auto invalid_json = context.ParseJson("{");
  CHECK(!invalid_json.Succeeded());
  CHECK(invalid_json.Error().Code() == quickjs::ErrorCode::Syntax);
}

void ValuesAndEquality(quickjs::Context& context) {
  {
    quickjs::Value original = Unwrap(context.NewString("copy"));
    quickjs::Value copy = original;
    CHECK(Unwrap(original.ToString()) == "copy");
    CHECK(Unwrap(copy.ToString()) == "copy");

    quickjs::Value moved = std::move(original);
    CHECK(Unwrap(moved.ToString()) == "copy");
    CHECK(original.IsUndefined());
  }
  {
    auto one = context.Number(1);
    auto other_one = context.Number(1);
    auto text = Unwrap(context.NewString("1"));
    CHECK(Unwrap(one.StrictEquals(other_one)));
    CHECK(!Unwrap(one.StrictEquals(text)));
  }
  {
    auto undefined_json = context.Undefined().ToJson();
    CHECK(!undefined_json.Succeeded());
    CHECK(undefined_json.Error().Code() == quickjs::ErrorCode::Type);
  }
  {
    auto function = Unwrap(context.Evaluate("(a, b) => a * b"));
    CHECK(function.IsFunction());
    CHECK(Unwrap(function.Call(std::vector<quickjs::Value>{context.Number(6), context.Number(7)})
                     .Value()
                     .ToInt32())
          == 42);
  }
}

void NativeCapturedValues() {
  quickjs::Runtime runtime;
  {
    quickjs::Context context(runtime);
    auto captured = context.Number(7);
    Require(context.SetGlobalFunction(
        "captured",
        [captured](quickjs::Context&, std::span<const quickjs::Value>) -> quickjs::Value {
          return captured;
        }
    ));
    CHECK(Unwrap(Unwrap(context.Evaluate("captured()")).ToInt32()) == 7);
  }
}

void Promises(quickjs::Context& context) {
  {
    auto result = context.Evaluate("Promise.resolve(5)");
    CHECK(result.Succeeded());
    CHECK(result.Value().IsPromise());
  }
  {
    quickjs::EvaluateOptions options;
    options.run_jobs = false;
    auto result = context.Evaluate(
        "Promise.resolve().then(() => { globalThis.queuedJob = 11; });",
        "<jobs>",
        options
    );
    CHECK(result.Succeeded());
    auto jobs = context.RunJobs();
    CHECK(jobs.Succeeded());
    CHECK(jobs.Value() >= 1);
    CHECK(Unwrap(Unwrap(context.Global("queuedJob")).ToInt32()) == 11);
  }
  {
    auto result = context.Evaluate(
        "globalThis.microtask = 0;"
        "Promise.resolve().then(() => { globalThis.microtask = 7; });"
    );
    CHECK(result.Succeeded());
    CHECK(Unwrap(Unwrap(context.Global("microtask")).ToInt32()) == 7);
  }
  {
    quickjs::EvaluateOptions options;
    options.await_promise = true;
    auto result = context.Evaluate("Promise.resolve(9)", "<await>", options);
    CHECK(result.Succeeded());
    CHECK(Unwrap(result.Value().ToNumber()) == 9.0);
  }
  {
    quickjs::EvaluateOptions options;
    options.await_promise = true;
    auto awaited = context.Evaluate("Promise.reject(new Error('rejected promise'))", "<await>", options);
    CHECK(!awaited.Succeeded());
    CHECK(awaited.Error().Message() == "rejected promise");
  }
}

void Modules(quickjs::Context& context) {
  context.SetModuleLoader([](std::string_view name) -> quickjs::Result<std::string> {
    if (name.find("dep.js") != std::string_view::npos) {
      return quickjs::Result<std::string>::Success("export const value = 41;");
    }
    return quickjs::Result<std::string>::Failure(quickjs::Error{
        quickjs::ErrorCode::Module,
        "Module not found: " + std::string(name),
        std::string(name),
    });
  });

  {
    auto result = context.EvaluateModule(
        "import { value } from './dep.js'; globalThis.moduleResult = value + 1;",
        "/main.js"
    );
    CHECK(result.Succeeded());
    CHECK(Unwrap(Unwrap(context.Global("moduleResult")).ToNumber()) == 42.0);
  }
  {
    auto result = context.EvaluateModule(
        "globalThis.metaUrl = import.meta.url;",
        "/meta.js"
    );
    CHECK(result.Succeeded());
    CHECK(Unwrap(Unwrap(context.Global("metaUrl")).ToString()) == "/meta.js");
  }
  {
    auto result = context.EvaluateModule("import './missing.js';", "/missing-main.js");
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::Module);
    CHECK(result.Error().Message().find("missing.js") != std::string::npos);
  }
}

void Limits() {
  {
    quickjs::RuntimeOptions options;
    options.should_interrupt = [] { return true; };
    quickjs::Runtime runtime(options);
    quickjs::Context context(runtime);
    auto result = context.Evaluate("1 + 1", "<interrupt>");
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::Interrupted);
  }
  {
    quickjs::Runtime runtime;
    quickjs::Context context(runtime);
    quickjs::EvaluateOptions options;
    options.time_limit = std::chrono::milliseconds(50);
    auto result = context.Evaluate("while (true) {}", "<loop>", options);
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::Interrupted);
  }
  {
    quickjs::RuntimeOptions options;
    options.memory_limit_bytes = 2 * 1024 * 1024;
    quickjs::Runtime runtime(options);
    quickjs::Context context(runtime);
    auto result = context.Evaluate(
        "const blocks = [];"
        "while (true) { blocks.push(new Array(8192).fill(1)); }",
        "<memory>"
    );
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::OutOfMemory);
  }
  {
    quickjs::Runtime runtime;
    quickjs::Context context(runtime);
    auto result = context.Evaluate(
        "function recurse() { return recurse(); } recurse();",
        "<stack>"
    );
    CHECK(!result.Succeeded());
    CHECK(result.Error().Code() == quickjs::ErrorCode::StackOverflow);
  }
}

void ContextMovement() {
  quickjs::Runtime runtime;
  quickjs::Context original(runtime);
  quickjs::Context moved(std::move(original));

  CHECK(moved.Evaluate("1 + 1").Succeeded());
  auto closed = original.Evaluate("1 + 1");
  CHECK(!closed.Succeeded());
  CHECK(closed.Error().Code() == quickjs::ErrorCode::Closed);
}

void ContextIsolation() {
  quickjs::Runtime first_runtime;
  quickjs::Context first(first_runtime);

  quickjs::Runtime second_runtime;
  quickjs::Context second(second_runtime);

  auto foreign = second.Number(1);
  auto failure = first.SetGlobal("foreign", foreign);
  CHECK(!failure.Succeeded());
  CHECK(failure.Error().Code() == quickjs::ErrorCode::Type);
}

} // namespace

int main() {
  quickjs::Runtime runtime;
  quickjs::Context context(runtime);

  EvaluateScripts(context);
  EvaluateErrors(context);
  GlobalsAndFunctions(context);
  ObjectsAndJson(context);
  ValuesAndEquality(context);
  NativeCapturedValues();
  Promises(context);
  Modules(context);
  Limits();
  ContextIsolation();
  ContextMovement();

  std::puts("Lib-QuickJS tests passed");
  return 0;
}
