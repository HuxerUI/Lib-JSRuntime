#include "internal.h"

#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace huxerui::quickjs {

namespace detail {

DeadlineGuard::DeadlineGuard(RuntimeState& state, std::chrono::milliseconds limit)
    : state_(state),
      previous_active_(state.deadline_active),
      previous_deadline_(state.deadline) {
  if (limit.count() > 0) {
    state_.deadline_active = true;
    state_.deadline = std::chrono::steady_clock::now() + limit;
  } else {
    state_.deadline_active = false;
  }
}

DeadlineGuard::~DeadlineGuard() {
  state_.deadline_active = previous_active_;
  state_.deadline = previous_deadline_;
}

void SetImportMeta(JSContext* context, JSModuleDef* module, std::string_view url) {
  JSValue meta = JS_GetImportMeta(context, module);
  if (JS_IsException(meta)) {
    JS_FreeValue(context, JS_GetException(context));
    return;
  }

  JSValue url_value = NewStringRaw(context, url);
  if (!JS_IsException(url_value)) {
    JS_SetPropertyStr(context, meta, "url", url_value);
  }
  JS_FreeValue(context, meta);

  if (JS_HasException(context)) {
    JS_FreeValue(context, JS_GetException(context));
  }
}

namespace {

JSModuleDef* ModuleLoaderTrampoline(JSContext* context, const char* module_name, void* opaque) {
  static_cast<void>(opaque);
  auto* state = static_cast<ContextState*>(JS_GetContextOpaque(context));
  if (!state || !state->module_loader) {
    JS_ThrowReferenceError(context, "No module loader is configured for '%s'", module_name);
    return nullptr;
  }

  try {
    auto loaded = state->module_loader(module_name);
    if (!loaded.Succeeded()) {
      state->loader_error = loaded.Error();
      JS_ThrowReferenceError(context, "%s", loaded.Error().Message().c_str());
      return nullptr;
    }

    const std::string& source = loaded.Value();
    JSValue compiled = JS_Eval(
        context,
        source.c_str(),
        source.size(),
        module_name,
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
    );
    if (JS_IsException(compiled)) {
      return nullptr;
    }

    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    SetImportMeta(context, module, module_name);
    JS_FreeValue(context, compiled);
    return module;
  } catch (const std::exception& error) {
    state->loader_error = Error{ErrorCode::Module, error.what(), module_name};
    JS_ThrowReferenceError(context, "%s", error.what());
    return nullptr;
  } catch (...) {
    state->loader_error = Error{ErrorCode::Module, "The module loader failed", module_name};
    JS_ThrowReferenceError(context, "The module loader failed for '%s'", module_name);
    return nullptr;
  }
}

JSValue NativeFunctionTrampoline(
    JSContext* context,
    JSValueConst this_value,
    int argc,
    JSValueConst* argv,
    int magic
) {
  static_cast<void>(this_value);
  auto* state = static_cast<ContextState*>(JS_GetContextOpaque(context));
  if (!state || !state->owner || magic < 0
      || static_cast<std::size_t>(magic) >= state->native_functions.size()) {
    return JS_ThrowInternalError(context, "Invalid native JavaScript function");
  }

  const std::shared_ptr<NativeFunction> function = state->native_functions[static_cast<std::size_t>(magic)];
  if (!function) {
    return JS_ThrowInternalError(context, "Invalid native JavaScript function");
  }

  std::vector<Value> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    arguments.push_back(ValueAccess::Make(context, JS_DupValue(context, argv[index])));
  }

  try {
    Value result = (*function)(*state->owner, std::span<const Value>(arguments));
    if (ValueAccess::ContextOf(result) && ValueAccess::ContextOf(result) != context) {
      return JS_ThrowTypeError(
          context,
          "A native JavaScript function returned a value from another context"
      );
    }
    return ValueAccess::Release(result);
  } catch (const std::exception& error) {
    return JS_ThrowPlainError(context, "%s", error.what());
  } catch (...) {
    return JS_ThrowPlainError(context, "A native JavaScript function failed");
  }
}

Result<void> SetGlobalRaw(ContextState& state, std::string_view name, JSValue value) {
  JSContext* context = state.context;
  JSValue global = JS_GetGlobalObject(context);
  if (JS_IsException(global)) {
    JS_FreeValue(context, value);
    return Result<void>::Failure(CaptureException(context));
  }

  const char* text = name.empty() ? "" : name.data();
  const JSAtom atom = JS_NewAtomLen(context, text, name.size());
  if (atom == JS_ATOM_NULL) {
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return Result<void>::Failure(CaptureException(context));
  }

  const int result = JS_SetProperty(context, global, atom, value);
  JS_FreeAtom(context, atom);
  JS_FreeValue(context, global);
  if (result < 0) {
    return Result<void>::Failure(CaptureException(context));
  }
  return Result<void>::Success();
}

Result<Value> GetGlobalRaw(ContextState& state, std::string_view name) {
  JSContext* context = state.context;
  JSValue global = JS_GetGlobalObject(context);
  if (JS_IsException(global)) {
    return Result<Value>::Failure(CaptureException(context));
  }

  const char* text = name.empty() ? "" : name.data();
  const JSAtom atom = JS_NewAtomLen(context, text, name.size());
  if (atom == JS_ATOM_NULL) {
    JS_FreeValue(context, global);
    return Result<Value>::Failure(CaptureException(context));
  }

  JSValue result = JS_GetProperty(context, global, atom);
  JS_FreeAtom(context, atom);
  JS_FreeValue(context, global);
  if (JS_IsException(result)) {
    return Result<Value>::Failure(CaptureException(context));
  }
  return Succeed(ValueAccess::Make(context, result));
}

} // namespace

