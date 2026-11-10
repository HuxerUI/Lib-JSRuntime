#include "internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <source_location>
#include <typeinfo>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <huxerui/task.h>

#include "embedded_js.h"
#include "props.h"

namespace huxerui::tsx::detail {

namespace {

constexpr std::string_view kJsxRuntimeModule =
    "export { jsx, jsxs, Fragment } from \"@huxerui/tsx\";\n";

quickjs::RuntimeOptions MakeEngineOptions(
    const RuntimeOptions& source,
    std::shared_ptr<ExecutionBudget> budget
) {
  budget->user_interrupt = source.should_interrupt;
  quickjs::RuntimeOptions options;
  options.memory_limit_bytes = source.memory_limit_bytes;
  options.stack_limit_bytes = source.stack_limit_bytes;
  options.should_interrupt = [budget = std::move(budget)]() {
    if (budget->Expired()) {
      return true;
    }
    return budget->user_interrupt && budget->user_interrupt();
  };
  return options;
}

quickjs::EvaluateOptions MakeEvaluateOptions(const RuntimeOptions& source) {
  quickjs::EvaluateOptions options;
  options.time_limit = source.evaluation_time_limit;
  return options;
}

template <class T> State<T> AcquireStateCell(T initial) {
  using Cell = huxerui::detail::StateCell<T>;
  auto candidate = std::make_shared<Cell>(std::move(initial));
  const auto acquire = &::huxerui::detail::UseStateCell;
  auto cell = acquire(
      typeid(T),
      std::source_location::current(),
      std::move(candidate)
  );
  return State<T>(std::static_pointer_cast<Cell>(std::move(cell)));
}

State<JsSlot> AcquireSlotState(quickjs::Value initial) {
  return AcquireStateCell(JsSlot{std::move(initial)});
}

huxerui::FocusRequester AcquireFocusRequester() {
  const auto acquire = &huxerui::UseFocusRequester;
  return acquire(std::source_location::current());
}

huxerui::ScrollController AcquireScrollController(float initial_offset) {
  const auto acquire = &huxerui::UseScrollController;
  return acquire(initial_offset, std::source_location::current());
}

huxerui::ToastHandle AcquireToastHandle() {
  const auto acquire = &huxerui::UseToast;
  return acquire();
}

huxerui::DialogHandle AcquireDialogHandle() {
  const auto acquire = &huxerui::UseDialog;
  return acquire();
}

huxerui::MenuHandle AcquireMenuHandle() {
  const auto acquire = &huxerui::UseMenu;
  return acquire();
}

huxerui::SnackBarHandle AcquireSnackBarHandle() {
  const auto acquire = &huxerui::UseSnackBar;
  return acquire();
}

huxerui::BottomSheetHandle AcquireBottomSheetHandle() {
  const auto acquire = &huxerui::UseBottomSheet;
  return acquire();
}

huxerui::PopupHandle AcquirePopupHandle() {
  const auto acquire = &huxerui::UsePopup;
  return acquire();
}

std::string RevisionName(std::string_view name, std::uint64_t revision) {
  const auto dot = name.rfind('.');
  const std::string suffix = ".r" + std::to_string(revision);
  if (dot == std::string_view::npos) {
    return std::string(name) + suffix;
  }
  return std::string(name.substr(0, dot)) + suffix + std::string(name.substr(dot));
}

const char* ErrorCodeName(quickjs::ErrorCode code) noexcept {
  switch (code) {
    case quickjs::ErrorCode::Exception:
      return "Exception";
    case quickjs::ErrorCode::Syntax:
      return "Syntax";
    case quickjs::ErrorCode::OutOfMemory:
      return "OutOfMemory";
    case quickjs::ErrorCode::StackOverflow:
      return "StackOverflow";
    case quickjs::ErrorCode::Interrupted:
      return "Interrupted";
    case quickjs::ErrorCode::Module:
      return "Module";
    case quickjs::ErrorCode::Type:
      return "Type";
    case quickjs::ErrorCode::Closed:
      return "Closed";
    case quickjs::ErrorCode::Unsupported:
      return "Unsupported";
  }
  return "Unknown";
}

const char* PhaseName(DiagnosticPhase phase) noexcept {
  switch (phase) {
    case DiagnosticPhase::Load:
      return "load";
    case DiagnosticPhase::Reload:
      return "reload";
    case DiagnosticPhase::Render:
      return "render";
    case DiagnosticPhase::Effect:
      return "effect";
    case DiagnosticPhase::Event:
      return "event";
    case DiagnosticPhase::Evaluation:
      return "evaluation";
    case DiagnosticPhase::Timer:
      return "timer";
    case DiagnosticPhase::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string FormatDiagnostic(const Diagnostic& diagnostic) {
  std::string text;
  text += PhaseName(diagnostic.phase);
  text += " [";
  text += ErrorCodeName(diagnostic.code);
  text += "]: ";
  text += diagnostic.operation;
  if (!diagnostic.message.empty()) {
    text += ": ";
    text += diagnostic.message;
  }
  if (!diagnostic.filename.empty()) {
    text += " (";
    text += diagnostic.filename;
    if (diagnostic.line > 0) {
      text += ":";
      text += std::to_string(diagnostic.line);
      if (diagnostic.column > 0) {
        text += ":";
        text += std::to_string(diagnostic.column);
      }
    }
    text += ")";
  }
  if (!diagnostic.stack.empty()) {
    text += "\n";
    text += diagnostic.stack;
  }
  if (!diagnostic.component_stack.empty()) {
    text += "\ncomponent stack: ";
    text += diagnostic.component_stack;
  }
  return text;
}

std::uint64_t FunctionKey(const quickjs::Value& function) {
  const auto source = function.ToString();
  if (source.Succeeded()) {
    return static_cast<std::uint64_t>(std::hash<std::string>{}(source.Value()));
  }
  return 0;
}

std::string FunctionName(const quickjs::Value& function) {
  const auto name = function.GetProperty("name");
  if (name.Succeeded()) {
    const auto text = name.Value().ToString();
    if (text.Succeeded() && !text.Value().empty()) {
      return text.Value();
    }
  }
  return "<anonymous>";
}

std::string FirstLine(std::string_view text) {
  const auto newline = text.find('\n');
  return std::string(text.substr(0, newline));
}

class ComponentPathScope final {
public:
  ComponentPathScope(RuntimeImpl& runtime, std::string_view path)
      : runtime_(runtime), previous_(runtime.ActiveComponentPath()) {
    runtime_.SetActiveComponentPath(std::string(path));
  }

  ~ComponentPathScope() { runtime_.SetActiveComponentPath(std::move(previous_)); }

  ComponentPathScope(const ComponentPathScope&) = delete;
  ComponentPathScope& operator=(const ComponentPathScope&) = delete;

private:
  RuntimeImpl& runtime_;
  std::string previous_;
};

class OperationBudgetScope final {
public:
  OperationBudgetScope(ExecutionBudget& budget, std::chrono::milliseconds limit)
      : budget_(budget), previous_active_(budget.active), previous_deadline_(budget.deadline) {
    if (limit.count() > 0) {
      budget_.active = true;
      budget_.deadline = std::chrono::steady_clock::now() + limit;
    }
  }

  ~OperationBudgetScope() {
    budget_.active = previous_active_;
    budget_.deadline = previous_deadline_;
  }

  OperationBudgetScope(const OperationBudgetScope&) = delete;
  OperationBudgetScope& operator=(const OperationBudgetScope&) = delete;

private:
  ExecutionBudget& budget_;
  bool previous_active_ = false;
  std::chrono::steady_clock::time_point previous_deadline_{};
};

class CurrentFrameGuard final {
public:
  CurrentFrameGuard(RuntimeImpl& runtime, RuntimeImpl::RenderFrame& frame)
      : runtime_(runtime), previous_(runtime.CurrentFrame()) {
    runtime_.SetCurrentFrame(&frame);
  }

  ~CurrentFrameGuard() { runtime_.SetCurrentFrame(previous_); }

  CurrentFrameGuard(const CurrentFrameGuard&) = delete;
  CurrentFrameGuard& operator=(const CurrentFrameGuard&) = delete;

private:
  RuntimeImpl& runtime_;
  RuntimeImpl::RenderFrame* previous_ = nullptr;
};

std::vector<huxerui::MenuEntry> BuildMenuEntries(
    RuntimeImpl& runtime,
    const quickjs::Value& entries_value,
    std::string_view component
) {
  std::vector<huxerui::MenuEntry> entries;
  if (!entries_value.IsArray()) {
    return entries;
  }
  const auto length = entries_value.Length();
  if (!length.Succeeded() || length.Value() < 0) {
    return entries;
  }
  entries.reserve(static_cast<std::size_t>(length.Value()));
  for (std::int64_t index = 0; index < length.Value(); ++index) {
    const auto value = entries_value.GetElement(static_cast<std::uint32_t>(index));
    if (!value.Succeeded() || !value.Value().IsObject()) {
      continue;
    }
    if (BoolProperty(value.Value(), "section").value_or(false)) {
      entries.emplace_back(huxerui::MenuSection{});
      continue;
    }
    const auto label = StringProperty(value.Value(), "label");
    if (!label || label->empty()) {
      continue;
    }
    auto callback = std::function<void()>([] {});
    if (const auto function = Property(value.Value(), "onSelect");
        function && function->IsFunction()) {
      callback = [impl = &runtime, function = *function, component]() {
        impl->InvokeHandler(function, {}, DiagnosticPhase::Event, component);
      };
    }
    huxerui::MenuItem item(*label, std::move(callback));
    if (const auto disabled = BoolProperty(value.Value(), "disabled"); disabled) {
      item = std::move(item).Enabled(!*disabled);
    }
    if (const auto checked = BoolProperty(value.Value(), "checked"); checked) {
      item = std::move(item).Checked(*checked);
    }
    entries.emplace_back(std::move(item));
  }
  return entries;
}

} // namespace

struct TimerRecord {
  std::uint64_t id = 0;
  bool repeating = false;
  bool cancelled = false;
  std::chrono::duration<double, std::milli> delay{0};
  quickjs::Value callback;
  std::string component_stack;
  huxerui::TaskHandle handle;
};

bool JsDeps::operator==(const JsDeps& other) const {
  if (always_run != other.always_run) {
    return false;
  }
  if (values.size() != other.values.size()) {
    return false;
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto equal = values[index].StrictEquals(other.values[index]);
    if (!equal.Succeeded() || !equal.Value()) {
      return false;
    }
  }
  return true;
}

RuntimeImpl::RuntimeImpl(RuntimeOptions options_in)
    : options(std::move(options_in)),
      budget(std::make_shared<ExecutionBudget>()),
      engine(MakeEngineOptions(options, budget)),
      context_(engine) {
  InstallHostFunctions();
  InstallModuleLoader();
}

RuntimeImpl::~RuntimeImpl() {
  CancelTimers();
}

void RuntimeImpl::InstallHostFunctions() {
  auto install = [this](std::string_view name, quickjs::NativeFunction function, std::size_t arity) {
    auto result = context_.SetGlobalFunction(name, std::move(function), arity);
    if (!result.Succeeded()) {
      throw std::runtime_error(
          "huxerui::tsx could not install host function '" + std::string(name) +
          "': " + result.Error().Message()
      );
    }
  };

  install("__huxerui_useState", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseState(owner, arguments);
  }, 1);
  install("__huxerui_stateValue", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostStateValue(owner, arguments);
  }, 1);
  install("__huxerui_setState", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostSetState(owner, arguments);
  }, 2);
  install("__huxerui_useRef", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseRef(owner, arguments);
  }, 1);
  install("__huxerui_useEffect", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseEffect(owner, arguments);
  }, 2);
  install("setTimeout", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostSetTimer(owner, arguments, false);
  }, 2);
  install("setInterval", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostSetTimer(owner, arguments, true);
  }, 2);
  install("clearTimeout", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostClearTimer(owner, arguments);
  }, 1);
  install("clearInterval", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostClearTimer(owner, arguments);
  }, 1);
  install("__huxerui_useFocusRequester", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseFocusRequester(owner, arguments);
  }, 0);
  install("__huxerui_requestFocus", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostRequestFocus(owner, arguments);
  }, 1);
  install("__huxerui_useScrollController", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseScrollController(owner, arguments);
  }, 1);
  install("__huxerui_scrollTo", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostScrollTo(owner, arguments);
  }, 2);
  install("__huxerui_scrollBy", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostScrollBy(owner, arguments);
  }, 2);
  install("__huxerui_scrollToItem", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostScrollToItem(owner, arguments);
  }, 3);
  install("__huxerui_scrollOffset", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostScrollOffset(owner, arguments);
  }, 1);
  install("__huxerui_scrollMetrics", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostScrollMetrics(owner, arguments);
  }, 1);
  install("__huxerui_useToast", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseToast(owner, arguments);
  }, 0);
  install("__huxerui_showToast", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowToast(owner, arguments);
  }, 3);
  install("__huxerui_dismissToast", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostDismissToast(owner, arguments);
  }, 2);
  install("__huxerui_useDialog", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseDialog(owner, arguments);
  }, 0);
  install("__huxerui_showDialog", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowDialog(owner, arguments);
  }, 2);
  install("__huxerui_showDialogContent", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowDialogContent(owner, arguments);
  }, 3);
  install("__huxerui_dismissDialog", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostDismissDialog(owner, arguments);
  }, 2);
  install("__huxerui_useMenu", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseMenu(owner, arguments);
  }, 0);
  install("__huxerui_showMenu", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowMenu(owner, arguments);
  }, 4);
  install("__huxerui_showMenuAnchored", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowMenuAnchored(owner, arguments);
  }, 2);
  install("__huxerui_dismissMenu", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostDismissMenu(owner, arguments);
  }, 2);
  install("__huxerui_useSnackBar", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseSnackBar(owner, arguments);
  }, 0);
  install("__huxerui_showSnackBar", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowSnackBar(owner, arguments);
  }, 5);
  install("__huxerui_dismissSnackBar", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostDismissSnackBar(owner, arguments);
  }, 2);
  install("__huxerui_useBottomSheet", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUseBottomSheet(owner, arguments);
  }, 0);
  install("__huxerui_showBottomSheet", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowBottomSheet(owner, arguments);
  }, 3);
  install("__huxerui_dismissBottomSheet", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostDismissBottomSheet(owner, arguments);
  }, 2);
  install("__huxerui_usePopup", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostUsePopup(owner, arguments);
  }, 0);
  install("__huxerui_showPopup", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostShowPopup(owner, arguments);
  }, 3);
  install("__huxerui_dismissPopup", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    return HostDismissPopup(owner, arguments);
  }, 2);
  install("__huxerui_log", [this](quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
    if (arguments.size() < 2) {
      return owner.Undefined();
    }
    const auto level = arguments[0].ToString();
    const auto message = arguments[1].ToString();
    if (level.Succeeded() && message.Succeeded()) {
      const std::string name = level.Value();
      LogLevel parsed = LogLevel::Info;
      if (name == "debug") {
        parsed = LogLevel::Debug;
      } else if (name == "warn") {
        parsed = LogLevel::Warn;
      } else if (name == "error") {
        parsed = LogLevel::Error;
      }
      Log(parsed, message.Value());
    }
    return owner.Undefined();
  }, 2);
}

