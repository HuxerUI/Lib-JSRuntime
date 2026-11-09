#include <huxerui/huxerui.h>
#include <huxerui/task.h>
#include <huxerui/tsx.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#ifndef HUXERUI_TSX_BUNDLE_PATH
#define HUXERUI_TSX_BUNDLE_PATH "app.js"
#endif

namespace {

using namespace std::chrono_literals;
using namespace huxerui;

std::filesystem::path BundlePath() {
  if (const char* override_path = std::getenv("HUXERUI_TSX_BUNDLE");
      override_path != nullptr && *override_path != '\0') {
    return override_path;
  }
  return HUXERUI_TSX_BUNDLE_PATH;
}

void Log(tsx::LogLevel level, std::string_view message) {
  const char* name = "info";
  if (level == tsx::LogLevel::Debug) {
    name = "debug";
  } else if (level == tsx::LogLevel::Warn) {
    name = "warn";
  } else if (level == tsx::LogLevel::Error) {
    name = "error";
  }
  std::fprintf(stderr, "[tsx:%s] %.*s\n", name, static_cast<int>(message.size()), message.data());
}

void Error(std::string_view message) {
  std::fprintf(stderr, "[tsx:error] %.*s\n", static_cast<int>(message.size()), message.data());
}

tsx::Runtime& Runtime() {
  static tsx::Runtime runtime(tsx::RuntimeOptions{
      .entry_name = "app.js",
      .on_log = Log,
      .on_error = Error,
  });
  static const bool loaded = [] {
    const auto result = runtime.LoadFile(BundlePath());
    if (!result.Succeeded()) {
      Error(result.Error().Message());
    }
    return true;
  }();
  static_cast<void>(loaded);
  return runtime;
}

Task<void> WatchBundle() {
  const auto path = BundlePath();
  std::error_code error;
  auto stamp = std::filesystem::last_write_time(path, error);
  while (true) {
    co_await Delay(250ms);
    const auto next = std::filesystem::last_write_time(path, error);
    if (error) {
      error.clear();
      continue;
    }
    if (next == stamp) {
      continue;
    }
    stamp = next;
    const auto result = Runtime().ReloadFile(path);
    if (result.Succeeded()) {
      std::fprintf(stderr, "[tsx] reloaded %s\n", path.string().c_str());
    } else {
      Error(result.Error().Message());
    }
  }
}

View App() {
  return Scope([]() -> View {
    Lifecycle([tasks = UseTaskScope()]() -> std::function<void()> {
      const auto handle = tasks.Launch(WatchBundle);
      return [handle] { handle.Cancel(); };
    });
    return Runtime().Root();
  });
}

} // namespace

const Application application{
    App,
    {
        .window = {
            .title = "HuxerUI TSX Showcase",
            .initial_size = {960.0F, 680.0F},
        },
    },
};

int main() {
  return RunApplication();
}