Result<Value> AwaitPromise(ContextState& state, Value promise, std::string filename) {
  JSContext* context = state.context;
  const JSValue raw_promise = ValueAccess::Raw(promise);

  for (;;) {
    const JSPromiseStateEnum promise_state = JS_PromiseState(context, raw_promise);
    if (promise_state == JS_PROMISE_FULFILLED) {
      return Succeed(ValueAccess::Make(context, JS_PromiseResult(context, raw_promise)));
    }
    if (promise_state == JS_PROMISE_REJECTED) {
      JSValue rejection = JS_PromiseResult(context, raw_promise);
      Error error = ErrorFromValue(context, rejection, std::move(filename));
      JS_FreeValue(context, rejection);
      return Result<Value>::Failure(std::move(error));
    }
    if (promise_state != JS_PROMISE_PENDING) {
      return Result<Value>::Failure(Error{
          ErrorCode::Type,
          "The value is not a promise",
          std::move(filename),
      });
    }

    JSContext* job_context = nullptr;
    const int job_result = JS_ExecutePendingJob(state.runtime_state->runtime, &job_context);
    if (job_result == 0) {
      return Result<Value>::Failure(Error{
          ErrorCode::Unsupported,
          "The promise did not settle",
          std::move(filename),
      });
    }
    if (job_result < 0) {
      JSContext* error_context = job_context ? job_context : context;
      auto* error_state = static_cast<ContextState*>(JS_GetContextOpaque(error_context));
      if (error_state) {
        return Result<Value>::Failure(TakeContextError(*error_state, error_context, std::move(filename)));
      }
      return Result<Value>::Failure(CaptureException(error_context, std::move(filename)));
    }
  }
}

} // namespace detail

Context::Context(Runtime& runtime) {
  if (!runtime.state_) {
    throw std::logic_error("Cannot create a context from a moved-from QuickJS runtime");
  }

  auto state = std::make_unique<detail::ContextState>();
  state->runtime_state = runtime.state_.get();
  state->context = JS_NewContext(state->runtime_state->runtime);
  if (!state->context) {
    throw std::runtime_error("Failed to create a QuickJS context");
  }
  state->owner = this;
  JS_SetContextOpaque(state->context, state.get());
  JS_SetModuleLoaderFunc(JS_GetRuntime(state->context), nullptr, &detail::ModuleLoaderTrampoline, nullptr);
  state_ = std::move(state);
}

namespace {

void RetireContextState(detail::ContextState& state) {
  // Host callables and module loaders may own Values from this context. Release
  // them before freeing the engine context so their destructors never touch a
  // retired JSContext.
  state.native_functions.clear();
  state.module_loader = {};
  state.loader_error.reset();
  if (state.context) {
    JS_FreeContext(state.context);
    state.context = nullptr;
  }
}

} // namespace

Context::~Context() {
  if (state_) {
    RetireContextState(*state_);
  }
}

Context::Context(Context&& other) noexcept : state_(std::move(other.state_)) {
  if (state_) {
    state_->owner = this;
  }
}

Context& Context::operator=(Context&& other) noexcept {
  if (this != &other) {
    if (state_) {
      RetireContextState(*state_);
    }
    state_ = std::move(other.state_);
    if (state_) {
      state_->owner = this;
    }
  }
  return *this;
}

