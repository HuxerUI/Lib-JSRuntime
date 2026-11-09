#pragma once

#include <huxerui/lifecycle.h>
#include <huxerui/modifier.h>
#include <huxerui/presentation.h>
#include <huxerui/scroll.h>
#include <huxerui/quickjs.h>
#include <huxerui/state.h>
#include <huxerui/tsx.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace huxerui::tsx::detail {

struct TimerRecord;

/// Shared deadline checked by the QuickJS interrupt handler. It is kept
/// separate from RuntimeImpl so the engine callback remains valid during
/// construction and teardown.
struct ExecutionBudget {
  bool active = false;
  std::chrono::steady_clock::time_point deadline{};
  std::function<bool()> user_interrupt;

  [[nodiscard]] bool Expired() const {
    return active && std::chrono::steady_clock::now() >= deadline;
  }
};

/// One composition-owned JavaScript value. HuxerUI state cells keep the handle
/// alive for as long as the component scope survives.
struct JsSlot {
  quickjs::Value value;
};

/// State owned by one mounted TSX function component.
struct ComponentInstance {
  std::uint64_t id = 0;
  std::string name;
  quickjs::Value function;
  std::weak_ptr<ComponentInstance> parent;
  std::vector<State<JsSlot>> states;
  std::vector<std::uint64_t> handles;
};

/// Lifecycle dependency that compares JavaScript values with `===`.
struct JsDeps {
  std::vector<quickjs::Value> values;
  std::uint64_t always_run = 0;

  bool operator==(const JsDeps& other) const;
};

class RuntimeImpl {
public:
  explicit RuntimeImpl(RuntimeOptions options);
  ~RuntimeImpl();

  RuntimeImpl(const RuntimeImpl&) = delete;
  RuntimeImpl& operator=(const RuntimeImpl&) = delete;

  quickjs::Result<void> Load(std::string_view source, std::string_view filename);
  quickjs::Result<void> LoadFile(const std::filesystem::path& path);
  quickjs::Result<void> LoadConfiguredBundle();

  quickjs::Result<void> Reload(std::string_view source, std::string_view filename);
  quickjs::Result<void> ReloadFile(const std::filesystem::path& path);
  quickjs::Result<void> ReloadConfiguredBundle();

  [[nodiscard]] View Root();
  [[nodiscard]] quickjs::Context& Context() noexcept { return context_; }

  void RegisterFunction(std::string name, quickjs::NativeFunction function, std::size_t arity);
  void SetModuleLoader(quickjs::ModuleLoader loader);
  quickjs::Result<quickjs::Value> Evaluate(std::string_view source, std::string_view filename);
  quickjs::Result<quickjs::Value> Invoke(std::string_view global);

  void ReportError(
      DiagnosticPhase phase,
      std::string_view operation,
      const quickjs::Error& error,
      std::string_view component_stack = {}
  );
  void ReportMessage(
      DiagnosticPhase phase,
      std::string_view operation,
      std::string_view message,
      std::string_view component_stack = {}
  );
  void ReportDiagnostic(Diagnostic diagnostic);
  void Log(LogLevel level, std::string_view message);
  void InvokeHandler(
      const quickjs::Value& function,
      std::span<const quickjs::Value> arguments,
      DiagnosticPhase phase = DiagnosticPhase::Event,
      std::string_view component_stack = {}
  );
  [[nodiscard]] bool InvokeDecisionHandler(
      const quickjs::Value& function,
      std::span<const quickjs::Value> arguments,
      DiagnosticPhase phase = DiagnosticPhase::Event,
      std::string_view component_stack = {}
  );

  [[nodiscard]] View ApplyFocusRef(View view, const quickjs::Value& props);
  [[nodiscard]] std::optional<huxerui::ScrollController> FindScrollController(
      const quickjs::Value& ref
  ) const;
  [[nodiscard]] View ApplyMenuAnchor(View view, const quickjs::Value& props);
  [[nodiscard]] View ApplyPopupAnchor(View view, const quickjs::Value& props);
  [[nodiscard]] std::uint64_t NextComponentKey(const quickjs::Value& function);

  /// @return One component's ancestry, outermost first, joined with " > ".
  [[nodiscard]] std::string ComponentPath(const ComponentInstance& instance) const;
  /// @return The component currently rendering or executing an effect/event.
  [[nodiscard]] std::string CurrentComponentPath() const;
  [[nodiscard]] View ErrorView(std::string_view message) const;
  [[nodiscard]] const std::string& ActiveComponentPath() const { return active_component_path_; }
  void SetActiveComponentPath(std::string path) { active_component_path_ = std::move(path); }
  [[nodiscard]] std::optional<Diagnostic> LastDiagnostic() const { return last_diagnostic_; }