void RuntimeImpl::InstallModuleLoader() {
  context_.SetModuleLoader([this](std::string_view name) -> quickjs::Result<std::string> {
    if (name == "@huxerui/tsx" || name == "@huxerui/tsx/index") {
      return quickjs::Result<std::string>::Success(std::string(kTsxRuntimeModule));
    }
    if (name == "@huxerui/tsx/jsx-runtime") {
      return quickjs::Result<std::string>::Success(std::string(kJsxRuntimeModule));
    }
    if (user_loader_) {
      return user_loader_(name);
    }
    if (!options.module_root.empty()) {
      const std::filesystem::path relative(name.starts_with('/') ? name.substr(1) : name);
      const auto path = options.module_root / relative;
      std::ifstream stream(path, std::ios::binary);
      if (stream) {
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return quickjs::Result<std::string>::Success(buffer.str());
      }
    }
    return quickjs::Result<std::string>::Failure(quickjs::Error{
        quickjs::ErrorCode::Module,
        "Cannot resolve module '" + std::string(name) +
            "'. Bundle the application or provide Runtime::SetModuleLoader().",
        std::string(name),
    });
  });
}

void RuntimeImpl::ReportError(
    DiagnosticPhase phase,
    std::string_view operation,
    const quickjs::Error& error,
    std::string_view component_stack
) {
  Diagnostic diagnostic;
  diagnostic.phase = phase;
  diagnostic.code = error.Code();
  diagnostic.operation = std::string(operation);
  diagnostic.message = error.Message();
  diagnostic.filename = error.Filename();
  diagnostic.line = error.Line();
  diagnostic.column = error.Column();
  diagnostic.generated_filename = error.Filename();
  diagnostic.generated_line = error.Line();
  diagnostic.generated_column = error.Column();
  diagnostic.stack = error.Stack();
  diagnostic.component_stack = std::string(component_stack);

  const bool map_applies = source_map_ && !source_map_module_.empty()
      && error.Filename() == source_map_module_;
  if (map_applies) {
    if (error.Line() > 0) {
      if (const auto mapped = source_map_->Find(error.Line(), error.Column(), source_map_base_)) {
        diagnostic.filename = mapped->filename;
        diagnostic.line = mapped->line;
        diagnostic.column = mapped->column;
      }
    }
    diagnostic.stack = RemapStack(
        diagnostic.stack,
        source_map_module_,
        *source_map_,
        source_map_base_
    );
  }

  ReportDiagnostic(std::move(diagnostic));
}