Result<Value> Context::Evaluate(
    std::string_view source,
    std::string_view filename,
    EvaluateOptions options
) {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  state_->loader_error.reset();
  detail::DeadlineGuard deadline(*state_->runtime_state, options.time_limit);

  const std::string text(source);
  const std::string name(filename);
  JSValue result = JS_Eval(
      state_->context,
      text.c_str(),
      text.size(),
      name.c_str(),
      JS_EVAL_TYPE_GLOBAL
  );
  if (JS_IsException(result)) {
    return Result<Value>::Failure(detail::TakeContextError(*state_, state_->context, name));
  }

  Value value = detail::ValueAccess::Make(state_->context, result);
  if (options.run_jobs) {
    auto jobs = RunJobs(options.max_jobs);
    if (!jobs.Succeeded()) {
      return Result<Value>::Failure(std::move(jobs).Error());
    }
  }
  if (options.await_promise && value.IsPromise()) {
    return detail::AwaitPromise(*state_, std::move(value), name);
  }
  return detail::Succeed(std::move(value));
}

Result<Value> Context::EvaluateModule(
    std::string_view source,
    std::string_view filename,
    EvaluateOptions options
) {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  state_->loader_error.reset();
  detail::DeadlineGuard deadline(*state_->runtime_state, options.time_limit);

  const std::string text(source);
  const std::string name(filename);
  JSValue compiled = JS_Eval(
      state_->context,
      text.c_str(),
      text.size(),
      name.c_str(),
      JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
  );
  if (JS_IsException(compiled)) {
    return Result<Value>::Failure(detail::TakeContextError(*state_, state_->context, name));
  }

  auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
  detail::SetImportMeta(state_->context, module, name);

  JSValue result = JS_EvalFunction(state_->context, compiled);
  if (JS_IsException(result)) {
    return Result<Value>::Failure(detail::TakeContextError(*state_, state_->context, name));
  }

  Value value = detail::ValueAccess::Make(state_->context, result);
  if (value.IsPromise()) {
    return detail::AwaitPromise(*state_, std::move(value), name);
  }
  if (options.run_jobs) {
    auto jobs = RunJobs(options.max_jobs);
    if (!jobs.Succeeded()) {
      return Result<Value>::Failure(std::move(jobs).Error());
    }
  }
  return detail::Succeed(std::move(value));
}

Result<int> Context::RunJobs(int max_jobs) {
  if (!state_) {
    return Result<int>::Failure(detail::ClosedError());
  }

  int executed = 0;
  while (max_jobs < 0 || executed < max_jobs) {
    JSContext* job_context = nullptr;
    const int result = JS_ExecutePendingJob(state_->runtime_state->runtime, &job_context);
    if (result == 0) {
      break;
    }
    if (result < 0) {
      JSContext* error_context = job_context ? job_context : state_->context;
      auto* error_state = static_cast<detail::ContextState*>(JS_GetContextOpaque(error_context));
      if (error_state) {
        return Result<int>::Failure(detail::TakeContextError(*error_state, error_context));
      }
      return Result<int>::Failure(detail::CaptureException(error_context));
    }
    ++executed;
  }
  return detail::Succeed(executed);
}

void Context::SetModuleLoader(ModuleLoader loader) {
  if (state_) {
    state_->module_loader = std::move(loader);
  }
}

Result<Value> Context::GlobalObject() const {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSValue global = JS_GetGlobalObject(state_->context);
  if (JS_IsException(global)) {
    return Result<Value>::Failure(detail::CaptureException(state_->context));
  }
  return detail::Succeed(detail::ValueAccess::Make(state_->context, global));
}

Result<Value> Context::Global(std::string_view name) const {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  return detail::GetGlobalRaw(*state_, name);
}

