#pragma once

/// @file
/// Embeddable JavaScript engine for HuxerUI applications.
///
/// Lib-QuickJS wraps the QuickJS engine with owning runtime, context, and value
/// types. Scripts can be evaluated, host functions exposed, ES modules loaded,
/// and JSON converted without exposing QuickJS handles in application code.
///
/// Engines are single-threaded: one Runtime and all its Contexts and Values
/// must be used from one thread at a time. Run a script on a worker when it may
/// block or exceed a frame budget. Runtime, Context, and Value are not
/// synchronized. All Value objects must be destroyed before their Context.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <huxerui/data.h>

namespace huxerui::quickjs {

class Context;
class Runtime;
class Value;

namespace detail {
struct ContextState;
struct RuntimeState;
struct ValueAccess;
} // namespace detail

/// Identifies the category of a JavaScript or host-integration failure.
enum class ErrorCode {
  Exception,     ///< A JavaScript exception such as a thrown value or ReferenceError.
  Syntax,        ///< Source text could not be parsed.
  OutOfMemory,   ///< The runtime memory limit or host allocation failed.
  StackOverflow, ///< The runtime call stack or configured stack limit was exceeded.
  Interrupted,   ///< A runtime interrupt callback or evaluation time limit stopped execution.
  Module,        ///< A module loader failed or returned invalid source.
  Type,          ///< A host value could not be converted or belongs to another context.
  Closed,        ///< The context or runtime has been moved from or retired.
  Unsupported,   ///< The requested operation is not supported by this adapter.
};

/// Describes a failed JavaScript operation without exposing an engine handle.
///
/// Errors are values and remain valid after the originating Context is
/// destroyed. The line and column are zero when the engine did not report a
/// source location.
class Error final {
public:
  /// Creates an error value.
  /// @param code Portable error category.
  /// @param message Human-readable diagnostic text.
  /// @param filename Source name associated with the failure, when known.
  /// @param line One-based source line, or zero when unknown.
  /// @param column One-based source column, or zero when unknown.
  /// @param stack Engine stack trace, when available.
  Error(
      ErrorCode code,
      std::string message,
      std::string filename = {},
      int line = 0,
      int column = 0,
      std::string stack = {}
  );

  /// @return The portable error category.
  [[nodiscard]] ErrorCode Code() const noexcept;

  /// @return Human-readable diagnostic text owned by this error.
  [[nodiscard]] const std::string& Message() const noexcept;

  /// @return The source name, or an empty string when unavailable.
  [[nodiscard]] const std::string& Filename() const noexcept;

  /// @return The one-based source line, or zero when unknown.
  [[nodiscard]] int Line() const noexcept;

  /// @return The one-based source column, or zero when unknown.
  [[nodiscard]] int Column() const noexcept;

  /// @return The engine stack trace, or an empty string when unavailable.
  [[nodiscard]] const std::string& Stack() const noexcept;

private:
  ErrorCode code_;
  std::string message_;
  std::string filename_;
  int line_ = 0;
  int column_ = 0;
  std::string stack_;
};

/// Holds either a successful value or an Error.
/// @tparam T Successful value type.
template <class T> using Result = huxerui::Result<T, Error>;

/// Configures one JavaScript runtime.
///
/// Zero leaves the corresponding QuickJS default in place. A runtime owns the
/// engine state shared by its contexts; contexts are created from it.
struct RuntimeOptions {
  /// Hard heap limit in bytes. Zero disables the engine limit.
  std::size_t memory_limit_bytes = 0;

  /// Maximum native stack size in bytes. Zero disables the engine limit.
  std::size_t stack_limit_bytes = 0;

  /// Garbage-collection threshold in bytes. Zero keeps the engine default.
  std::size_t gc_threshold_bytes = 0;

  /// Cooperative interrupt probe invoked while JavaScript executes.
  ///
  /// Return true to abort execution with ErrorCode::Interrupted. The callback
  /// runs on the thread that is evaluating the script and must not throw.
  std::function<bool()> should_interrupt;
};

/// Configures one evaluation.
struct EvaluateOptions {
  /// Execute the microtask queue after the source returns. Enabled by default.
  bool run_jobs = true;