void RuntimeImpl::ReportMessage(
    DiagnosticPhase phase,
    std::string_view operation,
    std::string_view message,
    std::string_view component_stack
) {
  Diagnostic diagnostic;
  diagnostic.phase = phase;
  diagnostic.code = quickjs::ErrorCode::Exception;
  diagnostic.operation = std::string(operation);
  diagnostic.message = std::string(message);
  diagnostic.component_stack = std::string(component_stack);
  ReportDiagnostic(std::move(diagnostic));
}

void RuntimeImpl::ReportDiagnostic(Diagnostic diagnostic) {
  diagnostic.description = FormatDiagnostic(diagnostic);
  last_diagnostic_ = diagnostic;
  if (options.on_diagnostic) {
    try {
      options.on_diagnostic(diagnostic);
    } catch (...) {
      // A diagnostic sink must not mask the original JavaScript failure.
    }
  }
  if (options.on_log) {
    try {
      options.on_log(LogLevel::Error, diagnostic.description);
    } catch (...) {
    }
  }
  if (options.on_error) {
    try {
      options.on_error(diagnostic.description);
    } catch (...) {
    }
  }
}

void RuntimeImpl::Log(LogLevel level, std::string_view message) {
  if (options.on_log) {
    try {
      options.on_log(level, message);
    } catch (...) {
    }
  }
}

View RuntimeImpl::ApplyMenuAnchor(View view, const quickjs::Value& props) {
  const auto anchor = Property(props, "menuAnchor");
  if (!anchor || !anchor->IsObject()) {
    return view;
  }
  const auto id = NumberProperty(*anchor, "id");
  if (!id || !std::isfinite(*id)) {
    return view;
  }
  const auto found = menu_handles_.find(static_cast<std::uint64_t>(*id));
  if (found == menu_handles_.end()) {
    return view;
  }
  return std::move(view).With(found->second.Anchor());
}

View RuntimeImpl::ApplyPopupAnchor(View view, const quickjs::Value& props) {
  const auto anchor = Property(props, "popupAnchor");
  if (!anchor || !anchor->IsObject()) {
    return view;
  }
  const auto id = NumberProperty(*anchor, "id");
  if (!id || !std::isfinite(*id)) {
    return view;
  }
  const auto found = popup_handles_.find(static_cast<std::uint64_t>(*id));
  if (found == popup_handles_.end()) {
    return view;
  }
  return std::move(view).With(found->second.Anchor());
}

View RuntimeImpl::ApplyFocusRef(View view, const quickjs::Value& props) {
  const auto ref = Property(props, "ref");
  const auto focus_ref = ref ? ref : Property(props, "focusRef");
  if (!focus_ref || !focus_ref->IsObject()) {
    return view;
  }
  const auto id = NumberProperty(*focus_ref, "id");
  if (!id || !std::isfinite(*id)) {
    return view;
  }
  const auto found = focus_requesters_.find(static_cast<std::uint64_t>(*id));
  if (found == focus_requesters_.end()) {
    return view;
  }
  return std::move(view).With(FocusTarget{found->second});
}

std::string RuntimeImpl::ComponentPath(const ComponentInstance& instance) const {
  std::vector<std::string> chain;
  chain.push_back(instance.name);
  auto parent = instance.parent.lock();
  while (parent) {
    chain.push_back(parent->name);
    parent = parent->parent.lock();
  }

  std::string path;
  for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator) {
    if (!path.empty()) {
      path += " > ";
    }
    path += *iterator;
  }
  return path;
}

std::uint64_t RuntimeImpl::NextComponentKey(const quickjs::Value& function) {
  const auto hash = FunctionKey(function);
  std::uint32_t occurrence = 0;
  if (current_frame_ != nullptr) {
    occurrence = current_frame_->component_occurrences[hash]++;
  } else {
    occurrence = root_component_occurrences_[hash]++;
  }
  return hash ^ (static_cast<std::uint64_t>(occurrence) * 0x9E3779B97F4A7C15ULL);
}

std::string RuntimeImpl::CurrentComponentPath() const {
  if (current_frame_ != nullptr && current_frame_->instance != nullptr) {
    return ComponentPath(*current_frame_->instance);
  }
  return active_component_path_;
}

void RuntimeImpl::InvokeHandler(
    const quickjs::Value& function,
    std::span<const quickjs::Value> arguments,
    DiagnosticPhase phase,
    std::string_view component_stack
) {
  if (!function.IsFunction()) {
    return;
  }
  ComponentPathScope path_scope(*this, component_stack);
  OperationBudgetScope budget_scope(*budget, options.operation_time_limit);
  const auto result = function.Call(arguments);
  if (!result.Succeeded()) {
    ReportError(phase, "event handler", result.Error(), component_stack);
  }
}

bool RuntimeImpl::InvokeDecisionHandler(
    const quickjs::Value& function,
    std::span<const quickjs::Value> arguments,
    DiagnosticPhase phase,
    std::string_view component_stack
) {
  if (!function.IsFunction()) {
    return false;
  }
  ComponentPathScope path_scope(*this, component_stack);
  OperationBudgetScope budget_scope(*budget, options.operation_time_limit);
  const auto result = function.Call(arguments);
  if (!result.Succeeded()) {
    ReportError(phase, "event handler", result.Error(), component_stack);
    return false;
  }
  const auto truthy = result.Value().ToBoolean();
  return truthy.Succeeded() && truthy.Value();
}

View RuntimeImpl::ErrorView(std::string_view message) const {
  return Text("TSX error: " + FirstLine(message));
}

quickjs::Value RuntimeImpl::HostUseState(quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
  auto* frame = current_frame_;
  if (frame == nullptr || frame->instance == nullptr) {
    throw std::runtime_error("useState() was called outside a TSX component render");
  }
  auto& instance = *frame->instance;
  const std::size_t index = frame->next_hook++;
  quickjs::Value initial = arguments.empty() ? owner.Undefined() : arguments[0];
  if (index >= instance.states.size() && initial.IsFunction()) {
    OperationBudgetScope budget_scope(*budget, options.operation_time_limit);
    const auto produced = initial.Call();
    if (!produced.Succeeded()) {
      throw std::runtime_error(
          "useState() initializer failed: " + produced.Error().Message()
      );
    }
    initial = produced.Value();
  }

  if (index >= instance.states.size()) {
    const std::uint64_t handle = next_handle++;
    instance.states.push_back(AcquireSlotState(initial));
    instance.handles.push_back(handle);
    handles.emplace(handle, std::make_pair(std::weak_ptr<ComponentInstance>(current_frame_->owner), index));
  } else {
    instance.states[index] = AcquireSlotState(owner.Undefined());
  }

  return owner.Number(static_cast<double>(instance.handles[index]));
}

