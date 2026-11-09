#include <huxerui/huxerui.h>
#include <huxerui/tsx.h>

#include <cstdio>
#include <string>

#include "app_bundle.h"

namespace {

huxerui::tsx::Runtime& Runtime() {
  static huxerui::tsx::Runtime runtime(huxerui::tsx::RuntimeOptions{
      .entry_name = "App.tsx",
      .bundle = std::string(counter::kBundle),
      .on_error = [](std::string_view message) {
        std::fprintf(stderr, "tsx: %.*s\n", static_cast<int>(message.size()), message.data());
      },
  });
  return runtime;
}

huxerui::View App() {
  return Runtime().Root();
}

} // namespace

const huxerui::Application application{
    App,
    {
        .window = {
            .title = "HuxerUI TSX Counter",
            .initial_size = {480.0F, 360.0F},
        },
    },
};

int main() {
  return huxerui::RunApplication();
}
