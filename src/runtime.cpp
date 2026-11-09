#include "internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <source_location>
#include <typeinfo>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "embedded_js.h"
#include "props.h"

namespace huxerui::tsx::detail {

namespace {

constexpr std::string_view kJsxRuntimeModule =
    "export { jsx, jsxs, Fragment } from \"@huxerui/tsx\";\n";

quickjs::RuntimeOptions MakeEngineOptions(std::size_t memory_limit_bytes) {
  quickjs::RuntimeOptions options;
  options.memory_limit_bytes = memory_limit_bytes;
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

std::string RevisionName(std::string_view name, std::uint64_t revision) {
  const auto dot = name.rfind('.');
  const std::string suffix = ".r" + std::to_string(revision);
  if (dot == std::string_view::npos) {
    return std::string(name) + suffix;
  }
  return std::string(name.substr(0, dot)) + suffix + std::string(name.substr(dot));
}

std::string DescribeError(const quickjs::Error& error) {
  std::string message = error.Message();
  if (!error.Filename().empty()) {
    message += " (";
    message += error.Filename();
    if (error.Line() > 0) {
      message += ":";
      message += std::to_string(error.Line());
      if (error.Column() > 0) {
        message += ":";
        message += std::to_string(error.Column());
      }
    }
    message += ")";
  }
  if (!error.Stack().empty()) {
    message += "\n";
    message += error.Stack();
  }
  return message;
}

} // namespace

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
      engine(MakeEngineOptions(options.memory_limit_bytes)),
      context_(engine) {
  InstallHostFunctions();
  InstallModuleLoader();
}

RuntimeImpl::~RuntimeImpl() = default;

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

void RuntimeImpl::ReportError(std::string_view operation, const quickjs::Error& error) {
  const std::string message = std::string(operation) + ": " + DescribeError(error);
  if (options.on_log) {
    options.on_log(LogLevel::Error, message);
  }
  if (options.on_error) {
    options.on_error(message);
  }
}

void RuntimeImpl::ReportMessage(std::string_view operation, std::string_view message) {
  const std::string full = std::string(operation) + ": " + std::string(message);
  if (options.on_log) {
    options.on_log(LogLevel::Error, full);
  }
  if (options.on_error) {
    options.on_error(full);
  }
}

void RuntimeImpl::Log(LogLevel level, std::string_view message) {
  if (options.on_log) {
    options.on_log(level, message);
  }
}

void RuntimeImpl::InvokeHandler(const quickjs::Value& function, std::span<const quickjs::Value> arguments) {
  if (!function.IsFunction()) {
    return;
  }
  const auto result = function.Call(arguments);
  if (!result.Succeeded()) {
    ReportError("event handler", result.Error());
  }
}

View RuntimeImpl::ErrorView(std::string_view message) const {
  return Text(std::string("TSX error: ") + std::string(message));
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
  huxerui::Lifecycle(
      [impl = this, effect]() -> std::function<void()> {
        const auto result = effect.Call();
        if (!result.Succeeded()) {
          impl->ReportError("useEffect", result.Error());
          return {};
        }
        const auto cleanup = result.Value();
        if (!cleanup.IsFunction()) {
          return {};
        }
        return std::function<void()>([impl, cleanup]() {
          impl->InvokeHandler(cleanup, {});
        });
      },
      std::move(deps)
  );
  return owner.Undefined();
}

quickjs::Result<void> RuntimeImpl::Load(std::string_view source, std::string_view filename) {
  const auto result = context_.EvaluateModule(source, filename);
  if (!result.Succeeded()) {
    ReportError("module evaluation", result.Error());
    return quickjs::Result<void>::Failure(result.Error());
  }
  loaded = true;
  return quickjs::Result<void>::Success();
}

quickjs::Result<void> RuntimeImpl::LoadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "Cannot open TSX bundle: " + path.string(),
        path.string(),
    };
    ReportError("bundle", error);
    return quickjs::Result<void>::Failure(error);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return Load(buffer.str(), path.filename().string());
}