quickjs::Value RuntimeImpl::HostStateValue(quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
  if (arguments.empty()) {
    return owner.Undefined();
  }
  const auto number = arguments[0].ToNumber();
  if (!number.Succeeded()) {
    return owner.Undefined();
  }
  const auto handle = static_cast<std::uint64_t>(number.Value());
  const auto found = handles.find(handle);
  if (found == handles.end()) {
    return owner.Undefined();
  }
  const auto instance = found->second.first.lock();
  if (!instance || found->second.second >= instance->states.size()) {
    handles.erase(found);
    return owner.Undefined();
  }
  const auto& slot = instance->states[found->second.second].Get();
  return slot.value.IsUndefined() ? owner.Undefined() : slot.value;
}

quickjs::Value RuntimeImpl::HostSetState(quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
  if (arguments.size() < 2) {
    return owner.Undefined();
  }
  const auto number = arguments[0].ToNumber();
  if (!number.Succeeded()) {
    return owner.Undefined();
  }
  const auto handle = static_cast<std::uint64_t>(number.Value());
  const auto found = handles.find(handle);
  if (found == handles.end()) {
    return owner.Undefined();
  }
  const auto instance = found->second.first.lock();
  if (!instance || found->second.second >= instance->states.size()) {
    handles.erase(found);
    return owner.Undefined();
  }
  instance->states[found->second.second] = JsSlot{arguments[1]};
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostUseRef(quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
  auto* frame = current_frame_;
  if (frame == nullptr || frame->instance == nullptr) {
    throw std::runtime_error("useRef() was called outside a TSX component render");
  }
  auto& instance = *frame->instance;
  const std::size_t index = frame->next_hook++;

  if (index >= instance.states.size()) {
    auto object = owner.NewObject();
    if (!object.Succeeded()) {
      throw std::runtime_error(object.Error().Message());
    }
    const quickjs::Value initial = arguments.empty() ? owner.Undefined() : arguments[0];
    auto assigned = object.Value().SetProperty("current", initial);
    if (!assigned.Succeeded()) {
      throw std::runtime_error(assigned.Error().Message());
    }
    const std::uint64_t handle = next_handle++;
    instance.states.push_back(AcquireSlotState(object.Value()));
    instance.handles.push_back(handle);
    handles.emplace(handle, std::make_pair(std::weak_ptr<ComponentInstance>(current_frame_->owner), index));
  } else {
    instance.states[index] = AcquireSlotState(owner.Undefined());
  }

  const auto& slot = instance.states[index].Get();
  return slot.value.IsUndefined() ? owner.Undefined() : slot.value;
}

quickjs::Value RuntimeImpl::HostUseEffect(quickjs::Context& owner, std::span<const quickjs::Value> arguments) {
  if (arguments.empty() || !arguments[0].IsFunction()) {
    throw std::runtime_error("useEffect() requires a function");
  }

  JsDeps deps;
  if (arguments.size() < 2 || arguments[1].IsNull() || arguments[1].IsUndefined()) {
    deps.always_run = next_effect_dependency_++;
  } else if (arguments[1].IsArray()) {
    const auto length = arguments[1].Length();
    if (length.Succeeded()) {
      deps.values.reserve(static_cast<std::size_t>(length.Value()));
      for (std::int64_t index = 0; index < length.Value(); ++index) {
        auto element = arguments[1].GetElement(static_cast<std::uint32_t>(index));
        if (element.Succeeded()) {
          deps.values.push_back(element.Value());
        }
      }
    }
  } else {
    throw std::runtime_error("useEffect() dependencies must be an array, null, or undefined");
  }

  const quickjs::Value effect = arguments[0];
  const std::string component = CurrentComponentPath();
  huxerui::Lifecycle(
      [impl = this, effect, component]() -> std::function<void()> {
        ComponentPathScope path_scope(*impl, component);
        OperationBudgetScope budget_scope(*impl->budget, impl->options.operation_time_limit);
        const auto result = effect.Call();
        if (!result.Succeeded()) {
          impl->ReportError(DiagnosticPhase::Effect, "useEffect", result.Error(), component);
          return std::function<void()>([] {});
        }
        const auto cleanup = result.Value();
        if (!cleanup.IsFunction()) {
          return std::function<void()>([] {});
        }
        return std::function<void()>([impl, cleanup, component]() {
          impl->InvokeHandler(cleanup, {}, DiagnosticPhase::Effect, component);
        });
      },
      std::move(deps)
  );
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostSetTimer(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments,
    bool repeating
) {
  if (arguments.empty() || !arguments[0].IsFunction()) {
    throw std::runtime_error(
        std::string(repeating ? "setInterval" : "setTimeout") +
        "() requires a callback function"
    );
  }

  double milliseconds = 0.0;
  if (arguments.size() > 1) {
    const auto number = arguments[1].ToNumber();
    if (!number.Succeeded()) {
      throw std::runtime_error("Timer delay must be a number");
    }
    if (std::isfinite(number.Value()) && number.Value() > 0.0) {
      milliseconds = std::min(number.Value(), 86'400'000.0);
    }
  }

  auto timer = std::make_shared<TimerRecord>();
  timer->id = next_timer_id_++;
  timer->repeating = repeating;
  timer->delay = std::chrono::duration<double, std::milli>(milliseconds);
  timer->callback = arguments[0];
  timer->component_stack = CurrentComponentPath();
  timers_.emplace(timer->id, timer);

  try {
    ScheduleTimer(timer);
  } catch (const std::exception& error) {
    timers_.erase(timer->id);
    ReportMessage(
        DiagnosticPhase::Timer,
        repeating ? "setInterval" : "setTimeout",
        error.what(),
        timer->component_stack
    );
    throw;
  }
  return owner.Number(static_cast<double>(timer->id));
}

quickjs::Value RuntimeImpl::HostClearTimer(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.empty()) {
    return owner.Undefined();
  }
  const auto number = arguments[0].ToNumber();
  if (!number.Succeeded() || !std::isfinite(number.Value())) {
    return owner.Undefined();
  }
  const auto id = static_cast<std::uint64_t>(number.Value());
  const auto found = timers_.find(id);
  if (found == timers_.end()) {
    return owner.Undefined();
  }
  const auto timer = std::move(found->second);
  timers_.erase(found);
  timer->cancelled = true;
  timer->handle.Cancel();
  return owner.Undefined();
}

void RuntimeImpl::ScheduleTimer(const std::shared_ptr<TimerRecord>& timer) {
  auto tasks = huxerui::UseApplicationTaskScope();
  auto* self = this;
  timer->handle = tasks.Launch([self, timer]() -> huxerui::Task<void> {
    while (!timer->cancelled) {
      co_await huxerui::Delay(timer->delay);
      if (timer->cancelled) {
        break;
      }
      self->FireTimer(timer);
      if (!timer->repeating) {
        break;
      }
    }
  });
}

void RuntimeImpl::FireTimer(const std::shared_ptr<TimerRecord>& timer) {
  if (timer->cancelled) {
    return;
  }
  ComponentPathScope path_scope(*this, timer->component_stack);
  OperationBudgetScope budget_scope(*budget, options.operation_time_limit);
  const auto result = timer->callback.Call();
  if (!result.Succeeded()) {
    ReportError(
        DiagnosticPhase::Timer,
        "timer callback",
        result.Error(),
        timer->component_stack
    );
  }
  if (!timer->repeating) {
    timers_.erase(timer->id);
  }
}

quickjs::Value RuntimeImpl::HostUseFocusRequester(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto requester = AcquireFocusRequester();
  const auto id = next_focus_requester_id_++;
  focus_requesters_.emplace(id, requester);
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostRequestFocus(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.empty()) {
    return owner.Boolean(false);
  }
  const auto number = arguments[0].ToNumber();
  if (!number.Succeeded() || !std::isfinite(number.Value())) {
    return owner.Boolean(false);
  }
  const auto id = static_cast<std::uint64_t>(number.Value());
  const auto found = focus_requesters_.find(id);
  if (found == focus_requesters_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(found->second.RequestFocus());
}

std::optional<huxerui::ScrollController> RuntimeImpl::FindScrollController(
    const quickjs::Value& ref
) const {
  if (!ref.IsObject()) {
    return std::nullopt;
  }
  const auto id = NumberProperty(ref, "id");
  if (!id || !std::isfinite(*id)) {
    return std::nullopt;
  }
  const auto found = scroll_controllers_.find(static_cast<std::uint64_t>(*id));
  if (found == scroll_controllers_.end()) {
    return std::nullopt;
  }
  return found->second;
}

quickjs::Value RuntimeImpl::HostUseScrollController(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  double initial_offset = 0.0;
  if (!arguments.empty()) {
    const auto number = arguments[0].ToNumber();
    if (number.Succeeded() && std::isfinite(number.Value())) {
      initial_offset = number.Value();
    }
  }
  auto controller = AcquireScrollController(static_cast<float>(initial_offset));
  const auto id = next_scroll_controller_id_++;
  scroll_controllers_.emplace(id, controller);
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostScrollTo(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto id = arguments[0].ToNumber();
  const auto offset = arguments[1].ToNumber();
  if (!id.Succeeded() || !offset.Succeeded() || !std::isfinite(id.Value())
      || !std::isfinite(offset.Value())) {
    return owner.Boolean(false);
  }
  const auto found = scroll_controllers_.find(static_cast<std::uint64_t>(id.Value()));
  if (found == scroll_controllers_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(found->second.ScrollTo(static_cast<float>(offset.Value())));
}

quickjs::Value RuntimeImpl::HostScrollBy(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto id = arguments[0].ToNumber();
  const auto delta = arguments[1].ToNumber();
  if (!id.Succeeded() || !delta.Succeeded() || !std::isfinite(id.Value())
      || !std::isfinite(delta.Value())) {
    return owner.Boolean(false);
  }
  const auto found = scroll_controllers_.find(static_cast<std::uint64_t>(id.Value()));
  if (found == scroll_controllers_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(found->second.ScrollBy(static_cast<float>(delta.Value())));
}

quickjs::Value RuntimeImpl::HostScrollToItem(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto id = arguments[0].ToNumber();
  const auto index = arguments[1].ToNumber();
  if (!id.Succeeded() || !index.Succeeded() || !std::isfinite(id.Value())
      || !std::isfinite(index.Value()) || index.Value() < 0.0) {
    return owner.Boolean(false);
  }
  const auto found = scroll_controllers_.find(static_cast<std::uint64_t>(id.Value()));
  if (found == scroll_controllers_.end()) {
    return owner.Boolean(false);
  }
  ScrollAlignment alignment = ScrollAlignment::Start;
  if (arguments.size() > 2) {
    const auto name = arguments[2].ToString();
    if (name.Succeeded()) {
      if (name.Value() == "center") {
        alignment = ScrollAlignment::Center;
      } else if (name.Value() == "end") {
        alignment = ScrollAlignment::End;
      }
    }
  }
  return owner.Boolean(found->second.ScrollToItem(
      static_cast<std::size_t>(index.Value()),
      alignment
  ));
}

quickjs::Value RuntimeImpl::HostScrollOffset(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.empty()) {
    return owner.Number(0.0);
  }
  const auto id = arguments[0].ToNumber();
  if (!id.Succeeded() || !std::isfinite(id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = scroll_controllers_.find(static_cast<std::uint64_t>(id.Value()));
  if (found == scroll_controllers_.end()) {
    return owner.Number(0.0);
  }
  return owner.Number(static_cast<double>(found->second.Offset()));
}

quickjs::Value RuntimeImpl::HostUseToast(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto handle = AcquireToastHandle();
  const auto id = next_toast_handle_id_++;
  toast_handles_.emplace(id, std::move(handle));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowToast(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = toast_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == toast_handles_.end()) {
    return owner.Number(0.0);
  }
  const auto message = arguments[1].ToString();
  if (!message.Succeeded() || message.Value().empty()) {
    return owner.Number(0.0);
  }
  double duration = 2.0;
  if (arguments.size() > 2) {
    const auto value = arguments[2].ToNumber();
    if (value.Succeeded() && std::isfinite(value.Value()) && value.Value() >= 0.0) {
      duration = value.Value();
    }
  }
  const auto id = found->second.Show(message.Value(), huxerui::ToastOptions{.duration = duration});
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostDismissToast(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto layer_id = arguments[1].ToNumber();
  if (!handle_id.Succeeded() || !layer_id.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(layer_id.Value())) {
    return owner.Boolean(false);
  }
  const auto found = toast_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == toast_handles_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(
      found->second.Dismiss(static_cast<huxerui::LayerId>(layer_id.Value()))
  );
}

quickjs::Value RuntimeImpl::HostUseDialog(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto handle = AcquireDialogHandle();
  const auto id = next_dialog_handle_id_++;
  dialog_handles_.emplace(id, std::move(handle));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowDialog(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = dialog_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == dialog_handles_.end()) {
    return owner.Number(0.0);
  }

  const auto& options = arguments[1];
  const auto title = StringProperty(options, "title").value_or("");
  const auto message = StringProperty(options, "message").value_or("");
  const auto positive = StringProperty(options, "positive").value_or("");
  const auto negative = StringProperty(options, "negative").value_or("");
  const auto component = CurrentComponentPath();

  auto make_callback = [impl = this, component](const quickjs::Value& function) {
    if (!function.IsFunction()) {
      return std::function<void()>();
    }
    return std::function<void()>([impl, function, component]() {
      impl->InvokeHandler(function, {}, DiagnosticPhase::Event, component);
    });
  };
  auto on_positive = std::function<void()>();
  auto on_negative = std::function<void()>();
  if (const auto value = Property(options, "onPositive"); value) {
    on_positive = make_callback(*value);
  }
  if (const auto value = Property(options, "onNegative"); value) {
    on_negative = make_callback(*value);
  }

  huxerui::DialogOptions dialog_options;
  if (const auto value = BoolProperty(options, "dismissOnOutsidePress"); value) {
    dialog_options.dismiss_on_outside_press = *value;
  }
  if (const auto value = BoolProperty(options, "dismissOnCancel"); value) {
    dialog_options.dismiss_on_cancel = *value;
  }

  huxerui::LayerId id = 0;
  if (!negative.empty()) {
    id = found->second.Show(
        title,
        message,
        positive,
        negative,
        std::move(on_positive),
        std::move(on_negative),
        std::move(dialog_options)
    );
  } else {
    id = found->second.Show(
        title,
        message,
        positive,
        std::move(on_positive),
        std::move(dialog_options)
    );
  }
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowDialogContent(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 3) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = dialog_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == dialog_handles_.end() || !arguments[1].IsFunction()) {
    return owner.Number(0.0);
  }

  const auto& options = arguments[2];
  huxerui::DialogOptions dialog_options;
  if (const auto value = BoolProperty(options, "dismissOnOutsidePress"); value) {
    dialog_options.dismiss_on_outside_press = *value;
  }
  if (const auto value = BoolProperty(options, "dismissOnCancel"); value) {
    dialog_options.dismiss_on_cancel = *value;
  }

  const auto render = arguments[1];
  const auto component = CurrentComponentPath();
  auto content = [impl = this, render, component]() -> View {
    const auto rendered = render.Call();
    if (!rendered.Succeeded()) {
      impl->ReportError(
          DiagnosticPhase::Render,
          "dialog content",
          rendered.Error(),
          component
      );
      return impl->ErrorView(rendered.Error().Message());
    }
    return impl->RenderValue(rendered.Value());
  };
  const auto id = found->second.Show(std::move(content), std::move(dialog_options));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostDismissDialog(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto layer_id = arguments[1].ToNumber();
  if (!handle_id.Succeeded() || !layer_id.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(layer_id.Value())) {
    return owner.Boolean(false);
  }
  const auto found = dialog_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == dialog_handles_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(
      found->second.Dismiss(static_cast<huxerui::LayerId>(layer_id.Value()))
  );
}

quickjs::Value RuntimeImpl::HostUseMenu(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto handle = AcquireMenuHandle();
  const auto id = next_menu_handle_id_++;
  menu_handles_.emplace(id, std::move(handle));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowMenu(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 4) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto x = arguments[1].ToNumber();
  const auto y = arguments[2].ToNumber();
  if (!handle_id.Succeeded() || !x.Succeeded() || !y.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(x.Value())
      || !std::isfinite(y.Value())) {
    return owner.Number(0.0);
  }
  const auto found = menu_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == menu_handles_.end()) {
    return owner.Number(0.0);
  }
  const auto entries = BuildMenuEntries(*this, arguments[3], CurrentComponentPath());
  if (entries.empty()) {
    return owner.Number(0.0);
  }

  const auto id = found->second.ShowAt(
      Point{static_cast<float>(x.Value()), static_cast<float>(y.Value())},
      std::move(entries)
  );
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowMenuAnchored(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = menu_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == menu_handles_.end()) {
    return owner.Number(0.0);
  }
  auto entries = BuildMenuEntries(*this, arguments[1], CurrentComponentPath());
  if (entries.empty()) {
    return owner.Number(0.0);
  }
  const auto id = found->second.Show(std::move(entries));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostDismissMenu(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto layer_id = arguments[1].ToNumber();
  if (!handle_id.Succeeded() || !layer_id.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(layer_id.Value())) {
    return owner.Boolean(false);
  }
  const auto found = menu_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == menu_handles_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(
      found->second.Dismiss(static_cast<huxerui::LayerId>(layer_id.Value()))
  );
}

quickjs::Value RuntimeImpl::HostUseSnackBar(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto handle = AcquireSnackBarHandle();
  const auto id = next_snack_bar_handle_id_++;
  snack_bar_handles_.emplace(id, std::move(handle));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowSnackBar(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 5) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = snack_bar_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == snack_bar_handles_.end()) {
    return owner.Number(0.0);
  }
  const auto message = arguments[1].ToString();
  if (!message.Succeeded() || message.Value().empty()) {
    return owner.Number(0.0);
  }
  std::string action;
  if (arguments[2].IsString()) {
    const auto text = arguments[2].ToString();
    if (text.Succeeded()) {
      action = text.Value();
    }
  }
  std::function<void()> on_action;
  if (arguments[3].IsFunction()) {
    const auto callback = arguments[3];
    const auto component = CurrentComponentPath();
    on_action = [impl = this, callback, component]() {
      impl->InvokeHandler(callback, {}, DiagnosticPhase::Event, component);
    };
  }
  huxerui::SnackBarOptions options;
  if (arguments[4].IsNumber()) {
    const auto duration = arguments[4].ToNumber();
    if (duration.Succeeded() && std::isfinite(duration.Value())) {
      if (duration.Value() < 0.0) {
        options.duration = std::nullopt;
      } else {
        options.duration = duration.Value();
      }
    }
  }
  huxerui::LayerId id = 0;
  if (!action.empty() && on_action) {
    id = found->second.Show(message.Value(), action, std::move(on_action), options);
  } else {
    id = found->second.Show(message.Value(), options);
  }
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostDismissSnackBar(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto layer_id = arguments[1].ToNumber();
  if (!handle_id.Succeeded() || !layer_id.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(layer_id.Value())) {
    return owner.Boolean(false);
  }
  const auto found = snack_bar_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == snack_bar_handles_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(
      found->second.Dismiss(static_cast<huxerui::LayerId>(layer_id.Value()))
  );
}

quickjs::Value RuntimeImpl::HostUseBottomSheet(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto handle = AcquireBottomSheetHandle();
  const auto id = next_bottom_sheet_handle_id_++;
  bottom_sheet_handles_.emplace(id, std::move(handle));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowBottomSheet(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 3) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = bottom_sheet_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == bottom_sheet_handles_.end() || !arguments[1].IsFunction()) {
    return owner.Number(0.0);
  }

  const auto& options = arguments[2];
  huxerui::BottomSheetOptions sheet_options;
  if (const auto value = BoolProperty(options, "dismissOnOutsidePress"); value) {
    sheet_options.dismiss_on_outside_press = *value;
  }
  if (const auto value = BoolProperty(options, "dismissOnCancel"); value) {
    sheet_options.dismiss_on_cancel = *value;
  }

  const auto render = arguments[1];
  const auto component = CurrentComponentPath();
  auto content = [impl = this, render, component]() -> View {
    const auto rendered = render.Call();
    if (!rendered.Succeeded()) {
      impl->ReportError(
          DiagnosticPhase::Render,
          "bottom sheet content",
          rendered.Error(),
          component
      );
      return impl->ErrorView(rendered.Error().Message());
    }
    return impl->RenderValue(rendered.Value());
  };
  const auto id = found->second.Show(std::move(content), std::move(sheet_options));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostDismissBottomSheet(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto layer_id = arguments[1].ToNumber();
  if (!handle_id.Succeeded() || !layer_id.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(layer_id.Value())) {
    return owner.Boolean(false);
  }
  const auto found = bottom_sheet_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == bottom_sheet_handles_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(
      found->second.Dismiss(static_cast<huxerui::LayerId>(layer_id.Value()))
  );
}

quickjs::Value RuntimeImpl::HostUsePopup(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  (void)arguments;
  auto handle = AcquirePopupHandle();
  const auto id = next_popup_handle_id_++;
  popup_handles_.emplace(id, std::move(handle));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostShowPopup(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 3) {
    return owner.Number(0.0);
  }
  const auto handle_id = arguments[0].ToNumber();
  if (!handle_id.Succeeded() || !std::isfinite(handle_id.Value())) {
    return owner.Number(0.0);
  }
  const auto found = popup_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == popup_handles_.end() || !arguments[1].IsFunction()) {
    return owner.Number(0.0);
  }

  const auto& options = arguments[2];
  huxerui::PopupOptions popup_options;
  if (const auto value = BoolProperty(options, "dismissOnOutsidePress"); value) {
    popup_options.dismiss_on_outside_press = *value;
  }
  if (const auto value = BoolProperty(options, "dismissOnCancel"); value) {
    popup_options.dismiss_on_cancel = *value;
  }
  if (const auto value = BoolProperty(options, "trapFocus"); value) {
    popup_options.trap_focus = *value;
  }
  if (const auto value = BoolProperty(options, "retainAnchorFocus"); value) {
    popup_options.retain_anchor_focus = *value;
  }

  const auto render = arguments[1];
  const auto component = CurrentComponentPath();
  auto content = [impl = this, render, component]() -> View {
    const auto rendered = render.Call();
    if (!rendered.Succeeded()) {
      impl->ReportError(
          DiagnosticPhase::Render,
          "popup content",
          rendered.Error(),
          component
      );
      return impl->ErrorView(rendered.Error().Message());
    }
    return impl->RenderValue(rendered.Value());
  };
  const auto id = found->second.Show(std::move(content), std::move(popup_options));
  return owner.Number(static_cast<double>(id));
}

quickjs::Value RuntimeImpl::HostDismissPopup(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 2) {
    return owner.Boolean(false);
  }
  const auto handle_id = arguments[0].ToNumber();
  const auto layer_id = arguments[1].ToNumber();
  if (!handle_id.Succeeded() || !layer_id.Succeeded()
      || !std::isfinite(handle_id.Value()) || !std::isfinite(layer_id.Value())) {
    return owner.Boolean(false);
  }
  const auto found = popup_handles_.find(static_cast<std::uint64_t>(handle_id.Value()));
  if (found == popup_handles_.end()) {
    return owner.Boolean(false);
  }
  return owner.Boolean(
      found->second.Dismiss(static_cast<huxerui::LayerId>(layer_id.Value()))
  );
}

quickjs::Value RuntimeImpl::HostScrollMetrics(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.empty()) {
    return owner.Undefined();
  }
  const auto id = arguments[0].ToNumber();
  if (!id.Succeeded() || !std::isfinite(id.Value())) {
    return owner.Undefined();
  }
  const auto found = scroll_controllers_.find(static_cast<std::uint64_t>(id.Value()));
  if (found == scroll_controllers_.end()) {
    return owner.Undefined();
  }
  auto object = owner.NewObject();
  if (!object.Succeeded()) {
    return owner.Undefined();
  }
  const auto& controller = found->second;
  auto set = [&](std::string_view name, double value) {
    static_cast<void>(object.Value().SetProperty(name, owner.Number(value)));
  };
  set("offset", static_cast<double>(controller.Offset()));
  set("maxOffset", static_cast<double>(controller.MaxOffset()));
  set("viewportExtent", static_cast<double>(controller.ViewportExtent()));
  set("contentExtent", static_cast<double>(controller.ContentExtent()));
  static_cast<void>(
      object.Value().SetProperty("connected", owner.Boolean(controller.IsConnected()))
  );
  return object.Value();
}

void RuntimeImpl::CancelTimers() noexcept {
  for (auto& [id, timer] : timers_) {
    (void)id;
    timer->cancelled = true;
    timer->handle.Cancel();
  }
  timers_.clear();
}

void RuntimeImpl::PrepareSourceMap(
    std::string_view source,
    std::string_view filename,
    const std::filesystem::path& source_base
) {
  source_map_ = SourceMap::FromBundle(source);
  source_map_module_ = std::string(filename);
  source_map_base_ = source_base;
}

void RuntimeImpl::RestoreSourceMap(
    std::optional<SourceMap> map,
    std::string filename,
    std::filesystem::path source_base
) {
  source_map_ = std::move(map);
  source_map_module_ = std::move(filename);
  source_map_base_ = std::move(source_base);
}

quickjs::Result<void> RuntimeImpl::Load(std::string_view source, std::string_view filename) {
  return LoadInternal(source, filename, {});
}

quickjs::Result<void> RuntimeImpl::LoadInternal(
    std::string_view source,
    std::string_view filename,
    const std::filesystem::path& source_base
) {
  auto previous_map = std::move(source_map_);
  auto previous_module = std::move(source_map_module_);
  auto previous_base = std::move(source_map_base_);
  PrepareSourceMap(source, filename, source_base);

  const auto result = context_.EvaluateModule(source, filename, MakeEvaluateOptions(options));
  if (!result.Succeeded()) {
    ReportError(DiagnosticPhase::Load, "module evaluation", result.Error());
    RestoreSourceMap(
        std::move(previous_map),
        std::move(previous_module),
        std::move(previous_base)
    );
    return quickjs::Result<void>::Failure(result.Error());
  }
  loaded = true;
  return quickjs::Result<void>::Success();
}

quickjs::Result<void> RuntimeImpl::LoadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    PrepareSourceMap({}, path.filename().string(), path.parent_path());
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "Cannot open TSX bundle: " + path.string(),
        path.string(),
    };
    ReportError(DiagnosticPhase::Load, "bundle", error);
    return quickjs::Result<void>::Failure(error);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return LoadInternal(buffer.str(), path.filename().string(), path.parent_path());
}

quickjs::Result<void> RuntimeImpl::Reload(std::string_view source, std::string_view filename) {
  return ReloadInternal(source, filename, {});
}

quickjs::Result<void> RuntimeImpl::ReloadInternal(
    std::string_view source,
    std::string_view filename,
    const std::filesystem::path& source_base
) {
  auto previous_map = std::move(source_map_);
  auto previous_module = std::move(source_map_module_);
  auto previous_base = std::move(source_map_base_);
  PrepareSourceMap(source, filename, source_base);

  const auto previous_root = context_.Global("__huxerui_root");
  const auto result = context_.EvaluateModule(source, filename, MakeEvaluateOptions(options));
  if (!result.Succeeded()) {
    ReportError(DiagnosticPhase::Reload, "module evaluation", result.Error());
    RestoreSourceMap(
        std::move(previous_map),
        std::move(previous_module),
        std::move(previous_base)
    );
    if (previous_root.Succeeded()) {
      static_cast<void>(context_.SetGlobal("__huxerui_root", previous_root.Value()));
    }
    return quickjs::Result<void>::Failure(result.Error());
  }

  const auto next_root = context_.Global("__huxerui_root");
  if (!next_root.Succeeded() || next_root.Value().IsUndefined() || next_root.Value().IsNull()) {
    if (previous_root.Succeeded()) {
      static_cast<void>(context_.SetGlobal("__huxerui_root", previous_root.Value()));
    }
    RestoreSourceMap(
        std::move(previous_map),
        std::move(previous_module),
        std::move(previous_base)
    );
    ReportMessage(DiagnosticPhase::Reload, "mount", "bundle evaluated but did not call mount()");
    return quickjs::Result<void>::Failure(quickjs::Error{
        quickjs::ErrorCode::Exception,
        "TSX reload bundle did not call mount()",
        std::string(filename),
    });
  }

  if (previous_root.Succeeded()) {
    const auto same = next_root.Value().StrictEquals(previous_root.Value());
    if (same.Succeeded() && same.Value()) {
      RestoreSourceMap(
          std::move(previous_map),
          std::move(previous_module),
          std::move(previous_base)
      );
      ReportMessage(
          DiagnosticPhase::Reload,
          "mount",
          "bundle evaluated but did not replace the mounted root"
      );
      return quickjs::Result<void>::Failure(quickjs::Error{
          quickjs::ErrorCode::Exception,
          "TSX reload bundle did not replace __huxerui_root; call mount() again",
          std::string(filename),
      });
    }
  }

  revision += 1;
  if (root_revision.IsValid()) {
    root_revision = revision;
  }
  loaded = true;
  return quickjs::Result<void>::Success();
}

quickjs::Result<void> RuntimeImpl::ReloadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    PrepareSourceMap({}, RevisionName(path.filename().string(), revision + 1), path.parent_path());
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "Cannot open TSX bundle: " + path.string(),
        path.string(),
    };
    ReportError(DiagnosticPhase::Reload, "bundle", error);
    return quickjs::Result<void>::Failure(error);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return ReloadInternal(
      buffer.str(),
      RevisionName(path.filename().string(), revision + 1),
      path.parent_path()
  );
}

