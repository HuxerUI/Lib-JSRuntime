#include "internal.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace huxerui::quickjs {

namespace detail {

namespace {

int InterruptTrampoline(JSContext* context, void* opaque) {
  static_cast<void>(context);
  auto* state = static_cast<RuntimeState*>(opaque);
  if (!state || state->capturing_error) {
    return 0;
  }
  if (state->options.should_interrupt) {
    try {
      if (state->options.should_interrupt()) {
        state->interrupted = true;
        return 1;
      }
    } catch (...) {
      state->interrupted = true;
      return 1;
    }
  }
  if (state->deadline_active && std::chrono::steady_clock::now() >= state->deadline) {
    state->interrupted = true;
    return 1;
  }
  return 0;
}

} // namespace

RuntimeState::~RuntimeState() {
  if (runtime) {
    JS_FreeRuntime(runtime);
  }
}

} // namespace detail

Runtime::Runtime(RuntimeOptions options) {
  auto state = std::make_unique<detail::RuntimeState>();
  state->options = std::move(options);
  state->runtime = JS_NewRuntime();
  if (!state->runtime) {
    throw std::runtime_error("Failed to create a QuickJS runtime");
  }

  if (state->options.memory_limit_bytes > 0) {
    JS_SetMemoryLimit(state->runtime, state->options.memory_limit_bytes);
  }
  if (state->options.stack_limit_bytes > 0) {
    JS_SetMaxStackSize(state->runtime, state->options.stack_limit_bytes);
  }
#if defined(_WIN32)
  else {
    // Windows defaults to a 1 MiB thread stack, which matches QuickJS's
    // 1 MiB JS_DEFAULT_STACK_SIZE and can hit the OS guard page before
    // QuickJS raises RangeError. Apply a 512 KiB limit unless overridden.
    JS_SetMaxStackSize(state->runtime, 512 * 1024);
  }
#endif
  if (state->options.gc_threshold_bytes > 0) {
    JS_SetGCThreshold(state->runtime, state->options.gc_threshold_bytes);
  }

  JS_SetRuntimeOpaque(state->runtime, state.get());
  JS_SetInterruptHandler(state->runtime, &detail::InterruptTrampoline, state.get());
  state_ = std::move(state);
}

Runtime::~Runtime() = default;

Runtime::Runtime(Runtime&& other) noexcept = default;

Runtime& Runtime::operator=(Runtime&& other) noexcept = default;

} // namespace huxerui::quickjs