  /// Resolve a returned Promise before reporting success. Module evaluation
  /// always waits regardless of this value.
  bool await_promise = false;

  /// Maximum microtasks to run. A negative value runs until the queue drains.
  int max_jobs = -1;

  /// Wall-clock limit for the complete evaluation. Zero disables the limit.
  std::chrono::milliseconds time_limit{0};
};

/// Owns a QuickJS runtime shared by one or more contexts.
///
/// Runtime is movable but not copyable. Do not move a runtime while contexts
/// created from it are alive. All contexts and values must be destroyed before
/// the runtime. The runtime is not thread-safe.
class Runtime final {
public:
  /// Creates a runtime.
  /// @param options Runtime limits and interrupt policy.
  /// @throws std::runtime_error If the engine cannot allocate a runtime.
  explicit Runtime(RuntimeOptions options = {});

  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&& other) noexcept;
  Runtime& operator=(Runtime&& other) noexcept;

private:
  friend class Context;
  std::unique_ptr<detail::RuntimeState> state_;
};

/// An owning JavaScript value bound to one Context.
///
/// Copying duplicates the underlying engine reference; moving transfers it.
/// Values are cheap handles, not deep copies. All copies and the original must
/// be destroyed before their Context, and a Value cannot be used with a
/// different Context. A default-constructed Value is `undefined` and has no
/// context; it converts to `"undefined"`, `false`, and NaN where JavaScript
/// coerces `undefined`.
class Value final {
public:
  /// Creates an `undefined` value with no context.
  Value() noexcept;

  Value(const Value& other);
  Value(Value&& other) noexcept;
  Value& operator=(const Value& other);
  Value& operator=(Value&& other) noexcept;
  ~Value();

  /// @return True for a JavaScript `undefined` value.
  [[nodiscard]] bool IsUndefined() const noexcept;

  /// @return True for a JavaScript `null` value.
  [[nodiscard]] bool IsNull() const noexcept;

  /// @return True for a JavaScript boolean.
  [[nodiscard]] bool IsBoolean() const noexcept;

  /// @return True for a JavaScript number, including NaN and infinities.
  [[nodiscard]] bool IsNumber() const noexcept;

  /// @return True for a JavaScript string.
  [[nodiscard]] bool IsString() const noexcept;

  /// @return True for a JavaScript BigInt.
  [[nodiscard]] bool IsBigInt() const noexcept;

  /// @return True for a JavaScript symbol.
  [[nodiscard]] bool IsSymbol() const noexcept;

  /// @return True for a JavaScript object or function.
  [[nodiscard]] bool IsObject() const noexcept;

  /// @return True for a callable JavaScript function.
  [[nodiscard]] bool IsFunction() const noexcept;

  /// @return True for a JavaScript Array.
  [[nodiscard]] bool IsArray() const noexcept;

  /// @return True for a JavaScript Promise.
  [[nodiscard]] bool IsPromise() const noexcept;

  /// @return True for a JavaScript Error object.
  [[nodiscard]] bool IsError() const noexcept;

  /// Applies JavaScript ToBoolean coercion.
  /// @return The coerced boolean.
  [[nodiscard]] Result<bool> ToBoolean() const;

  /// Applies JavaScript ToNumber coercion.
  /// @return The coerced number; `undefined` yields NaN.
  [[nodiscard]] Result<double> ToNumber() const;

  /// Applies JavaScript ToInt32 coercion.
  /// @return The coerced 32-bit integer.
  [[nodiscard]] Result<std::int32_t> ToInt32() const;

  /// Applies JavaScript ToString coercion.
  /// @return UTF-8 text. A context-free `undefined` returns `"undefined"`.
  [[nodiscard]] Result<std::string> ToString() const;

  /// Serializes this value with JSON.stringify.
  /// @param indent Optional indentation passed to JSON.stringify.
  /// @return Compact or indented JSON text.
  /// @return ErrorCode::Type when JSON.stringify yields no string, for example
  ///         for `undefined`, functions, or symbols.
  [[nodiscard]] Result<std::string> ToJson(std::string_view indent = {}) const;