quickjs::Result<void> RuntimeImpl::ReloadConfiguredBundle() {
  if (options.bundle.empty()) {
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "RuntimeOptions::bundle is empty",
    };
    ReportError(DiagnosticPhase::Reload, "bundle", error);
    return quickjs::Result<void>::Failure(error);
  }
  return ReloadInternal(
      options.bundle,
      RevisionName(options.entry_name, revision + 1),
      options.module_root
  );
}

quickjs::Result<void> RuntimeImpl::LoadConfiguredBundle() {
  if (options.bundle.empty()) {
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "RuntimeOptions::bundle is empty",
    };
    ReportError(DiagnosticPhase::Load, "bundle", error);
    return quickjs::Result<void>::Failure(error);
  }
  return LoadInternal(options.bundle, options.entry_name, options.module_root);
}

quickjs::Result<quickjs::Value> RuntimeImpl::Evaluate(std::string_view source, std::string_view filename) {
  const auto result = context_.Evaluate(source, filename, MakeEvaluateOptions(options));
  if (!result.Succeeded()) {
    ReportError(DiagnosticPhase::Evaluation, "evaluate", result.Error());
  }
  return result;
}

quickjs::Result<quickjs::Value> RuntimeImpl::Invoke(std::string_view global) {
  const auto function = context_.Global(global);
  if (!function.Succeeded()) {
    ReportError(DiagnosticPhase::Evaluation, "invoke", function.Error());
    return quickjs::Result<quickjs::Value>::Failure(function.Error());
  }
  if (!function.Value().IsFunction()) {
    const quickjs::Error error{
        quickjs::ErrorCode::Type,
        "Global '" + std::string(global) + "' is not a function",
    };
    ReportError(DiagnosticPhase::Evaluation, "invoke", error);
    return quickjs::Result<quickjs::Value>::Failure(error);
  }
  OperationBudgetScope budget_scope(*budget, options.operation_time_limit);
  const auto result = function.Value().Call();
  if (!result.Succeeded()) {
    ReportError(DiagnosticPhase::Evaluation, "invoke " + std::string(global), result.Error());
  }
  return result;
}

