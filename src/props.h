#pragma once

#include <huxerui/color.h>
#include <huxerui/gesture.h>
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

/// Applies modifiers shared by every host element. Element-specific props are
/// applied by the element itself first.
[[nodiscard]] View ApplyCommonProps(RuntimeImpl& runtime, View view, const quickjs::Value& props);

/// Applies pointer, hover, scroll, keyboard, focus, context-menu, back, and
/// click handlers shared by every host element.
[[nodiscard]] View ApplyEventProps(RuntimeImpl& runtime, View view, const quickjs::Value& props);

/// Builds portable JavaScript event objects for the host-event bridge.
[[nodiscard]] quickjs::Value MakePointerEvent(quickjs::Context& context, const PointerEvent& event);
[[nodiscard]] quickjs::Value MakeHoverEvent(quickjs::Context& context, const HoverEvent& event);
[[nodiscard]] quickjs::Value MakeScrollEvent(quickjs::Context& context, const ScrollInputEvent& event);
[[nodiscard]] quickjs::Value MakeKeyEvent(quickjs::Context& context, const KeyEvent& event);
[[nodiscard]] quickjs::Value MakeContextMenuEvent(quickjs::Context& context, Point position);
[[nodiscard]] quickjs::Value MakeTextEditingEvent(
    quickjs::Context& context,
    const TextEditingValue& value
);
[[nodiscard]] quickjs::Value MakeDragEvent(
    quickjs::Context& context,
    const DragEvent& event
);
[[nodiscard]] quickjs::Value MakeLongPressEvent(
    quickjs::Context& context,
    const LongPressEvent& event
);
[[nodiscard]] quickjs::Value MakeMultiTapEvent(
    quickjs::Context& context,
    const MultiTapEvent& event
);

void ApplyKey(View& view, const quickjs::Value& node);

/// Builds one host element. Unknown tags render an inline diagnostic.
[[nodiscard]] View BuildElement(RuntimeImpl& runtime, std::string_view tag, const quickjs::Value& props);

} // namespace huxerui::tsx::detail
