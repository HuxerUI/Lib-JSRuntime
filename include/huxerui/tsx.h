#pragma once

/// @file
/// TSX-to-HuxerUI bridge.
///
/// A TSX bundle is evaluated in an embedded QuickJS context. The bundle mounts
/// one element tree; each function component becomes a HuxerUI Scope and each
/// `useState` call becomes a HuxerUI state slot, so TSX recomposition follows
/// the same model as the C++ DSL. TSX is transpiled to JavaScript by the
/// consuming application (see `tools/huxerui-tsx.mjs`); this library never
/// parses TypeScript.
///
/// The runtime is single-threaded and must outlive every View it produces.

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <huxerui/huxerui.h>
#include <huxerui/quickjs.h>

namespace huxerui::tsx {

namespace detail {
class RuntimeImpl;
}

/// Classifies diagnostics forwarded from the JavaScript runtime.
enum class LogLevel {
  Debug,
  Info,
  Warn,
  Error,
};

/// Identifies the runtime phase that produced a diagnostic.
enum class DiagnosticPhase {
  Load,       ///< Initial bundle evaluation or bundle I/O.
  Reload,     ///< Hot-reload evaluation or bundle I/O.
  Render,     ///< Function-component render or a host-element builder.
  Effect,     ///< `useEffect` setup, cleanup, or dependency handling.
  Event,      ///< Event handler invoked from a host view.
  Evaluation, ///< `Runtime::Evaluate()` or `Runtime::Invoke()`.
  Timer,      ///< Timer scheduling or callback.
  Unknown,    ///< A failure that cannot be attributed to one phase.
};

/// One structured, fully formatted runtime failure.
///
/// `description` is the same text passed to `RuntimeOptions::on_error` and
/// `RuntimeOptions::on_log`. `message`, `stack`, `filename`, `line`, and
/// `column` preserve the JavaScript details for tooling. `component_stack`
/// lists the active TSX component path, outermost first.
struct Diagnostic {
  DiagnosticPhase phase = DiagnosticPhase::Unknown;
  quickjs::ErrorCode code = quickjs::ErrorCode::Exception;
  std::string operation;
  std::string message;
  /// Original source location when the loaded bundle embedded an inline source
  /// map and the location is covered by it; the generated bundle location
  /// otherwise.
  std::string filename;
  int line = 0;
  int column = 0;
  /// Generated bundle location before source-map resolution. Populated when
  /// the engine reported a location, so tooling can correlate both views.
  std::string generated_filename;
  int generated_line = 0;
  int generated_column = 0;
  /// JavaScript stack. Frames from the loaded bundle are rewritten to their
  /// original source locations when a source map is available.
  std::string stack;
  std::string component_stack;
  std::string description;
};

/// Configures one TSX runtime.
struct RuntimeOptions {
  /// Module identity assigned to the evaluated bundle, used in diagnostics.
  std::string entry_name = "app.js";
  /// ES module source. May be left empty and supplied to Load() instead.
  std::string bundle;
  /// Optional directory used to resolve absolute module names when the bundle
  /// is not self-contained. Relative names resolved by QuickJS are joined to
  /// this directory. It also resolves relative sources from an embedded
  /// source map; file-backed bundles use the bundle's own directory.
  std::filesystem::path module_root;
  /// QuickJS heap limit in bytes; zero keeps the engine default.
  std::size_t memory_limit_bytes = 0;
  /// QuickJS native stack limit in bytes; zero keeps the engine default.
  std::size_t stack_limit_bytes = 0;
  /// Wall-clock budget for one module or Runtime::Evaluate() operation; zero
  /// disables the limit. A runtime interrupt reports ErrorCode::Interrupted.
  std::chrono::milliseconds evaluation_time_limit{0};
  /// Wall-clock budget for one component render, hook initializer, effect,
  /// event handler, or timer callback; zero disables the limit. Applies to
  /// synchronous JavaScript calls made by the bridge.
  std::chrono::milliseconds operation_time_limit{0};
  /// Cooperative interrupt probe for all JavaScript execution. Return true to
  /// abort with ErrorCode::Interrupted. The callback must be thread-safe and
  /// must not throw.
  std::function<bool()> should_interrupt;
  /// Receives structured failures before `on_log`/`on_error`.
  std::function<void(const Diagnostic&)> on_diagnostic;
  /// Receives `console` output and host diagnostics.
  std::function<void(LogLevel, std::string_view)> on_log;
  /// Receives the formatted form of every diagnostic. Rendering continues with
  /// an inline error Text when a component throws.
  std::function<void(std::string_view)> on_error;
};

/// Owns the JavaScript engine, the mounted TSX tree, and its state.
class Runtime {
public:
  explicit Runtime(RuntimeOptions options = {});
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) noexcept;
  Runtime& operator=(Runtime&&) noexcept;