  /// Reads a named property.
  /// @param name UTF-8 property name.
  /// @return The property value, or `undefined` when absent.
  [[nodiscard]] Result<Value> GetProperty(std::string_view name) const;

  /// Reads an indexed property.
  /// @param index Nonnegative array index.
  /// @return The element value, or `undefined` when absent.
  [[nodiscard]] Result<Value> GetElement(std::uint32_t index) const;

  /// Writes a named property.
  /// @param name UTF-8 property name.
  /// @param value Value from the same context.
  /// @return Success, or a conversion or JavaScript error.
  [[nodiscard]] Result<void> SetProperty(std::string_view name, Value value) const;

  /// Writes an indexed property.
  /// @param index Nonnegative array index.
  /// @param value Value from the same context.
  /// @return Success, or a conversion or JavaScript error.
  [[nodiscard]] Result<void> SetElement(std::uint32_t index, Value value) const;

  /// Tests whether a named property is present, including inherited ones.
  /// @param name UTF-8 property name.
  /// @return Whether the property exists.
  [[nodiscard]] Result<bool> HasProperty(std::string_view name) const;

  /// Returns the array or string length.
  /// @return The length reported by the engine.
  [[nodiscard]] Result<std::int64_t> Length() const;

  /// Lists own string property names.
  /// @param enumerable_only Restrict the result to enumerable properties.
  /// @return Property names in engine order.
  [[nodiscard]] Result<std::vector<std::string>> PropertyNames(bool enumerable_only = true) const;

  /// Calls this value as a function with `undefined` as `this`.
  /// @return The call result, or ErrorCode::Type when this value is not callable.
  [[nodiscard]] Result<Value> Call() const;

  /// Calls this value as a function with `undefined` as `this`.
  /// @param arguments Values from the same context.
  /// @return The call result.
  [[nodiscard]] Result<Value> Call(std::span<const Value> arguments) const;

  /// Calls this value as a function with an explicit `this` value.
  /// @param this_value Receiver, or a default Value for `undefined`.
  /// @param arguments Values from the same context.
  /// @return The call result.
  [[nodiscard]] Result<Value> Call(Value this_value, std::span<const Value> arguments) const;

  /// Compares two values with JavaScript strict equality.
  /// @param other Value from the same context.
  /// @return Whether both values are strictly equal.
  [[nodiscard]] Result<bool> StrictEquals(const Value& other) const;

private:
  friend class Context;
  friend struct detail::ValueAccess;

  struct Opaque final {
    std::uint64_t words[2]{};
  };

  Value(void* context, Opaque opaque) noexcept;

  void* context_ = nullptr;
  Opaque opaque_{};
};

/// Host function callable from JavaScript.
///
/// The span is valid only for the duration of the call. Do not retain the
/// Context reference or the argument values by reference beyond the call.
/// Returning a Value from another context raises a JavaScript TypeError.
/// Throwing a C++ exception raises a catchable JavaScript Error with the
/// exception message.
using NativeFunction = std::function<Value(Context&, std::span<const Value>)>;

/// Resolves a module name to JavaScript source.
///
/// QuickJS normalizes relative import specifiers before calling the loader.
/// The returned text must be a complete ES module. Return ErrorCode::Module or
/// ErrorCode::Unsupported when a specifier cannot be served.
using ModuleLoader = std::function<Result<std::string>(std::string_view module_name)>;

/// Owns one JavaScript context and its global object.
///
/// Context is movable but not copyable. It must be destroyed before its
/// Runtime. Evaluation, host functions, and module loading are synchronous and
/// run on the calling thread. Use RunJobs() to drain promise continuations
/// created by asynchronous host work.
class Context final {
public:
  /// Creates a context with the standard JavaScript intrinsics.
  /// @param runtime Runtime that owns this context.
  /// @throws std::logic_error If the runtime has been moved from.
  /// @throws std::runtime_error If the engine cannot allocate a context.
  explicit Context(Runtime& runtime);

  ~Context();

  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  Context(Context&& other) noexcept;
  Context& operator=(Context&& other) noexcept;

