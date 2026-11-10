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