void RuntimeImpl::RegisterFunction(
    std::string name, quickjs::NativeFunction function, std::size_t arity
) {
  auto result = context_.SetGlobalFunction(name, std::move(function), arity);
  if (!result.Succeeded()) {
    throw std::runtime_error(
        "huxerui::tsx could not install '" + name + "': " + result.Error().Message()
    );
  }
}

void RuntimeImpl::SetModuleLoader(quickjs::ModuleLoader loader) {
  user_loader_ = std::move(loader);
  InstallModuleLoader();
}

void RuntimeImpl::AppendChildren(const quickjs::Value& children, std::vector<View>& output) {
  if (children.IsUndefined() || children.IsNull() || children.IsBoolean()) {
    return;
  }
  if (children.IsArray()) {
    const auto length = children.Length();
    if (!length.Succeeded()) {
      return;
    }
    for (std::int64_t index = 0; index < length.Value(); ++index) {
      const auto element = children.GetElement(static_cast<std::uint32_t>(index));
      if (element.Succeeded()) {
        AppendChildren(element.Value(), output);
      }
    }
    return;
  }
  if (children.IsString() || children.IsNumber()) {
    output.push_back(Text(TextContent(children)));
    return;
  }
  if (!children.IsObject()) {
    return;
  }

  const auto type = Property(children, "type");
  if (type && type->IsString()) {
    const auto tag = type->ToString();
    if (tag.Succeeded() && tag.Value() == "#fragment") {
      if (const auto props = Property(children, "props")) {
        AppendChildren(
            Property(*props, "children").value_or(context_.Undefined()),
            output
        );
      }
      return;
    }
  }

  View view = RenderValue(children);
  if (static_cast<bool>(view)) {
    output.push_back(std::move(view));
  }
}

