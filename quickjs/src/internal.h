#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <huxerui/quickjs.h>
#include <quickjs.h>

namespace huxerui::quickjs::detail {

struct RuntimeState final {
  ~RuntimeState();

  JSRuntime* runtime = nullptr;
  RuntimeOptions options;
  bool deadline_active = false;
  std::chrono::steady_clock::time_point deadline{};
};

struct ContextState final {
  RuntimeState* runtime_state = nullptr;
  JSContext* context = nullptr;
  Context* owner = nullptr;
  ModuleLoader module_loader;
  std::optional<Error> loader_error;
  std::vector<std::shared_ptr<NativeFunction>> native_functions;
};

struct ValueAccess final {
  [[nodiscard]] static JSContext* ContextOf(const Value& value) noexcept;
  [[nodiscard]] static JSValue Raw(const Value& value) noexcept;
  [[nodiscard]] static Value Make(JSContext* context, JSValue value) noexcept;
  [[nodiscard]] static JSValue Release(Value& value) noexcept;
};

/// Creates a string value. The caller owns the returned handle.
[[nodiscard]] JSValue NewStringRaw(JSContext* context, std::string_view value);

/// Converts a value to UTF-8 without changing the pending exception state.
[[nodiscard]] std::optional<std::string> ToUtf8(JSContext* context, JSValueConst value);

/// Converts one JavaScript exception or rejection value into a portable Error.
[[nodiscard]] Error ErrorFromValue(JSContext* context, JSValueConst value, std::string filename = {});

/// Reads and clears the pending exception.
[[nodiscard]] Error CaptureException(JSContext* context, std::string filename = {});

/// Reads a context failure, preferring a module loader error recorded by the
/// loader trampoline.
[[nodiscard]] Error TakeContextError(ContextState& state, JSContext* context, std::string filename = {});

[[nodiscard]] Error ClosedError(std::string message = "The JavaScript context is closed");
[[nodiscard]] Error TypeError(std::string message);

/// Validates that a value may be passed to a JavaScript API on `context`.
/// A context-free default Value is accepted as `undefined`.
[[nodiscard]] std::optional<Error> ValidateValue(JSContext* context, const Value& value);

/// Creates a successful Result without requiring the explicit core Result constructor.
template <class T> [[nodiscard]] Result<T> Succeed(T value) {
  return Result<T>::Success(std::move(value));
}

[[nodiscard]] JSValue DuplicateRaw(JSContext* context, const Value& value);

/// Publishes import.meta.url for a compiled module.
void SetImportMeta(JSContext* context, JSModuleDef* module, std::string_view url);

/// Runs promise jobs until a promise settles.
[[nodiscard]] Result<Value> AwaitPromise(
    ContextState& state,
    Value promise,
    std::string filename = {}
);

class DeadlineGuard final {
public:
  DeadlineGuard(RuntimeState& state, std::chrono::milliseconds limit);
  DeadlineGuard(const DeadlineGuard&) = delete;
  DeadlineGuard& operator=(const DeadlineGuard&) = delete;
  ~DeadlineGuard();

private:
  RuntimeState& state_;
  bool previous_active_ = false;
  std::chrono::steady_clock::time_point previous_deadline_{};
};

} // namespace huxerui::quickjs::detail
