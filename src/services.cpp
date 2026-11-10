#include "internal.h"
#include "props.h"

#include <huxerui/app.h>
#include <huxerui/clipboard.h>
#include <huxerui/http.h>
#include <huxerui/task.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace huxerui::tsx::detail {

namespace {

std::string Lowercase(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

std::optional<huxerui::HttpMethod> ParseHttpMethod(std::string_view value) {
  const auto method = Lowercase(value);
  if (method == "get") {
    return huxerui::HttpMethod::Get;
  }
  if (method == "head") {
    return huxerui::HttpMethod::Head;
  }
  if (method == "post") {
    return huxerui::HttpMethod::Post;
  }
  if (method == "put") {
    return huxerui::HttpMethod::Put;
  }
  if (method == "patch") {
    return huxerui::HttpMethod::Patch;
  }
  if (method == "delete") {
    return huxerui::HttpMethod::Delete;
  }
  if (method == "options") {
    return huxerui::HttpMethod::Options;
  }
  return std::nullopt;
}

const char* HttpErrorName(huxerui::HttpErrorCode code) noexcept {
  switch (code) {
    case huxerui::HttpErrorCode::Transport:
      return "transport";
    case huxerui::HttpErrorCode::Timeout:
      return "timeout";
    case huxerui::HttpErrorCode::Unsupported:
      return "unsupported";
  }
  return "transport";
}

void RejectWithString(
    quickjs::Context& context,
    const quickjs::Value& reject,
    std::string_view message
) {
  const auto text = context.NewString(message);
  if (!text.Succeeded()) {
    return;
  }
  const quickjs::Value arguments[]{text.Value()};
  static_cast<void>(reject.Call(arguments));
  static_cast<void>(context.RunJobs());
}

void ResolveWithValue(
    quickjs::Context& context,
    const quickjs::Value& resolve,
    const quickjs::Value& value
) {
  const quickjs::Value arguments[]{value};
  static_cast<void>(resolve.Call(arguments));
  static_cast<void>(context.RunJobs());
}

void RejectWithValue(
    quickjs::Context& context,
    const quickjs::Value& reject,
    const quickjs::Value& value
) {
  const quickjs::Value arguments[]{value};
  static_cast<void>(reject.Call(arguments));
  static_cast<void>(context.RunJobs());
}

} // namespace

quickjs::Value RuntimeImpl::HostClipboardRead(
    quickjs::Context& owner,
    std::span<const quickjs::Value>
) {
  try {
    const auto application = huxerui::UseApplication();
    const auto clipboard = application.Clipboard();
    if (!clipboard || !clipboard->IsAvailable()) {
      return owner.Null();
    }
    const auto text = clipboard->ReadText();
    if (!text) {
      return owner.Null();
    }
    const auto value = owner.NewString(*text);
    return value.Succeeded() ? value.Value() : owner.Null();
  } catch (...) {
    return owner.Null();
  }
}

quickjs::Value RuntimeImpl::HostClipboardWrite(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.empty()) {
    return owner.Boolean(false);
  }
  const auto text = arguments[0].ToString();
  if (!text.Succeeded()) {
    return owner.Boolean(false);
  }
  try {
    const auto application = huxerui::UseApplication();
    const auto clipboard = application.Clipboard();
    if (!clipboard || !clipboard->IsAvailable()) {
      return owner.Boolean(false);
    }
    return owner.Boolean(clipboard->WriteText(text.Value()));
  } catch (...) {
    return owner.Boolean(false);
  }
}

quickjs::Value RuntimeImpl::HostClipboardAvailable(
    quickjs::Context& owner,
    std::span<const quickjs::Value>
) {
  try {
    const auto application = huxerui::UseApplication();
    const auto clipboard = application.Clipboard();
    return owner.Boolean(clipboard && clipboard->IsAvailable());
  } catch (...) {
    return owner.Boolean(false);
  }
}