Result<void> Context::SetGlobal(std::string_view name, Value value) {
  if (!state_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  if (auto error = detail::ValidateValue(state_->context, value)) {
    return Result<void>::Failure(std::move(*error));
  }
  return detail::SetGlobalRaw(*state_, name, detail::DuplicateRaw(state_->context, value));
}

Result<void> Context::SetGlobal(std::string_view name, bool value) {
  if (!state_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  return detail::SetGlobalRaw(*state_, name, JS_NewBool(state_->context, value));
}

Result<void> Context::SetGlobal(std::string_view name, double value) {
  if (!state_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  return detail::SetGlobalRaw(*state_, name, JS_NewFloat64(state_->context, value));
}

Result<void> Context::SetGlobal(std::string_view name, std::int32_t value) {
  if (!state_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  return detail::SetGlobalRaw(*state_, name, JS_NewInt32(state_->context, value));
}

Result<void> Context::SetGlobal(std::string_view name, std::string_view value) {
  if (!state_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  JSValue text = detail::NewStringRaw(state_->context, value);
  if (JS_IsException(text)) {
    return Result<void>::Failure(detail::CaptureException(state_->context));
  }
  return detail::SetGlobalRaw(*state_, name, text);
}

Result<void> Context::SetGlobal(std::string_view name, const char* value) {
  if (!value) {
    return Result<void>::Failure(
        detail::TypeError("A null string cannot be installed as a global")
    );
  }
  return SetGlobal(name, std::string_view(value));
}

Result<void> Context::SetGlobalFunction(
    std::string_view name,
    NativeFunction function,
    std::size_t arity
) {
  if (!state_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  if (!function) {
    return Result<void>::Failure(
        detail::TypeError("A native function must be callable")
    );
  }
  if (state_->native_functions.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return Result<void>::Failure(Error{
        ErrorCode::Unsupported,
        "Too many native JavaScript functions",
    });
  }

  const auto magic = static_cast<int>(state_->native_functions.size());
  state_->native_functions.push_back(
      std::make_shared<NativeFunction>(std::move(function))
  );

  const int length = arity > static_cast<std::size_t>(std::numeric_limits<int>::max())
      ? std::numeric_limits<int>::max()
      : static_cast<int>(arity);
  const std::string function_name(name);
  JSValue raw_function = JS_NewCFunctionMagic(
      state_->context,
      &detail::NativeFunctionTrampoline,
      function_name.c_str(),
      length,
      JS_CFUNC_generic_magic,
      magic
  );
  if (JS_IsException(raw_function)) {
    return Result<void>::Failure(detail::CaptureException(state_->context));
  }

  JSValue global = JS_GetGlobalObject(state_->context);
  if (JS_IsException(global)) {
    JS_FreeValue(state_->context, raw_function);
    return Result<void>::Failure(detail::CaptureException(state_->context));
  }
  const int result = JS_SetPropertyStr(state_->context, global, function_name.c_str(), raw_function);
  JS_FreeValue(state_->context, global);
  if (result < 0) {
    return Result<void>::Failure(detail::CaptureException(state_->context));
  }
  return Result<void>::Success();
}

Value Context::Undefined() const noexcept {
  if (!state_) {
    return Value{};
  }
  return detail::ValueAccess::Make(state_->context, JS_UNDEFINED);
}

Value Context::Null() const {
  if (!state_) {
    throw std::logic_error("Cannot create a value from a moved-from QuickJS context");
  }
  return detail::ValueAccess::Make(state_->context, JS_NULL);
}

Value Context::Boolean(bool value) const {
  if (!state_) {
    throw std::logic_error("Cannot create a value from a moved-from QuickJS context");
  }
  return detail::ValueAccess::Make(state_->context, JS_NewBool(state_->context, value));
}

Value Context::Number(double value) const {
  if (!state_) {
    throw std::logic_error("Cannot create a value from a moved-from QuickJS context");
  }
  return detail::ValueAccess::Make(state_->context, JS_NewFloat64(state_->context, value));
}

Result<Value> Context::NewString(std::string_view value) const {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSValue text = detail::NewStringRaw(state_->context, value);
  if (JS_IsException(text)) {
    return Result<Value>::Failure(detail::CaptureException(state_->context));
  }
  return detail::Succeed(detail::ValueAccess::Make(state_->context, text));
}

Result<Value> Context::NewObject() const {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSValue object = JS_NewObject(state_->context);
  if (JS_IsException(object)) {
    return Result<Value>::Failure(detail::CaptureException(state_->context));
  }
  return detail::Succeed(detail::ValueAccess::Make(state_->context, object));
}

Result<Value> Context::NewArray(std::span<const Value> values) const {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSValue array = JS_NewArray(state_->context);
  if (JS_IsException(array)) {
    return Result<Value>::Failure(detail::CaptureException(state_->context));
  }

  for (std::size_t index = 0; index < values.size(); ++index) {
    if (auto error = detail::ValidateValue(state_->context, values[index])) {
      JS_FreeValue(state_->context, array);
      return Result<Value>::Failure(std::move(*error));
    }
    if (JS_SetPropertyUint32(
            state_->context,
            array,
            static_cast<uint32_t>(index),
            detail::DuplicateRaw(state_->context, values[index])
        ) < 0) {
      const Error failure = detail::CaptureException(state_->context);
      JS_FreeValue(state_->context, array);
      return Result<Value>::Failure(failure);
    }
  }
  return detail::Succeed(detail::ValueAccess::Make(state_->context, array));
}

Result<Value> Context::ParseJson(std::string_view text, std::string_view filename) const {
  if (!state_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  const std::string source(text);
  const std::string name(filename);
  JSValue value = JS_ParseJSON(
      state_->context,
      source.c_str(),
      source.size(),
      name.c_str()
  );
  if (JS_IsException(value)) {
    return Result<Value>::Failure(detail::CaptureException(state_->context, name));
  }
  return detail::Succeed(detail::ValueAccess::Make(state_->context, value));
}

} // namespace huxerui::quickjs
