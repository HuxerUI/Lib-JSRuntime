#pragma once

#include <huxerui/color.h>
#include <huxerui/geometry.h>
#include <huxerui/quickjs.h>
#include <huxerui/view.h>

#include <optional>
#include <string>
#include <string_view>

#include "internal.h"

namespace huxerui::tsx::detail {

[[nodiscard]] std::optional<quickjs::Value> Property(const quickjs::Value& object, std::string_view name);
[[nodiscard]] bool HasProperty(const quickjs::Value& object, std::string_view name);
[[nodiscard]] std::optional<double> NumberProperty(const quickjs::Value& object, std::string_view name);
[[nodiscard]] std::optional<bool> BoolProperty(const quickjs::Value& object, std::string_view name);
[[nodiscard]] std::optional<std::string> StringProperty(const quickjs::Value& object, std::string_view name);

/// Flattens primitive text children; non-text nodes contribute nothing.
[[nodiscard]] std::string TextContent(const quickjs::Value& value);

[[nodiscard]] std::optional<Color> ToColor(const quickjs::Value& value);
[[nodiscard]] std::optional<EdgeInsets> ToEdgeInsets(const quickjs::Value& value);
[[nodiscard]] std::optional<CornerRadii> ToCornerRadii(const quickjs::Value& value);

/// Applies modifiers shared by every host element and the generic `onClick`
/// handler. Element-specific props are applied by the element itself first.
[[nodiscard]] View ApplyCommonProps(RuntimeImpl& runtime, View view, const quickjs::Value& props);

void ApplyKey(View& view, const quickjs::Value& node);

/// Builds one host element. Unknown tags render an inline diagnostic.
[[nodiscard]] View BuildElement(RuntimeImpl& runtime, std::string_view tag, const quickjs::Value& props);

} // namespace huxerui::tsx::detail