quickjs::Value RuntimeImpl::HostQuitApplication(
    quickjs::Context& owner,
    std::span<const quickjs::Value>
) {
  try {
    huxerui::UseApplication().Quit();
  } catch (...) {
  }
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostApplicationLifecycle(
    quickjs::Context& owner,
    std::span<const quickjs::Value>
) {
  std::string state = "unknown";
  try {
    switch (huxerui::UseApplication().LifecycleState()) {
      case huxerui::ApplicationLifecycleState::Active:
        state = "active";
        break;
      case huxerui::ApplicationLifecycleState::Inactive:
        state = "inactive";
        break;
      case huxerui::ApplicationLifecycleState::Background:
        state = "background";
        break;
    }
  } catch (...) {
  }
  const auto value = owner.NewString(state);
  return value.Succeeded() ? value.Value() : owner.Undefined();
}

namespace {

std::optional<double> NumberArgument(
    std::span<const quickjs::Value> arguments,
    std::size_t index
) {
  if (index >= arguments.size()) {
    return std::nullopt;
  }
  const auto value = arguments[index].ToNumber();
  if (!value.Succeeded() || !std::isfinite(value.Value())) {
    return std::nullopt;
  }
  return value.Value();
}

std::optional<Color> ColorArgument(
    std::span<const quickjs::Value> arguments,
    std::size_t index
) {
  if (index >= arguments.size()) {
    return std::nullopt;
  }
  return ToColor(arguments[index]);
}

RuntimeImpl::PaintState* PaintStateArgument(
    RuntimeImpl& runtime,
    std::span<const quickjs::Value> arguments
) {
  const auto id = NumberArgument(arguments, 0);
  if (!id || *id < 1.0) {
    return nullptr;
  }
  return runtime.FindPaintState(static_cast<std::uint64_t>(*id));
}

} // namespace

quickjs::Value RuntimeImpl::HostPaintFillRect(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x = NumberArgument(arguments, 1);
  const auto y = NumberArgument(arguments, 2);
  const auto width = NumberArgument(arguments, 3);
  const auto height = NumberArgument(arguments, 4);
  const auto color = ColorArgument(arguments, 5);
  if (!state || !x || !y || !width || !height || !color) {
    return owner.Undefined();
  }
  const auto radius = NumberArgument(arguments, 6).value_or(0.0);
  state->context->DrawRect(
      Rect{
          static_cast<float>(*x),
          static_cast<float>(*y),
          static_cast<float>(*width),
          static_cast<float>(*height),
      },
      *color,
      CornerRadii(static_cast<float>(radius))
  );
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintStrokeRect(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x = NumberArgument(arguments, 1);
  const auto y = NumberArgument(arguments, 2);
  const auto width = NumberArgument(arguments, 3);
  const auto height = NumberArgument(arguments, 4);
  const auto color = ColorArgument(arguments, 5);
  if (!state || !x || !y || !width || !height || !color) {
    return owner.Undefined();
  }
  const auto stroke = NumberArgument(arguments, 6).value_or(1.0);
  const auto radius = NumberArgument(arguments, 7).value_or(0.0);
  state->context->DrawBorder(
      Rect{
          static_cast<float>(*x),
          static_cast<float>(*y),
          static_cast<float>(*width),
          static_cast<float>(*height),
      },
      *color,
      StrokeStyle{.width = static_cast<float>(stroke)},
      CornerRadii(static_cast<float>(radius))
  );
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintLine(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x1 = NumberArgument(arguments, 1);
  const auto y1 = NumberArgument(arguments, 2);
  const auto x2 = NumberArgument(arguments, 3);
  const auto y2 = NumberArgument(arguments, 4);
  const auto color = ColorArgument(arguments, 5);
  if (!state || !x1 || !y1 || !x2 || !y2 || !color) {
    return owner.Undefined();
  }
  const auto stroke = NumberArgument(arguments, 6).value_or(1.0);
  state->context->DrawLine(
      Point{static_cast<float>(*x1), static_cast<float>(*y1)},
      Point{static_cast<float>(*x2), static_cast<float>(*y2)},
      *color,
      StrokeStyle{.width = static_cast<float>(stroke)}
  );
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintCircle(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x = NumberArgument(arguments, 1);
  const auto y = NumberArgument(arguments, 2);
  const auto radius = NumberArgument(arguments, 3);
  const auto color = ColorArgument(arguments, 4);
  if (!state || !x || !y || !radius || !color) {
    return owner.Undefined();
  }
  state->context->DrawCircle(
      Point{static_cast<float>(*x), static_cast<float>(*y)},
      static_cast<float>(*radius),
      *color
  );
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintArc(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x = NumberArgument(arguments, 1);
  const auto y = NumberArgument(arguments, 2);
  const auto radius = NumberArgument(arguments, 3);
  const auto start = NumberArgument(arguments, 4);
  const auto sweep = NumberArgument(arguments, 5);
  const auto color = ColorArgument(arguments, 6);
  if (!state || !x || !y || !radius || !start || !sweep || !color) {
    return owner.Undefined();
  }
  const auto stroke = NumberArgument(arguments, 7).value_or(1.0);
  state->context->DrawArc(
      Point{static_cast<float>(*x), static_cast<float>(*y)},
      static_cast<float>(*radius),
      static_cast<float>(*start),
      static_cast<float>(*sweep),
      *color,
      StrokeStyle{.width = static_cast<float>(stroke)}
  );
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintPushClipRect(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x = NumberArgument(arguments, 1);
  const auto y = NumberArgument(arguments, 2);
  const auto width = NumberArgument(arguments, 3);
  const auto height = NumberArgument(arguments, 4);
  if (!state || !x || !y || !width || !height) {
    return owner.Undefined();
  }
  const auto radius = NumberArgument(arguments, 5).value_or(0.0);
  state->context->PushClip(
      Rect{
          static_cast<float>(*x),
          static_cast<float>(*y),
          static_cast<float>(*width),
          static_cast<float>(*height),
      },
      CornerRadii(static_cast<float>(radius))
  );
  ++state->clips;
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintPopClip(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  if (state && state->clips > 0) {
    state->context->PopClip();
    --state->clips;
  }
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintPushTranslate(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  const auto x = NumberArgument(arguments, 1);
  const auto y = NumberArgument(arguments, 2);
  if (!state || !x || !y) {
    return owner.Undefined();
  }
  Transform2D transform;
  transform.translate_x = static_cast<float>(*x);
  transform.translate_y = static_cast<float>(*y);
  state->context->PushTransform(transform);
  ++state->transforms;
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostPaintPopTransform(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  auto* state = PaintStateArgument(*this, arguments);
  if (state && state->transforms > 0) {
    state->context->PopTransform();
    --state->transforms;
  }
  return owner.Undefined();
}

quickjs::Value RuntimeImpl::HostFetch(
    quickjs::Context& owner,
    std::span<const quickjs::Value> arguments
) {
  if (arguments.size() < 4 || !arguments[2].IsFunction() || !arguments[3].IsFunction()) {
    return owner.Undefined();
  }
  const quickjs::Value resolve = arguments[2];
  const quickjs::Value reject = arguments[3];

  const auto url = arguments[0].IsString() ? arguments[0].ToString() : quickjs::Result<std::string>::Failure(
      quickjs::Error{quickjs::ErrorCode::Type, "fetch() requires a URL string"}
  );
  if (!url.Succeeded()) {
    RejectWithString(owner, reject, url.Error().Message());
    return owner.Undefined();
  }

  huxerui::HttpRequest request;
  request.url = url.Value();

  const auto options = arguments[1].IsObject() ? std::optional<quickjs::Value>(arguments[1])
                                                : std::nullopt;
  if (options) {
    if (const auto method_name = StringProperty(*options, "method")) {
      const auto method = ParseHttpMethod(*method_name);
      if (!method) {
        RejectWithString(owner, reject, "Unsupported HTTP method: " + *method_name);
        return owner.Undefined();
      }
      request.method = *method;
    }
    if (const auto body = StringProperty(*options, "body")) {
      request.body.resize(body->size());
      if (!body->empty()) {
        std::memcpy(request.body.data(), body->data(), body->size());
      }
    }
    if (const auto timeout = NumberProperty(*options, "timeout");
        timeout && std::isfinite(*timeout) && *timeout > 0.0) {
      request.timeout = std::chrono::milliseconds(static_cast<std::int64_t>(*timeout));
    }
    if (const auto headers = Property(*options, "headers");
        headers && headers->IsObject()) {
      const auto names = headers->PropertyNames();
      if (names.Succeeded()) {
        for (const auto& name : names.Value()) {
          const auto value = headers->GetProperty(name);
          if (!value.Succeeded()) {
            continue;
          }
          const auto text = value.Value().ToString();
          if (text.Succeeded()) {
            request.headers.push_back(huxerui::HttpHeader{name, text.Value()});
          }
        }
      }
    }
  }

  try {
    auto tasks = huxerui::UseApplicationTaskScope();
    auto work = [context = &owner,
                 resolve = resolve,
                 reject = reject,
                 request = std::move(request)]() mutable -> huxerui::Task<void> {
      try {
        huxerui::HttpClient http;
        auto result = co_await http.SendAsync(std::move(request));
        if (!result.Succeeded()) {
          auto object = context->NewObject();
          if (!object.Succeeded()) {
            RejectWithString(*context, reject, result.Error().message);
            co_return;
          }
          if (auto text = context->NewString(result.Error().message); text.Succeeded()) {
            static_cast<void>(object.Value().SetProperty("message", text.Value()));
          }
          if (auto text = context->NewString(HttpErrorName(result.Error().code));
              text.Succeeded()) {
            static_cast<void>(object.Value().SetProperty("code", text.Value()));
          }
          RejectWithValue(*context, reject, object.Value());
          co_return;
        }

        auto response = std::move(result).Value();
        auto object = context->NewObject();
        if (!object.Succeeded()) {
          RejectWithString(*context, reject, "Could not allocate HTTP response");
          co_return;
        }
        auto& target = object.Value();
        if (auto text = context->NewString(response.url); text.Succeeded()) {
          static_cast<void>(target.SetProperty("url", text.Value()));
        }
        static_cast<void>(
            target.SetProperty("status", context->Number(response.status_code))
        );
        auto headers = context->NewObject();
        if (headers.Succeeded()) {
          for (const auto& header : response.headers) {
            if (auto text = context->NewString(header.value); text.Succeeded()) {
              static_cast<void>(headers.Value().SetProperty(header.name, text.Value()));
            }
          }
          static_cast<void>(target.SetProperty("headers", headers.Value()));
        }
        std::string body;
        if (!response.body.empty()) {
          body.assign(
              reinterpret_cast<const char*>(response.body.data()),
              response.body.size()
          );
        }
        if (auto text = context->NewString(body); text.Succeeded()) {
          static_cast<void>(target.SetProperty("body", text.Value()));
        }
        static_cast<void>(target.SetProperty(
            "byteLength",
            context->Number(static_cast<double>(response.body.size()))
        ));
        ResolveWithValue(*context, resolve, target);
      } catch (const std::exception& error) {
        RejectWithString(*context, reject, error.what());
      } catch (...) {
        RejectWithString(*context, reject, "HTTP request failed");
      }
    };

    async_tasks_.push_back(tasks.Launch(std::move(work)));
  } catch (const std::exception& error) {
    RejectWithString(owner, reject, error.what());
  } catch (...) {
    RejectWithString(owner, reject, "No active HuxerUI application for fetch()");
  }
  return owner.Undefined();
}

} // namespace huxerui::tsx::detail