quickjs::Result<void> RuntimeImpl::Reload(std::string_view source, std::string_view filename) {
  const auto previous_root = context_.Global("__huxerui_root");
  const auto result = context_.EvaluateModule(source, filename);
  if (!result.Succeeded()) {
    ReportError("reload", result.Error());
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
    ReportMessage("reload", "bundle evaluated but did not call mount()");
    return quickjs::Result<void>::Failure(quickjs::Error{
        quickjs::ErrorCode::Exception,
        "TSX reload bundle did not call mount()",
        std::string(filename),
    });
  }

  if (previous_root.Succeeded()) {
    const auto same = next_root.Value().StrictEquals(previous_root.Value());
    if (same.Succeeded() && same.Value()) {
      ReportMessage("reload", "bundle evaluated but did not replace the mounted root");
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
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "Cannot open TSX bundle: " + path.string(),
        path.string(),
    };
    ReportError("reload", error);
    return quickjs::Result<void>::Failure(error);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return Reload(buffer.str(), RevisionName(path.filename().string(), revision + 1));
}

quickjs::Result<void> RuntimeImpl::ReloadConfiguredBundle() {
  if (options.bundle.empty()) {
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "RuntimeOptions::bundle is empty",
    };
    ReportError("reload", error);
    return quickjs::Result<void>::Failure(error);
  }
  return Reload(options.bundle, RevisionName(options.entry_name, revision + 1));
}

quickjs::Result<void> RuntimeImpl::LoadConfiguredBundle() {
  if (options.bundle.empty()) {
    const quickjs::Error error{
        quickjs::ErrorCode::Module,
        "RuntimeOptions::bundle is empty",
    };
    ReportError("bundle", error);
    return quickjs::Result<void>::Failure(error);
  }
  return Load(options.bundle, options.entry_name);
}

quickjs::Result<quickjs::Value> RuntimeImpl::Evaluate(std::string_view source, std::string_view filename) {
  return context_.Evaluate(source, filename);
}

quickjs::Result<quickjs::Value> RuntimeImpl::Invoke(std::string_view global) {
  const auto function = context_.Global(global);
  if (!function.Succeeded()) {
    return quickjs::Result<quickjs::Value>::Failure(function.Error());
  }
  if (!function.Value().IsFunction()) {
    return quickjs::Result<quickjs::Value>::Failure(quickjs::Error{
        quickjs::ErrorCode::Type,
        "Global '" + std::string(global) + "' is not a function",
    });
  }
  return function.Value().Call();
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
  auto* self = this;

  View scope = Scope([self, function, props]() -> View {
    auto candidate = std::make_shared<ComponentInstance>();
    candidate->id = self->AllocateInstanceId();
    const auto holder = AcquireStateCell<std::shared_ptr<ComponentInstance>>(std::move(candidate));
    const auto instance = holder.Get();

    RenderFrame frame{instance, instance.get(), 0};
    auto* previous = self->current_frame_;
    self->current_frame_ = &frame;
    const quickjs::Value arguments[]{props};
    const auto result = function.Call(arguments);
    self->current_frame_ = previous;

    if (!result.Succeeded()) {
      self->ReportError("component render", result.Error());
      return self->ErrorView(result.Error().Message());
    }
    return self->RenderValue(result.Value());
  });

  ApplyKey(scope, node);
  return scope;
}

View RuntimeImpl::Root() {
  if (!loaded) {
    if (options.bundle.empty()) {
      throw std::logic_error(
          "huxerui::tsx::Runtime::Root() requires a loaded bundle; call Load(), LoadFile(), or LoadConfiguredBundle()"
      );
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
      throw std::runtime_error(
          "huxerui::tsx could not read the mounted root: " + root.Error().Message()
      );
    }
    const auto value = root.Value();
    if (value.IsUndefined() || value.IsNull()) {
      throw std::logic_error(
          "huxerui::tsx has no mounted root; call mount(<App />) from the entry module"
      );
    }

    View content = Scope([self, value]() -> View {
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
