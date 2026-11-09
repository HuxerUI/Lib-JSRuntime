# Embedded QuickJS

This is the Lib-QuickJS engine wrapper embedded inside `Lib-JSRuntime`. It is compiled directly into `HuxerUI::TSX`; `HuxerUI::QuickJS` remains as a compatibility alias. No sibling `Lib-QuickJS` checkout is required. Lib-QuickJS is the JavaScript engine library for [HuxerUI](https://github.com/HuxerUI/HuxerUI). It packages the
[quickjs-ng](https://github.com/quickjs-ng/quickjs) engine behind owning C++ runtime, context, and value types so
applications can evaluate scripts, expose host functions, load ES modules, and convert JSON.

Link **`HuxerUI::TSX`** (or its compatibility alias **`HuxerUI::QuickJS`**) and include **`<huxerui/quickjs.h>`**. The API lives in **`huxerui::quickjs`**. The
engine is headless: it needs no runtime, window, or application hook. Start with one `Runtime` and one `Context`;
create values and register host functions from there.

The library vendors quickjs-ng `v0.18.0` because it builds with the MSVC, Clang, GCC, Android NDK, Apple, and
Emscripten toolchains used by HuxerUI. Only the engine core is vendored; the `qjs` CLI, `quickjs-libc`, and
standard host modules are not part of this library.

- [Capabilities](#capabilities)
- [Platform support](#platform-support)
- [Integration](#integration)
- [Runtime and context](#runtime-and-context)
- [Values](#values)
- [Host functions](#host-functions)
- [Modules](#modules)
- [Promises and jobs](#promises-and-jobs)
- [Limits and interruption](#limits-and-interruption)
- [Threading and lifetimes](#threading-and-lifetimes)
- [Build and validation](#build-and-validation)
- [Repository layout](#repository-layout)

## Capabilities

| Capability | Public entry points |
| --- | --- |
| Evaluate classic scripts | `Context::Evaluate()` in [`quickjs.h`](include/huxerui/quickjs.h) |
| Evaluate ES modules with host-provided source | `Context::EvaluateModule()`, `Context::SetModuleLoader()` |
| Read and write globals | `Context::Global()`, `Context::SetGlobal()`, `Context::GlobalObject()` |
| Expose C++ callables | `Context::SetGlobalFunction()`, `NativeFunction` |
| Own JavaScript handles | `Value`, including copy, move, property, element, call, and conversion operations |
| Create objects, arrays, and strings | `Context::NewObject()`, `Context::NewArray()`, `Context::NewString()` |
| Convert JSON | `Context::ParseJson()`, `Value::ToJson()` |
| Run promise continuations | `Context::RunJobs()`, `EvaluateOptions::await_promise` |
| Bound execution | `RuntimeOptions::should_interrupt`, `EvaluateOptions::time_limit`, memory and stack limits |
| Report failures without engine handles | `Error`, `ErrorCode`, `Result<T>` |

## Platform support

The engine sources are platform-independent C11 and add no platform adapter. The CMake target links the platform
math, dynamic-loader, and thread libraries where they exist.

| Platform | Status |
| --- | --- |
| Linux | Built and tested in this repository |
| Android | Expected to build with the NDK; not exercised here |
| iOS | Expected to build with the Apple toolchain; not exercised here |
| macOS | Expected to build with the Apple toolchain; not exercised here |
| Windows | Expected to build with MSVC or MinGW; not exercised here |
| Web | Expected with Emscripten; the core does not request pthreads |

## Integration

The parent `Lib-JSRuntime` project compiles `quickjs/` into `HuxerUI::TSX`. A consumer does not need a separate Lib-QuickJS checkout:

```cmake
huxerui_add_app(my_app
        SOURCES
            src/app.cpp
)

huxerui_use_library(my_app
        TARGET HuxerUI::TSX
        PATH "${CMAKE_CURRENT_SOURCE_DIR}/path/to/Lib-JSRuntime"
)
```

`HuxerUI::QuickJS` remains an alias of the merged target for compatibility.

Include the public header explicitly; it is not included by `<huxerui/huxerui.h>`:

```cpp
#include <huxerui/quickjs.h>
```

A minimal engine:

```cpp
#include <huxerui/quickjs.h>

#include <cstdio>

int main() {
  huxerui::quickjs::Runtime runtime;
  huxerui::quickjs::Context context(runtime);

  auto result = context.Evaluate("6 * 7");
  if (!result.Succeeded()) {
    std::fprintf(stderr, "%s\n", result.Error().Message().c_str());
    return 1;
  }

  auto number = result.Value().ToNumber();
  if (number.Succeeded()) {
    std::printf("%.0f\n", number.Value());
  }
}
```

`Evaluate()` parses and executes with the standard JavaScript intrinsics already installed by QuickJS. The
returned value is the script completion value. A thrown exception is reported as `Result<Value>` failure, never
as an empty value.

## Runtime and context

`Runtime` owns the engine heap and shared limits. `Context` owns one global object and the intrinsic constructors.
A runtime may host several contexts; contexts must be destroyed before their runtime.

```cpp
huxerui::quickjs::Runtime runtime;
huxerui::quickjs::Context context(runtime);

huxerui::quickjs::RuntimeOptions options;
options.memory_limit_bytes = 32 * 1024 * 1024;
options.stack_limit_bytes = 512 * 1024;
options.should_interrupt = [] { return false; };
```

Zero limit fields leave the QuickJS default in place. `should_interrupt` is a cooperative probe called while
JavaScript executes; return true to abort. `Runtime`, `Context`, and `Value` are movable but not copyable, except
`Value`, whose copy duplicates the engine reference.

## Values

`Value` owns one JavaScript handle. Every read, write, call, and conversion returns `Result<T>` when the engine
can throw.

```cpp
using namespace huxerui::quickjs;

auto object = context.NewObject();
if (!object.Succeeded()) {
  return;
}

object.Value().SetProperty("name", context.Undefined());

auto name = context.NewString("Ada");
if (name.Succeeded()) {
  object.Value().SetProperty("name", name.Value());
}

auto json = object.Value().ToJson("  ");
if (json.Succeeded()) {
  std::puts(json.Value().c_str());
}
```

- `IsUndefined()`, `IsNull()`, `IsBoolean()`, `IsNumber()`, `IsString()`, `IsBigInt()`, `IsSymbol()`,
  `IsObject()`, `IsFunction()`, `IsArray()`, `IsPromise()`, and `IsError()` inspect the engine tag.
- `ToBoolean()`, `ToNumber()`, `ToInt32()`, and `ToString()` apply JavaScript coercion. A default-constructed,
  context-free `Value` behaves as `undefined`: false, NaN, zero, and `"undefined"`.
- `GetProperty()` and `GetElement()` return the value or `undefined` when absent. `SetProperty()` and
  `SetElement()` accept values from the same context and throw only through the returned `Result`.
- `Length()` works for arrays and strings. `PropertyNames()` lists own string keys.
- `Call()` invokes a function with `undefined` or an explicit receiver. Arguments and the receiver must belong to
  the same context.
- `StrictEquals()` implements JavaScript `===`.

A `Value` from another context is rejected with `ErrorCode::Type`; it is never silently converted.

## Host functions

`SetGlobalFunction()` installs a C++ callable on the global object. The callback runs synchronously on the
evaluating thread. The argument span is valid only during the call.

```cpp
context.SetGlobalFunction(
    "readConfig",
    [](Context& owner, std::span<const Value> arguments) -> Value {
      if (arguments.empty()) {
        throw std::invalid_argument("readConfig expects a path");
      }
      const std::string path = arguments[0].ToString().Value();
      static_cast<void>(path);

      auto parsed = owner.ParseJson(R"({"theme":"dark"})");
      if (!parsed.Succeeded()) {
        throw std::runtime_error(parsed.Error().Message());
      }
      return parsed.Value();
    },
    1
);
```

A C++ exception becomes a catchable JavaScript `Error`; it can be caught by script code. Returning a `Value` from
another context raises a JavaScript `TypeError`. The optional arity argument is the reported function `length`.
Registered host functions are retained by their context until it is destroyed.

## Modules

`EvaluateModule()` compiles a module, sets `import.meta.url` to its filename, evaluates it, and waits for module
evaluation to settle. Static imports are resolved by the loader installed with `SetModuleLoader()`.

```cpp
context.SetModuleLoader([](std::string_view name) -> Result<std::string> {
  if (name == "/app/dep.js") {
    return Result<std::string>::Success("export const answer = 42;");
  }
  return Result<std::string>::Failure(Error{
      ErrorCode::Module,
      "Unknown module: " + std::string(name),
  });
});

auto result = context.EvaluateModule(
    "import { answer } from './dep.js'; globalThis.answer = answer;",
    "/app/main.js"
);
```

QuickJS normalizes relative specifiers against the importing module before calling the loader. The loader owns
source acquisition: read a file, consult an embedded bundle, or synthesize text. The returned text must be a
complete ES module. A failure from the loader is reported with its original `ErrorCode` and message.

A module's completion value is not an expression value as in a classic script; communicate results through
exports, globals, or host callbacks installed by the module.

This adapter supports JavaScript modules loaded by the regular module loader. Dynamic `import()` and JSON/attribute
module types are not implemented; provide those through a future host policy rather than relying on the engine
defaults.

## Promises and jobs

QuickJS uses a microtask queue for promise continuations. `Evaluate()` drains it by default after the script
returns, so `Promise.resolve().then(...)` has run before the call returns. Set `EvaluateOptions::run_jobs = false`
to inspect the queue manually, or call `RunJobs()` later from the same thread.

```cpp
EvaluateOptions options;
options.await_promise = true;

auto result = context.Evaluate("Promise.resolve(42)", "<eval>", options);
if (result.Succeeded()) {
  const double value = result.Value().ToNumber().Value();
}
```

`await_promise` waits for a returned promise and reports a rejection as an error. A pending promise with no
remaining jobs returns `ErrorCode::Unsupported` instead of blocking forever. Module evaluation always waits for
its evaluation promise. Rejected promises that no script handles are not reported automatically; attach a handler
or evaluate with `await_promise` to observe them.

There are no timers, network requests, or `console` methods in this library. Promise settlement therefore comes
from host callbacks or script-only work. Host-driven completion should call `RunJobs()` after the host state
becomes ready.

## Limits and interruption

Two independent mechanisms bound execution:

- `RuntimeOptions::should_interrupt` is called while bytecode executes. Return true to abort with
  `ErrorCode::Interrupted`.
- `EvaluateOptions::time_limit` installs a wall-clock deadline for the complete evaluation, including the
  microtask queue. Zero disables it.

```cpp
EvaluateOptions options;
options.time_limit = std::chrono::milliseconds(100);

auto result = context.Evaluate("while (true) {}", "<guard>", options);
// result.Error().Code() == ErrorCode::Interrupted
```

`memory_limit_bytes`, `stack_limit_bytes`, and `gc_threshold_bytes` configure the runtime. Exhausting the memory
limit reports `ErrorCode::OutOfMemory`; exceeding the call stack reports `ErrorCode::StackOverflow`.

## Threading and lifetimes

A `Runtime`, its contexts, and all values are single-threaded. Use them from one thread at a time. To run a script
off the UI thread, create the runtime and context on that worker and keep them there; do not share values across
threads. HuxerUI's `WorkerSequence` or a dedicated worker is the usual owner.

Destroy values before their context and contexts before their runtime. Value copies share the same engine
reference and must satisfy the same lifetime rule. A moved-from `Value` is `undefined`; operations on a moved-from
`Context` report `ErrorCode::Closed`.

## Build and validation

From the `Lib-JSRuntime` root, with the HuxerUI source tree or an installed SDK:

```sh
cmake -G Ninja -S . -B build \
  -DHUXERUI_HOME=/path/to/HuxerUI \
  -DHUXERUI_TSX_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The test target covers script evaluation, coercion, globals, native callbacks and their exception bridge,
objects/arrays, JSON, value copying and moving, strict equality, promises and the microtask queue, module loading,
import.meta, missing modules, interrupt callbacks, time limits, and cross-context rejection.

The current Linux build and tests are the validated baseline. Other platforms use the same vendored C sources but
require their own toolchain run before being claimed as validated.

## Repository layout

| Path | Contents |
| --- | --- |
| [`include/huxerui/quickjs.h`](include/huxerui/quickjs.h) | Public Runtime, Context, Value, Error, and Result contracts |
| [`src/runtime.cpp`](src/runtime.cpp) | Runtime limits, interrupt trampoline, and lifetime |
| [`src/context.cpp`](src/context.cpp) | Evaluation, jobs, globals, native functions, module loader, and promises |
| [`src/value.cpp`](src/value.cpp) | Value ownership, coercion, property operations, and calls |
| [`src/error.cpp`](src/error.cpp) | Exception capture, source locations, and conversion helpers |
| [`third_party/quickjs/`](third_party/quickjs) | Pinned quickjs-ng `v0.18.0` engine sources and license |
| [`tests/`](tests) | Engine contract tests |

## Relationship to HuxerUI

HuxerUI owns windows, composition, state, tasks, and file access. Lib-QuickJS owns the JavaScript heap and value
lifetimes. File-backed module loading, UI bindings, console output, and timers belong to the consuming application
or a future host-integration layer; this library does not install process-wide services or start threads.