  [[nodiscard]] std::uint64_t AllocateInstanceId() noexcept { return next_instance_id++; }

  // Render entry points used by the element builders.
  [[nodiscard]] View RenderValue(const quickjs::Value& value);
  void AppendChildren(const quickjs::Value& children, std::vector<View>& output);
  [[nodiscard]] std::vector<View> RenderChildren(const quickjs::Value& children);

  struct RenderFrame {
    std::shared_ptr<ComponentInstance> owner;
    ComponentInstance* instance = nullptr;
    std::size_t next_hook = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> component_occurrences;
  };

  RenderFrame* CurrentFrame() const noexcept { return current_frame_; }
  void SetCurrentFrame(RenderFrame* frame) noexcept { current_frame_ = frame; }

  RuntimeOptions options;
  std::shared_ptr<ExecutionBudget> budget;
  quickjs::Runtime engine;
  quickjs::Context context_;

  std::unordered_map<std::uint64_t, std::pair<std::weak_ptr<ComponentInstance>, std::size_t>> handles;
  std::uint64_t next_handle = 1;
  std::uint64_t next_instance_id = 1;
  std::uint64_t revision = 0;
  State<std::uint64_t> root_revision;
  bool loaded = false;

private:
  void InstallHostFunctions();
  void InstallModuleLoader();

  [[nodiscard]] View RenderComponent(const quickjs::Value& node, const quickjs::Value& function);
  [[nodiscard]] quickjs::Value HostUseState(
      quickjs::Context& owner, std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostStateValue(
      quickjs::Context& owner, std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostSetState(
      quickjs::Context& owner, std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseRef(
      quickjs::Context& owner, std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseEffect(
      quickjs::Context& owner, std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostSetTimer(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments,
      bool repeating
  );
  [[nodiscard]] quickjs::Value HostClearTimer(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseFocusRequester(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostRequestFocus(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseScrollController(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseToast(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseDialog(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowDialog(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowDialogContent(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostDismissDialog(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseMenu(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowMenu(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowMenuAnchored(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostDismissMenu(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseSnackBar(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowSnackBar(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostDismissSnackBar(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUseBottomSheet(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowBottomSheet(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostDismissBottomSheet(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostUsePopup(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowPopup(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostDismissPopup(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostShowToast(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostDismissToast(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostScrollTo(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostScrollBy(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostScrollToItem(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostScrollOffset(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  [[nodiscard]] quickjs::Value HostScrollMetrics(
      quickjs::Context& owner,
      std::span<const quickjs::Value> arguments
  );
  void ScheduleTimer(const std::shared_ptr<TimerRecord>& timer);
  void FireTimer(const std::shared_ptr<TimerRecord>& timer);
  void CancelTimers() noexcept;

  quickjs::ModuleLoader user_loader_;
  RenderFrame* current_frame_ = nullptr;
  std::uint64_t next_effect_dependency_ = 1;
  std::string active_component_path_;
  std::unordered_map<std::uint64_t, std::uint32_t> root_component_occurrences_;
  std::optional<Diagnostic> last_diagnostic_;
  std::uint64_t next_timer_id_ = 1;
  std::unordered_map<std::uint64_t, std::shared_ptr<TimerRecord>> timers_;
  std::uint64_t next_focus_requester_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::FocusRequester> focus_requesters_;
  std::uint64_t next_scroll_controller_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::ScrollController> scroll_controllers_;
  std::uint64_t next_toast_handle_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::ToastHandle> toast_handles_;
  std::uint64_t next_dialog_handle_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::DialogHandle> dialog_handles_;
  std::uint64_t next_menu_handle_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::MenuHandle> menu_handles_;
  std::uint64_t next_snack_bar_handle_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::SnackBarHandle> snack_bar_handles_;
  std::uint64_t next_bottom_sheet_handle_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::BottomSheetHandle> bottom_sheet_handles_;
  std::uint64_t next_popup_handle_id_ = 1;
  std::unordered_map<std::uint64_t, huxerui::PopupHandle> popup_handles_;
};

} // namespace huxerui::tsx::detail
