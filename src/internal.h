#pragma once

#include <huxerui/lifecycle.h>
#include <huxerui/quickjs.h>
#include <huxerui/state.h>
#include <huxerui/tsx.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace huxerui::tsx::detail {

/// One composition-owned JavaScript value. HuxerUI state cells keep the handle
/// alive for as long as the component scope survives.
struct JsSlot {
  quickjs::Value value;
};

/// State owned by one mounted TSX function component.
struct ComponentInstance {
  std::uint64_t id = 0;
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

  void ReportError(std::string_view operation, const quickjs::Error& error);
  void ReportMessage(std::string_view operation, std::string_view message);
  void Log(LogLevel level, std::string_view message);
  void InvokeHandler(const quickjs::Value& function, std::span<const quickjs::Value> arguments);

  [[nodiscard]] std::uint64_t AllocateInstanceId() noexcept { return next_instance_id++; }

  // Render entry points used by the element builders.
  [[nodiscard]] View RenderValue(const quickjs::Value& value);
  void AppendChildren(const quickjs::Value& children, std::vector<View>& output);
  [[nodiscard]] std::vector<View> RenderChildren(const quickjs::Value& children);

  struct RenderFrame {
    std::shared_ptr<ComponentInstance> owner;
    ComponentInstance* instance = nullptr;
    std::size_t next_hook = 0;
  };

  RenderFrame* CurrentFrame() const noexcept { return current_frame_; }

  RuntimeOptions options;
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

  [[nodiscard]] View ErrorView(std::string_view message) const;

  quickjs::ModuleLoader user_loader_;
  RenderFrame* current_frame_ = nullptr;
  std::uint64_t next_effect_dependency_ = 1;
};

} // namespace huxerui::tsx::detail