  /// Evaluates source as a classic script.
  /// @param source UTF-8 JavaScript text.
  /// @param filename Source name used in diagnostics. Defaults to `"<eval>"`.
  /// @param options Evaluation policy.
  /// @return The script completion value.
  [[nodiscard]] Result<Value> Evaluate(
      std::string_view source,
      std::string_view filename = "<eval>",
      EvaluateOptions options = {}
  );

  /// Evaluates source as an ES module and waits for its evaluation to settle.
  ///
  /// Static imports are resolved through the configured ModuleLoader. The
  /// module's `import.meta.url` is set to `filename`.
  /// @param source UTF-8 module text.
  /// @param filename Nonempty module identity.
  /// @param options Evaluation policy; module evaluation always waits.
  /// @return The module evaluation value.
  [[nodiscard]] Result<Value> EvaluateModule(
      std::string_view source,
      std::string_view filename,
      EvaluateOptions options = {}
  );

  /// Executes pending promise jobs.
  /// @param max_jobs Maximum number of jobs to run; negative drains the queue.
  /// @return Number of executed jobs, or the first uncaught job exception.
  [[nodiscard]] Result<int> RunJobs(int max_jobs = -1);

  /// Installs or replaces the module loader used by future imports.
  /// @param loader Loader callable; an empty function disables module loading.
  void SetModuleLoader(ModuleLoader loader);

  /// @return The global object.
  [[nodiscard]] Result<Value> GlobalObject() const;

  /// Reads a global binding.
  /// @param name UTF-8 global name.
  /// @return The global value, or `undefined` when absent.
  [[nodiscard]] Result<Value> Global(std::string_view name) const;

  /// Writes a global binding.
  /// @param name UTF-8 global name.
  /// @param value Value from this context.
  /// @return Success or a conversion error.
  [[nodiscard]] Result<void> SetGlobal(std::string_view name, Value value);

  /// Writes a boolean global binding.
  [[nodiscard]] Result<void> SetGlobal(std::string_view name, bool value);

  /// Writes a numeric global binding.
  [[nodiscard]] Result<void> SetGlobal(std::string_view name, double value);

  /// Writes a 32-bit integer global binding.
  [[nodiscard]] Result<void> SetGlobal(std::string_view name, std::int32_t value);

  /// Writes a string global binding.
  [[nodiscard]] Result<void> SetGlobal(std::string_view name, std::string_view value);

  /// Writes a string literal global binding without selecting the bool overload.
  [[nodiscard]] Result<void> SetGlobal(std::string_view name, const char* value);

  /// Creates a host function and installs it as a global binding.
  /// @param name UTF-8 global name.
  /// @param function Callable invoked on this thread.
  /// @param arity Reported JavaScript function length.
  [[nodiscard]] Result<void> SetGlobalFunction(
      std::string_view name,
      NativeFunction function,
      std::size_t arity = 0
  );

  /// @return A JavaScript `undefined` value.
  [[nodiscard]] Value Undefined() const noexcept;

  /// @return A JavaScript `null` value.
  /// @throws std::logic_error If this context has been moved from.
  [[nodiscard]] Value Null() const;

  /// @return A JavaScript boolean value.
  /// @throws std::logic_error If this context has been moved from.
  [[nodiscard]] Value Boolean(bool value) const;

  /// @return A JavaScript number value.
  /// @throws std::logic_error If this context has been moved from.
  [[nodiscard]] Value Number(double value) const;

  /// Creates a JavaScript string.
  /// @param value UTF-8 text; embedded nulls are preserved.
  [[nodiscard]] Result<Value> NewString(std::string_view value) const;

  /// Creates an empty JavaScript object.
  [[nodiscard]] Result<Value> NewObject() const;

  /// Creates a JavaScript array from existing values.
  /// @param values Values from this context.
  [[nodiscard]] Result<Value> NewArray(std::span<const Value> values = {}) const;

  /// Parses JSON text.
  /// @param text UTF-8 JSON text.
  /// @param filename Source name used in diagnostics.
  [[nodiscard]] Result<Value> ParseJson(
      std::string_view text,
      std::string_view filename = "<json>"
  ) const;

private:
  friend struct detail::ValueAccess;

  std::unique_ptr<detail::ContextState> state_;
};

} // namespace huxerui::quickjs