std::vector<View> RuntimeImpl::RenderChildren(const quickjs::Value& children) {
  std::vector<View> output;
  AppendChildren(children, output);
  return output;
}

View RuntimeImpl::RenderValue(const quickjs::Value& value) {
  if (value.IsUndefined() || value.IsNull() || value.IsBoolean()) {
    return View{};
  }
  if (value.IsString() || value.IsNumber()) {
    return Text(TextContent(value));
  }
  if (value.IsArray()) {
    auto children = RenderChildren(value);
    if (children.empty()) {
      return View{};
    }
    if (children.size() == 1) {
      return std::move(children.front());
    }
    // HuxerUI has no fragment view; a component returning siblings is stacked.
    return Column(std::move(children));
  }
  if (!value.IsObject()) {
    return View{};
  }

  const auto type = Property(value, "type");
  if (!type || type->IsUndefined() || type->IsNull()) {
    return View{};
  }

  if (type->IsFunction()) {
    return RenderComponent(value, *type);
  }

  if (!type->IsString()) {
    return View{};
  }
  const auto tag = type->ToString();
  if (!tag.Succeeded()) {
    return View{};
  }
  const auto props = Property(value, "props").value_or(context_.Undefined());

  if (tag.Value() == "#fragment") {
    auto children = RenderChildren(
        Property(props, "children").value_or(context_.Undefined())
    );
    if (children.empty()) {
      return View{};
    }
    if (children.size() == 1) {
      return std::move(children.front());
    }
    return Column(std::move(children));
  }

  View view = BuildElement(*this, tag.Value(), props);
  view = ApplyCommonProps(*this, std::move(view), props);
  ApplyKey(view, value);
  return view;
}