  /// Evaluates `source` as the application module and resolves `mount()`.
  /// @param filename Module identity reported in diagnostics.
  quickjs::Result<void> Load(std::string_view source, std::string_view filename = "app.js");

  /// Reads and evaluates a UTF-8 JavaScript module.
  quickjs::Result<void> LoadFile(const std::filesystem::path& path);

  /// Evaluates `RuntimeOptions::bundle`.
  quickjs::Result<void> LoadConfiguredBundle();

  /// Re-evaluates a bundle and remounts the whole TSX tree with fresh state.
  ///
  /// This is the layout-preview path: layout, text, styles, and structure update
  /// immediately, while every component is remounted so hooks start from their
  /// initial values. Call from the UI thread. On evaluation failure the previous
  /// tree and root stay mounted.
  /// @param filename New module identity. Use a distinct name per revision so
  ///        the ES module cache cannot return the previous instance.
  quickjs::Result<void> Reload(std::string_view source, std::string_view filename);

  /// Reads and reloads a UTF-8 JavaScript module. A revision suffix is added to
  /// the module identity automatically.
  quickjs::Result<void> ReloadFile(const std::filesystem::path& path);

  /// Reloads `RuntimeOptions::bundle`.
  quickjs::Result<void> ReloadConfiguredBundle();

  /// Returns the mounted root as a HuxerUI composition scope.
  ///
  /// Call from an application root factory. Each call starts one composition
  /// of the TSX tree; nested component scopes retain their state.
  /// @throws std::logic_error if no bundle has been evaluated or `mount()` was
  ///         not called by the entry module.
  [[nodiscard]] View Root();

  /// @return The JavaScript context owned by this runtime.
  [[nodiscard]] quickjs::Context& Context() noexcept;
  [[nodiscard]] const quickjs::Context& Context() const noexcept;

  /// @return The most recently reported failure, or `std::nullopt` when no
  ///         diagnostic has been emitted. Diagnostics are retained until the
  ///         runtime is destroyed.
  [[nodiscard]] std::optional<Diagnostic> LastDiagnostic() const;

  /// Installs a host function on the global object. It remains available to
  /// the bundle under the same name.
  void RegisterFunction(std::string name, quickjs::NativeFunction function, std::size_t arity = 0);

  /// Installs a module resolver used after the built-in `@huxerui/tsx`
  /// modules. Relative names are already normalized by QuickJS.
  void SetModuleLoader(quickjs::ModuleLoader loader);

  /// Evaluates an expression and returns its completion value. Intended for
  /// host-driven callbacks and diagnostics, not for rendering.
  quickjs::Result<quickjs::Value> Evaluate(std::string_view source, std::string_view filename = "<eval>");

  /// Calls a global function with no arguments.
  quickjs::Result<quickjs::Value> Invoke(std::string_view global);

private:
  std::unique_ptr<detail::RuntimeImpl> impl_;
};

/// Convenience root factory for applications that own one process-wide runtime.
/// @param runtime Runtime that outlives the application's views.
[[nodiscard]] View TsxRoot(Runtime& runtime);

} // namespace huxerui::tsx
