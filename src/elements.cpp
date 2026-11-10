#include "props.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <huxerui/resource.h>
#include <huxerui/theme.h>
#include <huxerui/text.h>

namespace huxerui::tsx::detail {
namespace {

std::string Lowercase(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

std::string AttributeName(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    if (character == '-' || character == '_') {
      continue;
    }
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

std::optional<ValidationResult> ValidationFromProps(const quickjs::Value& props) {
  const auto validation = Property(props, "validation");
  if (!validation || !validation->IsObject()) {
    return std::nullopt;
  }
  const auto status = StringProperty(*validation, "status");
  if (!status) {
    return std::nullopt;
  }
  const auto name = AttributeName(*status);
  const auto message = StringProperty(*validation, "message").value_or("");
  if (name == "invalid") {
    return ValidationResult::Invalid(message);
  }
  if (name == "valid") {
    return ValidationResult::Valid();
  }
  if (name == "pending") {
    return ValidationResult::Pending(message);
  }
  return ValidationResult::None();
}

std::optional<std::chrono::year_month_day> ParseDate(std::string_view text) {
  int year = 0;
  int month = 0;
  int day = 0;
  const std::string copy(text);
  if (std::sscanf(copy.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
    return std::nullopt;
  }
  const std::chrono::year_month_day date{
      std::chrono::year{year},
      std::chrono::month{static_cast<unsigned int>(month)},
      std::chrono::day{static_cast<unsigned int>(day)},
  };
  if (!date.ok()) {
    return std::nullopt;
  }
  return date;
}

std::string FormatDate(std::chrono::year_month_day date) {
  char buffer[16]{};
  std::snprintf(
      buffer,
      sizeof(buffer),
      "%04d-%02u-%02u",
      static_cast<int>(date.year()),
      static_cast<unsigned int>(date.month()),
      static_cast<unsigned int>(date.day())
  );
  return buffer;
}

std::optional<std::chrono::minutes> ParseTime(std::string_view text) {
  int hours = 0;
  int minutes = 0;
  const std::string copy(text);
  if (std::sscanf(copy.c_str(), "%d:%d", &hours, &minutes) != 2) {
    return std::nullopt;
  }
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    return std::nullopt;
  }
  return std::chrono::minutes{hours * 60 + minutes};
}

std::string FormatTime(std::chrono::minutes value) {
  const auto total = value.count();
  char buffer[8]{};
  std::snprintf(
      buffer,
      sizeof(buffer),
      "%02d:%02d",
      static_cast<int>(total / 60),
      static_cast<int>(total % 60)
  );
  return buffer;
}

std::optional<TextRole> ParseRole(const quickjs::Value& props) {
  const auto name = StringProperty(props, "role");
  if (!name) {
    return std::nullopt;
  }
  const auto role = Lowercase(*name);
  if (role == "body") {
    return TextRole::Body;
  }
  if (role == "label") {
    return TextRole::Label;
  }
  if (role == "title") {
    return TextRole::Title;
  }
  return std::nullopt;
}

std::optional<FontWeight> ParseFontWeight(const quickjs::Value& value) {
  if (value.IsNumber()) {
    const auto number = value.ToNumber();
    if (!number.Succeeded()) {
      return std::nullopt;
    }
    return static_cast<FontWeight>(std::clamp(static_cast<int>(number.Value()), 100, 900));
  }
  const auto text = value.ToString();
  if (!text.Succeeded()) {
    return std::nullopt;
  }
  const auto name = AttributeName(text.Value());
  if (name == "thin") {
    return FontWeight::Thin;
  }
  if (name == "extralight" || name == "ultralight") {
    return FontWeight::ExtraLight;
  }
  if (name == "light") {
    return FontWeight::Light;
  }
  if (name == "regular" || name == "normal") {
    return FontWeight::Regular;
  }
  if (name == "medium") {
    return FontWeight::Medium;
  }
  if (name == "semibold" || name == "demibold") {
    return FontWeight::SemiBold;
  }
  if (name == "bold") {
    return FontWeight::Bold;
  }
  if (name == "extrabold" || name == "ultrabold") {
    return FontWeight::ExtraBold;
  }
  if (name == "black" || name == "heavy") {
    return FontWeight::Black;
  }
  return std::nullopt;
}

std::optional<TextAlign> ParseTextAlign(const quickjs::Value& value) {
  const auto text = value.ToString();
  if (!text.Succeeded()) {
    return std::nullopt;
  }
  const auto name = AttributeName(text.Value());
  if (name == "leading" || name == "start" || name == "left") {
    return TextAlign::Leading;
  }
  if (name == "center") {
    return TextAlign::Center;
  }
  if (name == "trailing" || name == "end" || name == "right") {
    return TextAlign::Trailing;
  }
  return std::nullopt;
}

std::optional<TextDecoration> ParseDecoration(const quickjs::Value& value) {
  const auto text = value.ToString();
  if (!text.Succeeded()) {
    return std::nullopt;
  }
  const auto name = AttributeName(text.Value());
  if (name == "none") {
    return TextDecoration::None;
  }
  if (name == "underline") {
    return TextDecoration::Underline;
  }
  if (name == "strikethrough" || name == "lineThrough") {
    return TextDecoration::StrikeThrough;
  }
  return std::nullopt;
}

std::optional<TextStyle> TextStyleFromProps(const quickjs::Value& props) {
  const bool has_size = HasProperty(props, "fontSize");
  const bool has_weight = HasProperty(props, "fontWeight");
  const bool has_italic = HasProperty(props, "italic");
  const bool has_family = HasProperty(props, "fontFamily");
  const bool has_color = HasProperty(props, "color");
  const bool has_decoration = HasProperty(props, "decoration");
  if (!has_size && !has_weight && !has_italic && !has_family && !has_color && !has_decoration) {
    return std::nullopt;
  }

  float size = 14.0F;
  if (const auto number = NumberProperty(props, "fontSize")) {
    size = static_cast<float>(*number);
  }

  Font font = Font::System(size);
  if (const auto family = StringProperty(props, "fontFamily")) {
    const auto name = Lowercase(*family);
    if (name == "system") {
      font = Font::System(size);
    } else if (name == "monospace") {
      font = Font::Monospace(size);
    } else {
      font = Font::Named(*family, size);
    }
  }
  if (const auto value = Property(props, "fontWeight")) {
    if (const auto weight = ParseFontWeight(*value)) {
      font = font.WithWeight(*weight);
    }
  }
  if (const auto italic = BoolProperty(props, "italic"); italic && *italic) {
    font = font.WithSlant(FontSlant::Italic);
  }

  TextStyle style{.font = font, .foreground = Color::Rgb(31, 35, 40)};
  if (const auto value = Property(props, "color")) {
    if (const auto color = ToColor(*value)) {
      style.foreground = *color;
    }
  }
  if (const auto value = Property(props, "decoration")) {
    if (const auto decoration = ParseDecoration(*value)) {
      style.decoration = *decoration;
    }
  }
  return style;
}

std::string ChildText(const quickjs::Value& props) {
  const auto children = Property(props, "children");
  return children ? TextContent(*children) : std::string{};
}

View BuildLayout(RuntimeImpl& runtime, const quickjs::Value& props, int kind) {
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  switch (kind) {
  case 0:
    return Column(std::move(children));
  case 1:
    return Row(std::move(children));
  case 2:
    return Flow(std::move(children));
  default:
    return Stack(std::move(children));
  }
}

View BuildText(RuntimeImpl& runtime, const quickjs::Value& props) {
  StringVariant text(ChildText(props));
  if (auto localized = StringVariantProperty(props, "value")) {
    text = std::move(*localized);
  }
  const auto role = ParseRole(props).value_or(TextRole::Body);
  Text element(std::move(text), role);
  if (const auto style = TextStyleFromProps(props)) {
    element = std::move(element).Style(*style);
  }
  if (const auto value = Property(props, "textAlign")) {
    if (const auto align = ParseTextAlign(*value)) {
      element = std::move(element).Align(*align);
    }
  }
  return element;
}

View BuildButton(RuntimeImpl& runtime, const quickjs::Value& props) {
  StringVariant label(ChildText(props));
  if (auto localized = StringVariantProperty(props, "label")) {
    label = std::move(*localized);
  }
  return Button(std::move(label));
}

std::optional<ImageVariant> ImageFromValue(const quickjs::Value& source) {
  if (source.IsString()) {
    const auto path = source.ToString();
    if (!path.Succeeded()) {
      return std::nullopt;
    }
    try {
      return ImageAsset::FromFile(path.Value());
    } catch (...) {
      return std::nullopt;
    }
  }
  if (!source.IsObject()) {
    return std::nullopt;
  }
  if (const auto file = StringProperty(source, "file")) {
    try {
      return ImageAsset::FromFile(*file);
    } catch (...) {
      return std::nullopt;
    }
  }
  if (const auto resource = ParseResourceId(source)) {
    return ImageResource(resource->first, resource->second);
  }
  return std::nullopt;
}

std::optional<ImageVariant> ImageFromProps(const quickjs::Value& props) {
  const auto source = Property(props, "src");
  if (!source) {
    return std::nullopt;
  }
  return ImageFromValue(*source);
}

View BuildIconButton(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto image = ImageFromProps(props);
  if (!image) {
    return Text("[IconButton: missing src]");
  }
  StringVariant label("");
  if (auto localized = StringVariantProperty(props, "label")) {
    label = std::move(*localized);
  }
  return IconButton(*image, std::move(label));
}

View BuildImage(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto image = ImageFromProps(props);
  if (!image) {
    return Text("[Image: missing src]");
  }
  Image element(*image);
  if (const auto fit = StringProperty(props, "fit")) {
    const auto name = AttributeName(*fit);
    if (name == "none") {
      element = std::move(element).Fit(ImageFit::None);
    } else if (name == "contain") {
      element = std::move(element).Fit(ImageFit::Contain);
    } else if (name == "cover") {
      element = std::move(element).Fit(ImageFit::Cover);
    } else if (name == "fill") {
      element = std::move(element).Fit(ImageFit::Fill);
    } else if (name == "scaledown") {
      element = std::move(element).Fit(ImageFit::ScaleDown);
    }
  }
  if (const auto value = Property(props, "tint")) {
    if (const auto color = ToColor(*value)) {
      element = std::move(element).Tint(*color);
    }
  }
  return element;
}

void SetOptionalFrameValue(
    quickjs::Context& context,
    quickjs::Value& object,
    const char* name,
    std::optional<float> value
) {
  if (value) {
    static_cast<void>(object.SetProperty(name, context.Number(*value)));
  }
}

View BuildSelect(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const auto items_value = Property(props, "items");
  if (!items_value || !items_value->IsArray()) {
    return Text("Select requires an items array");
  }
  const auto length = items_value->Length();
  if (!length.Succeeded() || length.Value() <= 0) {
    return Text("Select requires a non-empty items array");
  }
  std::vector<quickjs::Value> items;
  items.reserve(static_cast<std::size_t>(length.Value()));
  for (std::int64_t index = 0; index < length.Value(); ++index) {
    const auto element = items_value->GetElement(static_cast<std::uint32_t>(index));
    if (element.Succeeded()) {
      items.push_back(element.Value());
    }
  }
  if (items.empty()) {
    return Text("Select requires at least one valid item");
  }
  const auto selected = static_cast<std::size_t>(std::clamp(
      NumberProperty(props, "selected").value_or(0.0),
      0.0,
      static_cast<double>(items.size() - 1)
  ));
  Select element(items, selected, [&runtime](const quickjs::Value& value) {
    return runtime.RenderValue(value);
  });
  if (auto label = StringVariantProperty(props, "label")) {
    element = std::move(element).Label(std::move(*label));
  }
  if (const auto validation = ValidationFromProps(props)) {
    element = std::move(element).Validation(*validation);
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{
              impl->Context().Number(static_cast<double>(next))
          };
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  if (const auto handler = Property(props, "onExpandedChange");
      handler && handler->IsFunction()) {
    element = std::move(element).OnExpandedChanged(
        [impl = &runtime, function = *handler, component](bool expanded) {
          const quickjs::Value arguments[]{impl->Context().Boolean(expanded)};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildComboBox(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  TextEditingValue value;
  value.text = StringProperty(props, "value").value_or("");
  std::vector<std::string> suggestions;
  if (const auto list = Property(props, "suggestions"); list && list->IsArray()) {
    const auto length = list->Length();
    if (length.Succeeded()) {
      suggestions.reserve(static_cast<std::size_t>(length.Value()));
      for (std::int64_t index = 0; index < length.Value(); ++index) {
        const auto element = list->GetElement(static_cast<std::uint32_t>(index));
        if (!element.Succeeded()) {
          continue;
        }
        const auto text = element.Value().ToString();
        if (text.Succeeded()) {
          suggestions.push_back(text.Value());
        }
      }
    }
  }
  ComboBox element(value, suggestions);
  if (auto label = StringVariantProperty(props, "label")) {
    element = std::move(element).Label(std::move(*label));
  }
  if (auto placeholder = StringVariantProperty(props, "placeholder")) {
    element = std::move(element).Placeholder(std::move(*placeholder));
  }
  if (const auto max_length = NumberProperty(props, "maxLength")) {
    element = std::move(element).MaxLength(
        static_cast<std::size_t>(std::max(0.0, *max_length))
    );
  }
  if (const auto validation = ValidationFromProps(props)) {
    element = std::move(element).Validation(*validation);
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](const TextEditingValue& next) {
          const auto text = impl->Context().NewString(next.text);
          if (text.Succeeded()) {
            const quickjs::Value arguments[]{text.Value()};
            impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
          }
        }
    );
  }
  if (const auto handler = Property(props, "onSelect"); handler && handler->IsFunction()) {
    element = std::move(element).OnSelected(
        [impl = &runtime, function = *handler, component](
            std::size_t index, const TextEditingValue& next) {
          const auto text = impl->Context().NewString(next.text);
          if (text.Succeeded()) {
            const quickjs::Value arguments[]{
                impl->Context().Number(static_cast<double>(index)),
                text.Value(),
            };
            impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
          }
        }
    );
  }
  if (const auto handler = Property(props, "onSubmit"); handler && handler->IsFunction()) {
    element = std::move(element).OnSubmitted(
        [impl = &runtime, function = *handler, component] {
          impl->InvokeHandler(function, {}, DiagnosticPhase::Event, component);
        }
    );
  }
  if (const auto handler = Property(props, "onExpandedChange");
      handler && handler->IsFunction()) {
    element = std::move(element).OnExpandedChanged(
        [impl = &runtime, function = *handler, component](bool expanded) {
          const quickjs::Value arguments[]{impl->Context().Boolean(expanded)};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildDatePicker(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const auto value = ParseDate(StringProperty(props, "value").value_or(""));
  if (!value) {
    return Text("DatePicker requires value as YYYY-MM-DD");
  }
  DatePicker element(*value);
  if (auto label = StringVariantProperty(props, "label")) {
    element = std::move(element).Label(std::move(*label));
  }
  if (const auto validation = ValidationFromProps(props)) {
    element = std::move(element).Validation(*validation);
  }
  if (const auto minimum = ParseDate(StringProperty(props, "min").value_or(""))) {
    element = std::move(element).Minimum(*minimum);
  }
  if (const auto maximum = ParseDate(StringProperty(props, "max").value_or(""))) {
    element = std::move(element).Maximum(*maximum);
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::chrono::year_month_day next) {
          const auto text = impl->Context().NewString(FormatDate(next));
          if (text.Succeeded()) {
            const quickjs::Value arguments[]{text.Value()};
            impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
          }
        }
    );
  }
  return element;
}

View BuildTimePicker(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const auto value = ParseTime(StringProperty(props, "value").value_or(""));
  if (!value) {
    return Text("TimePicker requires value as HH:MM");
  }
  TimePicker element(*value);
  if (auto label = StringVariantProperty(props, "label")) {
    element = std::move(element).Label(std::move(*label));
  }
  if (const auto validation = ValidationFromProps(props)) {
    element = std::move(element).Validation(*validation);
  }
  if (const auto step = NumberProperty(props, "step"); step && *step > 0.0) {
    element = std::move(element).Step(
        std::chrono::minutes{static_cast<std::int64_t>(*step)}
    );
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::chrono::minutes next) {
          const auto text = impl->Context().NewString(FormatTime(next));
          if (text.Succeeded()) {
            const quickjs::Value arguments[]{text.Value()};
            impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
          }
        }
    );
  }
  return element;
}

View BuildRefreshBox(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  View content = children.size() == 1 ? children.front() : Column(std::move(children));
  RefreshBox element(std::move(content), BoolProperty(props, "refreshing").value_or(false));
  if (const auto handler = Property(props, "onRefresh"); handler && handler->IsFunction()) {
    element = std::move(element).OnRefresh(
        [impl = &runtime, function = *handler, component] {
          impl->InvokeHandler(function, {}, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildPager(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  if (children.empty()) {
    return Text("Pager requires at least one child");
  }
  const auto selected = static_cast<std::size_t>(std::clamp(
      NumberProperty(props, "selected").value_or(0.0),
      0.0,
      static_cast<double>(children.size() - 1)
  ));
  Pager element(std::move(children), selected);
  if (const auto axis = StringProperty(props, "axis")) {
    element = std::move(element).ScrollAxis(
        AttributeName(*axis) == "vertical" ? Axis::Vertical : Axis::Horizontal
    );
  }
  if (const auto reverse = BoolProperty(props, "reverse")) {
    element = std::move(element).Reverse(*reverse);
  }
  if (const auto drag = BoolProperty(props, "dragEnabled")) {
    element = std::move(element).DragEnabled(*drag);
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{
              impl->Context().Number(static_cast<double>(next))
          };
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  if (const auto handler = Property(props, "onSettled"); handler && handler->IsFunction()) {
    element = std::move(element).OnSettled(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{
              impl->Context().Number(static_cast<double>(next))
          };
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildSplitLayout(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  if (children.size() < 2) {
    return Text("SplitLayout requires at least two children");
  }
  SplitLayout element(std::move(children));
  if (const auto axis = StringProperty(props, "axis")) {
    element = std::move(element).Orientation(
        AttributeName(*axis) == "vertical" ? Axis::Vertical : Axis::Horizontal
    );
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](
            std::vector<SplitChange> changes) {
          auto array = impl->Context().NewArray();
          if (!array.Succeeded()) {
            return;
          }
          for (std::size_t index = 0; index < changes.size(); ++index) {
            auto item = impl->Context().NewObject();
            if (!item.Succeeded()) {
              continue;
            }
            auto& object = item.Value();
            static_cast<void>(object.SetProperty(
                "index",
                impl->Context().Number(static_cast<double>(changes[index].index))
            ));
            static_cast<void>(object.SetProperty(
                "grow",
                impl->Context().Number(static_cast<double>(changes[index].grow.factor))
            ));
            auto frame = impl->Context().NewObject();
            if (frame.Succeeded()) {
              SetOptionalFrameValue(impl->Context(), frame.Value(), "width", changes[index].frame.width);
              SetOptionalFrameValue(impl->Context(), frame.Value(), "height", changes[index].frame.height);
              SetOptionalFrameValue(impl->Context(), frame.Value(), "minWidth", changes[index].frame.min_width);
              SetOptionalFrameValue(impl->Context(), frame.Value(), "maxWidth", changes[index].frame.max_width);
              SetOptionalFrameValue(impl->Context(), frame.Value(), "minHeight", changes[index].frame.min_height);
              SetOptionalFrameValue(impl->Context(), frame.Value(), "maxHeight", changes[index].frame.max_height);
              static_cast<void>(object.SetProperty("frame", frame.Value()));
            }
            static_cast<void>(array.Value().SetElement(
                static_cast<std::uint32_t>(index),
                object
            ));
          }
          const quickjs::Value arguments[]{array.Value()};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildTopAppBar(RuntimeImpl& runtime, const quickjs::Value& props) {
  StringVariant title(ChildText(props));
  if (auto localized = StringVariantProperty(props, "title")) {
    title = std::move(*localized);
  }
  std::optional<View> leading;
  if (const auto value = Property(props, "leading");
      value && !value->IsUndefined() && !value->IsNull()) {
    leading = runtime.RenderValue(*value);
  }
  auto actions = runtime.RenderChildren(
      Property(props, "actions").value_or(runtime.Context().Undefined())
  );
  TopAppBar element(std::move(title), std::move(leading), std::move(actions));
  if (const auto align = StringProperty(props, "titleAlign");
      align && AttributeName(*align) == "center") {
    element = std::move(element).TitleAlignment(TopAppBarTitleAlignment::Center);
  }
  return element;
}

View BuildDivider(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto axis = StringProperty(props, "axis").value_or("horizontal");
  return Divider(AttributeName(axis) == "vertical" ? Axis::Vertical : Axis::Horizontal);
}

View BuildScrollView(RuntimeImpl& runtime, const quickjs::Value& props) {
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  View content = children.size() == 1 ? children.front() : Column(std::move(children));
  ScrollView element(content);
  const auto axis = StringProperty(props, "axis").value_or("vertical");
  element = std::move(element).ScrollAxis(AttributeName(axis) == "horizontal" ? Axis::Horizontal : Axis::Vertical);
  if (const auto ref = Property(props, "scrollRef"); ref && ref->IsObject()) {
    if (const auto controller = runtime.FindScrollController(*ref)) {
      element = std::move(element).Controller(*controller);
    }
  }
  return element;
}

View BuildSelectionArea(RuntimeImpl& runtime, const quickjs::Value& props) {
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  View content = children.size() == 1 ? children.front() : Column(std::move(children));
  return SelectionArea(content);
}

View BuildTextField(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  TextEditingValue value;
  value.text = StringProperty(props, "value").value_or("");
  if (const auto selection = Property(props, "selection"); selection && selection->IsObject()) {
    const auto anchor = NumberProperty(*selection, "anchor");
    const auto active = NumberProperty(*selection, "active");
    if (anchor && active) {
      value.selection.anchor = static_cast<TextOffset>(*anchor);
      value.selection.active = static_cast<TextOffset>(*active);
      if (const auto affinity = StringProperty(*selection, "affinity");
          affinity && AttributeName(*affinity) == "upstream") {
        value.selection.affinity = TextAffinity::Upstream;
      }
    }
  }
  TextField element(value);
  if (const auto label = StringProperty(props, "label")) {
    element = std::move(element).Label(*label);
  }
  if (const auto placeholder = StringProperty(props, "placeholder")) {
    element = std::move(element).Placeholder(*placeholder);
  }
  if (const auto variant = StringProperty(props, "variant")) {
    const auto name = AttributeName(*variant);
    if (name == "filled") {
      element = std::move(element).Variant(TextFieldVariant::Filled);
    } else if (name == "outlined") {
      element = std::move(element).Variant(TextFieldVariant::Outlined);
    } else if (name == "standard") {
      element = std::move(element).Variant(TextFieldVariant::Standard);
    }
  }
  if (const auto multiline = BoolProperty(props, "multiline"); multiline && *multiline) {
    element = std::move(element).LineLimits(TextFieldLineLimits::MultiLine());
  }
  if (const auto max_length = NumberProperty(props, "maxLength")) {
    element = std::move(element).MaxLength(static_cast<std::size_t>(std::max(0.0, *max_length)));
  }
  if (const auto secure = BoolProperty(props, "secure"); secure && *secure) {
    element = std::move(element).Secure();
  }
  if (const auto validation = ValidationFromProps(props)) {
    element = std::move(element).Validation(*validation);
  }
  const auto on_change = Property(props, "onChange");
  const auto on_edit = Property(props, "onEdit");
  if ((on_change && on_change->IsFunction()) || (on_edit && on_edit->IsFunction())) {
    element = std::move(element).OnChanged(
        [impl = &runtime, on_change, on_edit, component](const TextEditingValue& next) {
          if (on_change && on_change->IsFunction()) {
            const auto argument = impl->Context().NewString(next.text);
            if (argument.Succeeded()) {
              const quickjs::Value arguments[]{argument.Value()};
              impl->InvokeHandler(*on_change, arguments, DiagnosticPhase::Event, component);
            }
          }
          if (on_edit && on_edit->IsFunction()) {
            try {
              const auto event = MakeTextEditingEvent(impl->Context(), next);
              const quickjs::Value arguments[]{event};
              impl->InvokeHandler(*on_edit, arguments, DiagnosticPhase::Event, component);
            } catch (const std::exception& error) {
              impl->ReportMessage(
                  DiagnosticPhase::Event,
                  "text edit event",
                  error.what(),
                  component
              );
            }
          }
        }
    );
  }
  if (const auto handler = Property(props, "onSubmit"); handler && handler->IsFunction()) {
    element = std::move(element).OnSubmitted([impl = &runtime, function = *handler, component] {
      impl->InvokeHandler(function, {}, DiagnosticPhase::Event, component);
    });
  }
  return element;
}

View BuildCheckbox(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool checked = BoolProperty(props, "checked").value_or(false);
  Checkbox element = label.empty() ? Checkbox(checked) : Checkbox(label, checked);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler, component](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
    });
  }
  return element;
}

View BuildRadioButton(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool selected = BoolProperty(props, "selected").value_or(false);
  RadioButton element = label.empty() ? RadioButton(selected) : RadioButton(label, selected);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler, component](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
    });
  }
  return element;
}

View BuildSwitch(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool checked = BoolProperty(props, "checked").value_or(false);
  Switch element = label.empty() ? Switch(checked) : Switch(label, checked);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler, component](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
    });
  }
  return element;
}

View BuildSlider(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const float value = static_cast<float>(NumberProperty(props, "value").value_or(0.0));
  Slider element(value);
  const auto minimum = NumberProperty(props, "min");
  const auto maximum = NumberProperty(props, "max");
  if (minimum || maximum) {
    element = std::move(element).Range(
        static_cast<float>(minimum.value_or(0.0)),
        static_cast<float>(maximum.value_or(1.0))
    );
  }
  if (const auto step = NumberProperty(props, "step")) {
    element = std::move(element).Step(static_cast<float>(*step));
  }
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler, component](float next) {
      const quickjs::Value arguments[]{impl->Context().Number(next)};
      impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
    });
  }
  return element;
}

View BuildProgressBar(RuntimeImpl& runtime, const quickjs::Value& props) {
  if (const auto progress = NumberProperty(props, "progress")) {
    return ProgressBar(static_cast<float>(*progress));
  }
  return ProgressBar();
}

View BuildProgressCircle(RuntimeImpl& runtime, const quickjs::Value& props) {
  if (const auto progress = NumberProperty(props, "progress")) {
    return ProgressCircle(static_cast<float>(*progress));
  }
  return ProgressCircle();
}

View BuildChip(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool selected = BoolProperty(props, "selected").value_or(false);
  Chip element(label, selected);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler, component](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
    });
  }
  return element;
}

std::vector<StringVariant> StringListProperty(const quickjs::Value& props, std::string_view name) {
  std::vector<StringVariant> output;
  const auto value = Property(props, name);
  if (!value || !value->IsArray()) {
    return output;
  }
  const auto length = value->Length();
  if (!length.Succeeded()) {
    return output;
  }
  output.reserve(static_cast<std::size_t>(length.Value()));
  for (std::int64_t index = 0; index < length.Value(); ++index) {
    const auto element = value->GetElement(static_cast<std::uint32_t>(index));
    if (!element.Succeeded()) {
      continue;
    }
    const auto text = element.Value().ToString();
    if (text.Succeeded()) {
      output.emplace_back(text.Value());
    }
  }
  return output;
}

View BuildVirtualList(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto items = Property(props, "items");
  const auto render = Property(props, "renderItem");
  if (!items || !items->IsArray()) {
    return Text("VirtualList requires an items array");
  }
  if (!render || !render->IsFunction()) {
    return Text("VirtualList requires a renderItem function");
  }
  const auto length = items->Length();
  if (!length.Succeeded() || length.Value() < 0) {
    return Text("VirtualList items length is invalid");
  }

  const auto component = runtime.CurrentComponentPath();
  auto item_array = *items;
  auto render_function = *render;
  auto* impl = &runtime;
  VirtualList element(
      static_cast<std::size_t>(length.Value()),
      [impl, item_array, render_function, component](std::size_t index) -> View {
        const auto item = item_array.GetElement(static_cast<std::uint32_t>(index));
        if (!item.Succeeded()) {
          return Text("VirtualList could not read item " + std::to_string(index));
        }
        const quickjs::Value arguments[]{
            item.Value(),
            impl->Context().Number(static_cast<double>(index)),
        };
        const auto rendered = render_function.Call(arguments);
        if (!rendered.Succeeded()) {
          impl->ReportError(
              DiagnosticPhase::Render,
              "virtual item",
              rendered.Error(),
              component
          );
          return impl->ErrorView(rendered.Error().Message());
        }
        return impl->RenderValue(rendered.Value());
      }
  );

  if (const auto axis = StringProperty(props, "axis"); axis) {
    element = std::move(element).ScrollAxis(
        AttributeName(*axis) == "horizontal" ? Axis::Horizontal : Axis::Vertical
    );
  }
  if (const auto extent = NumberProperty(props, "itemExtent"); extent && *extent > 0.0) {
    element = std::move(element).ItemExtent(static_cast<float>(*extent));
  }
  if (const auto extent = NumberProperty(props, "estimatedItemExtent"); extent && *extent > 0.0) {
    element = std::move(element).EstimatedItemExtent(static_cast<float>(*extent));
  }
  if (const auto extent = NumberProperty(props, "cacheExtent"); extent && *extent >= 0.0) {
    element = std::move(element).CacheExtent(static_cast<float>(*extent));
  }
  if (const auto ref = Property(props, "scrollRef"); ref && ref->IsObject()) {
    if (const auto controller = runtime.FindScrollController(*ref)) {
      element = std::move(element).Controller(*controller);
    }
  }
  return element;
}

View BuildVirtualGrid(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto items = Property(props, "items");
  const auto render = Property(props, "renderItem");
  if (!items || !items->IsArray()) {
    return Text("VirtualGrid requires an items array");
  }
  if (!render || !render->IsFunction()) {
    return Text("VirtualGrid requires a renderItem function");
  }
  const auto length = items->Length();
  if (!length.Succeeded() || length.Value() < 0) {
    return Text("VirtualGrid items length is invalid");
  }

  const auto component = runtime.CurrentComponentPath();
  auto item_array = *items;
  auto render_function = *render;
  auto* impl = &runtime;
  VirtualGrid element(
      static_cast<std::size_t>(length.Value()),
      [impl, item_array, render_function, component](std::size_t index) -> View {
        const auto item = item_array.GetElement(static_cast<std::uint32_t>(index));
        if (!item.Succeeded()) {
          return Text("VirtualGrid could not read item " + std::to_string(index));
        }
        const quickjs::Value arguments[]{
            item.Value(),
            impl->Context().Number(static_cast<double>(index)),
        };
        const auto rendered = render_function.Call(arguments);
        if (!rendered.Succeeded()) {
          impl->ReportError(
              DiagnosticPhase::Render,
              "virtual grid item",
              rendered.Error(),
              component
          );
          return impl->ErrorView(rendered.Error().Message());
        }
        return impl->RenderValue(rendered.Value());
      }
  );

  if (const auto adaptive = NumberProperty(props, "adaptiveColumnWidth");
      adaptive && *adaptive > 0.0) {
    element = std::move(element).Columns(GridColumns::Adaptive(static_cast<float>(*adaptive)));
  } else {
    const auto columns = NumberProperty(props, "columns").value_or(1.0);
    element = std::move(element).Columns(
        GridColumns::Fixed(static_cast<std::size_t>(std::max(1.0, columns)))
    );
  }
  if (const auto extent = NumberProperty(props, "rowExtent"); extent && *extent > 0.0) {
    element = std::move(element).RowExtent(static_cast<float>(*extent));
  }
  if (const auto extent = NumberProperty(props, "estimatedRowExtent"); extent && *extent > 0.0) {
    element = std::move(element).EstimatedRowExtent(static_cast<float>(*extent));
  }
  if (const auto spacing = NumberProperty(props, "rowSpacing"); spacing && *spacing >= 0.0) {
    element = std::move(element).RowSpacing(static_cast<float>(*spacing));
  }
  if (const auto spacing = NumberProperty(props, "columnSpacing"); spacing && *spacing >= 0.0) {
    element = std::move(element).ColumnSpacing(static_cast<float>(*spacing));
  }
  if (const auto extent = NumberProperty(props, "cacheExtent"); extent && *extent >= 0.0) {
    element = std::move(element).CacheExtent(static_cast<float>(*extent));
  }
  if (const auto spans = Property(props, "itemSpans"); spans && spans->IsArray()) {
    std::vector<std::size_t> values;
    const auto span_length = spans->Length();
    if (span_length.Succeeded()) {
      values.reserve(static_cast<std::size_t>(span_length.Value()));
      for (std::int64_t index = 0; index < span_length.Value(); ++index) {
        const auto span = spans->GetElement(static_cast<std::uint32_t>(index));
        if (!span.Succeeded()) {
          continue;
        }
        const auto number = span.Value().ToNumber();
        if (number.Succeeded() && std::isfinite(number.Value())) {
          values.push_back(static_cast<std::size_t>(std::max(1.0, number.Value())));
        }
      }
    }
    if (!values.empty()) {
      element = std::move(element).ItemSpans(std::move(values));
    }
  }
  if (const auto ref = Property(props, "scrollRef"); ref && ref->IsObject()) {
    if (const auto controller = runtime.FindScrollController(*ref)) {
      element = std::move(element).Controller(*controller);
    }
  }
  return element;
}

View BuildNavigationPane(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto component = runtime.CurrentComponentPath();
  auto labels = StringListProperty(props, "labels");
  if (labels.empty()) {
    return Text("NavigationPane requires a non-empty labels array");
  }
  std::vector<NavigationItem> items;
  items.reserve(labels.size());
  for (auto& label : labels) {
    items.emplace_back(std::move(label));
  }
  const double selected = NumberProperty(props, "selected").value_or(0.0);
  const auto index = static_cast<std::size_t>(std::clamp(
      selected,
      0.0,
      static_cast<double>(items.size() - 1)
  ));
  NavigationPane element(
      std::move(items),
      index,
      BoolProperty(props, "expanded").value_or(false)
  );
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{impl->Context().Number(static_cast<double>(next))};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildNavigationBar(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto component = runtime.CurrentComponentPath();
  const auto items_value = Property(props, "items");
  if (!items_value || !items_value->IsArray()) {
    return Text("NavigationBar requires an items array");
  }
  const auto length = items_value->Length();
  if (!length.Succeeded() || length.Value() <= 0) {
    return Text("NavigationBar requires a non-empty items array");
  }
  std::vector<NavigationItem> items;
  items.reserve(static_cast<std::size_t>(length.Value()));
  for (std::int64_t index = 0; index < length.Value(); ++index) {
    const auto value = items_value->GetElement(static_cast<std::uint32_t>(index));
    if (!value.Succeeded() || !value.Value().IsObject()) {
      continue;
    }
    const auto label_value = Property(value.Value(), "label");
    const auto source_value = Property(value.Value(), "src");
    const auto label = label_value ? StringVariantFromValue(*label_value) : std::nullopt;
    const auto source = source_value ? ImageFromValue(*source_value) : std::nullopt;
    if (!label || !source) {
      continue;
    }
    NavigationItem item(*source, std::move(*label));
    if (const auto selected_value = Property(value.Value(), "selectedSrc")) {
      if (auto selected = ImageFromValue(*selected_value)) {
        item = std::move(item).SelectedIcon(*selected);
      }
    }
    if (const auto disabled = BoolProperty(value.Value(), "disabled"); disabled && *disabled) {
      item = std::move(item).Enabled(false);
    }
    items.push_back(std::move(item));
  }
  if (items.empty()) {
    return Text("NavigationBar requires at least one valid item");
  }
  const double selected = NumberProperty(props, "selected").value_or(0.0);
  const auto selected_index = static_cast<std::size_t>(std::clamp(
      selected,
      0.0,
      static_cast<double>(items.size() - 1)
  ));
  NavigationBar element(std::move(items), selected_index);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{impl->Context().Number(static_cast<double>(next))};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildTabs(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto component = runtime.CurrentComponentPath();
  auto labels = StringListProperty(props, "labels");
  if (labels.empty()) {
    return Text("Tabs requires a non-empty labels array");
  }
  const double selected = NumberProperty(props, "selected").value_or(0.0);
  const auto index = static_cast<std::size_t>(std::clamp(
      selected,
      0.0,
      static_cast<double>(labels.size() - 1)
  ));
  Tabs element(std::move(labels), index);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{impl->Context().Number(static_cast<double>(next))};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildSegmentedButton(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto component = runtime.CurrentComponentPath();
  auto labels = StringListProperty(props, "labels");
  if (labels.empty()) {
    return Text("SegmentedButton requires a non-empty labels array");
  }
  const double selected = NumberProperty(props, "selected").value_or(0.0);
  const auto index = static_cast<std::size_t>(std::clamp(
      selected,
      0.0,
      static_cast<double>(labels.size() - 1)
  ));
  SegmentedButton element(std::move(labels), index);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler, component](std::size_t next) {
          const quickjs::Value arguments[]{impl->Context().Number(static_cast<double>(next))};
          impl->InvokeHandler(function, arguments, DiagnosticPhase::Event, component);
        }
    );
  }
  return element;
}

View BuildIndexedPages(RuntimeImpl& runtime, const quickjs::Value& props) {
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  if (children.empty()) {
    return View{};
  }
  const auto selected = NumberProperty(props, "selected").value_or(0.0);
  const auto index = static_cast<std::size_t>(std::clamp(
      selected,
      0.0,
      static_cast<double>(children.size() - 1)
  ));
  return IndexedPages(std::move(children), index);
}

View BuildTheme(RuntimeImpl& runtime, const quickjs::Value& props) {
  auto children = runtime.RenderChildren(
      Property(props, "children").value_or(runtime.Context().Undefined())
  );
  View content = children.size() == 1 ? children.front() : Column(std::move(children));
  std::string variant = "material";
  if (const auto value = StringProperty(props, "variant")) {
    variant = Lowercase(*value);
  } else if (const auto value = StringProperty(props, "theme")) {
    variant = Lowercase(*value);
  }
  const bool dark = BoolProperty(props, "dark").value_or(false);
  if (variant == "flat" || variant == "flatdark") {
    if (variant == "flatdark" || dark) {
      return FlatDarkTheme(content);
    }
    return FlatTheme(content);
  }
  if (variant == "materialdark" || dark) {
    return MaterialDarkTheme(content);
  }
  return MaterialTheme(content);
}

} // namespace

View BuildElement(RuntimeImpl& runtime, std::string_view tag, const quickjs::Value& props) {
  if (tag == "Column") {
    return BuildLayout(runtime, props, 0);
  }
  if (tag == "Row") {
    return BuildLayout(runtime, props, 1);
  }
  if (tag == "Flow") {
    return BuildLayout(runtime, props, 2);
  }
  if (tag == "Stack") {
    return BuildLayout(runtime, props, 3);
  }
  if (tag == "Text") {
    return BuildText(runtime, props);
  }
  if (tag == "Button") {
    return BuildButton(runtime, props);
  }
  if (tag == "IconButton") {
    return BuildIconButton(runtime, props);
  }
  if (tag == "Image") {
    return BuildImage(runtime, props);
  }
  if (tag == "Select") {
    return BuildSelect(runtime, props);
  }
  if (tag == "ComboBox") {
    return BuildComboBox(runtime, props);
  }
  if (tag == "DatePicker") {
    return BuildDatePicker(runtime, props);
  }
  if (tag == "TimePicker") {
    return BuildTimePicker(runtime, props);
  }
  if (tag == "RefreshBox") {
    return BuildRefreshBox(runtime, props);
  }
  if (tag == "Pager") {
    return BuildPager(runtime, props);
  }
  if (tag == "SplitLayout") {
    return BuildSplitLayout(runtime, props);
  }
  if (tag == "TopAppBar") {
    return BuildTopAppBar(runtime, props);
  }
  if (tag == "Divider") {
    return BuildDivider(runtime, props);
  }
  if (tag == "Spacer") {
    return Spacer();
  }
  if (tag == "ScrollView") {
    return BuildScrollView(runtime, props);
  }
  if (tag == "SelectionArea") {
    return BuildSelectionArea(runtime, props);
  }
  if (tag == "TextField") {
    return BuildTextField(runtime, props);
  }
  if (tag == "Checkbox") {
    return BuildCheckbox(runtime, props);
  }
  if (tag == "RadioButton") {
    return BuildRadioButton(runtime, props);
  }
  if (tag == "Switch") {
    return BuildSwitch(runtime, props);
  }
  if (tag == "Slider") {
    return BuildSlider(runtime, props);
  }
  if (tag == "ProgressBar") {
    return BuildProgressBar(runtime, props);
  }
  if (tag == "ProgressCircle") {
    return BuildProgressCircle(runtime, props);
  }
  if (tag == "Chip") {
    return BuildChip(runtime, props);
  }
  if (tag == "IndexedPages") {
    return BuildIndexedPages(runtime, props);
  }
  if (tag == "VirtualList") {
    return BuildVirtualList(runtime, props);
  }
  if (tag == "VirtualGrid") {
    return BuildVirtualGrid(runtime, props);
  }
  if (tag == "Tabs") {
    return BuildTabs(runtime, props);
  }
  if (tag == "NavigationPane") {
    return BuildNavigationPane(runtime, props);
  }
  if (tag == "NavigationBar") {
    return BuildNavigationBar(runtime, props);
  }
  if (tag == "SegmentedButton") {
    return BuildSegmentedButton(runtime, props);
  }
  if (tag == "Theme") {
    return BuildTheme(runtime, props);
  }
  return Text(std::string("Unknown TSX element <") + std::string(tag) + ">");
}

} // namespace huxerui::tsx::detail