View RuntimeImpl::RenderComponent(const quickjs::Value& node, const quickjs::Value& function) {
  const quickjs::Value props = Property(node, "props").value_or(context_.Undefined());
  const std::string name = FunctionName(function);
  std::weak_ptr<ComponentInstance> parent;
  if (const auto* frame = current_frame_; frame != nullptr && frame->instance != nullptr) {
    parent = frame->owner;
  }
  const auto component_key = NextComponentKey(function);
  auto* self = this;

  View scope = Scope([self, function, props, name, parent]() -> View {
    auto candidate = std::make_shared<ComponentInstance>();
    candidate->id = self->AllocateInstanceId();
    candidate->name = name;
    candidate->function = function;
    candidate->parent = parent;
    auto holder = AcquireStateCell<std::shared_ptr<ComponentInstance>>(std::move(candidate));
    auto instance = holder.Get();
    const auto same_function = instance->function.StrictEquals(function);
    if (!same_function.Succeeded() || !same_function.Value()) {
      auto replacement = std::make_shared<ComponentInstance>();
      replacement->id = self->AllocateInstanceId();
      replacement->name = name;
      replacement->function = function;
      replacement->parent = parent;
      holder = replacement;
      instance = std::move(replacement);
    }

    RenderFrame frame{instance, instance.get(), 0};
    CurrentFrameGuard frame_guard(*self, frame);

    OperationBudgetScope budget_scope(*self->budget, self->options.operation_time_limit);
    const quickjs::Value arguments[]{props};
    const auto result = function.Call(arguments);
    if (!result.Succeeded()) {
      self->ReportError(
          DiagnosticPhase::Render,
          "component " + name,
          result.Error(),
          self->ComponentPath(*instance)
      );
      return self->ErrorView(result.Error().Message());
    }
    return self->RenderValue(result.Value());
  });

  scope = std::move(scope).Key(component_key);
  ApplyKey(scope, node);
  return scope;
}

View RuntimeImpl::Root() {
  if (!loaded) {
    if (options.bundle.empty()) {
      const char* message =
          "huxerui::tsx::Runtime::Root() requires a loaded bundle; call Load(), LoadFile(), or LoadConfiguredBundle()";
      ReportMessage(DiagnosticPhase::Render, "root", message);
      throw std::logic_error(message);
    }
    const auto result = LoadConfiguredBundle();
    if (!result.Succeeded()) {
      throw std::runtime_error(
          "huxerui::tsx could not load RuntimeOptions::bundle: " + result.Error().Message()
      );
    }
  }
  auto* self = this;
  return Scope([self]() -> View {
    self->root_revision = AcquireStateCell<std::uint64_t>(self->revision);
    const std::uint64_t current_revision = self->root_revision.Get();

    const auto root = self->context_.Global("__huxerui_root");
    if (!root.Succeeded()) {
      self->ReportError(DiagnosticPhase::Render, "root", root.Error());
      throw std::runtime_error(
          "huxerui::tsx could not read the mounted root: " + root.Error().Message()
      );
    }
    const auto value = root.Value();
    if (value.IsUndefined() || value.IsNull()) {
      const char* message =
          "huxerui::tsx has no mounted root; call mount(<App />) from the entry module";
      self->ReportMessage(DiagnosticPhase::Render, "root", message);
      throw std::logic_error(message);
    }

    View content = Scope([self, value]() -> View {
      self->root_component_occurrences_.clear();
      return self->RenderValue(value);
    });
    return std::move(content).Key(current_revision);
  });
}

} // namespace huxerui::tsx::detail

namespace huxerui::tsx {

Runtime::Runtime(RuntimeOptions options) : impl_(std::make_unique<detail::RuntimeImpl>(std::move(options))) {}

Runtime::~Runtime() = default;

Runtime::Runtime(Runtime&&) noexcept = default;

Runtime& Runtime::operator=(Runtime&&) noexcept = default;

quickjs::Result<void> Runtime::Load(std::string_view source, std::string_view filename) {
  return impl_->Load(source, filename);
}

quickjs::Result<void> Runtime::LoadFile(const std::filesystem::path& path) {
  return impl_->LoadFile(path);
}

quickjs::Result<void> Runtime::LoadConfiguredBundle() {
  return impl_->LoadConfiguredBundle();
}

quickjs::Result<void> Runtime::Reload(std::string_view source, std::string_view filename) {
  return impl_->Reload(source, filename);
}

quickjs::Result<void> Runtime::ReloadFile(const std::filesystem::path& path) {
  return impl_->ReloadFile(path);
}

quickjs::Result<void> Runtime::ReloadConfiguredBundle() {
  return impl_->ReloadConfiguredBundle();
}

View Runtime::Root() {
  return impl_->Root();
}

quickjs::Context& Runtime::Context() noexcept {
  return impl_->Context();
}

const quickjs::Context& Runtime::Context() const noexcept {
  return impl_->Context();
}

std::optional<Diagnostic> Runtime::LastDiagnostic() const {
  return impl_->LastDiagnostic();
}

void Runtime::RegisterFunction(std::string name, quickjs::NativeFunction function, std::size_t arity) {
  impl_->RegisterFunction(std::move(name), std::move(function), arity);
}

void Runtime::SetModuleLoader(quickjs::ModuleLoader loader) {
  impl_->SetModuleLoader(std::move(loader));
}

quickjs::Result<quickjs::Value> Runtime::Evaluate(std::string_view source, std::string_view filename) {
  return impl_->Evaluate(source, filename);
}

quickjs::Result<quickjs::Value> Runtime::Invoke(std::string_view global) {
  return impl_->Invoke(global);
}

View TsxRoot(Runtime& runtime) {
  return runtime.Root();
}

} // namespace huxerui::tsx
