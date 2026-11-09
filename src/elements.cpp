#include "props.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
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
  std::string text = ChildText(props);
  if (const auto value = StringProperty(props, "value")) {
    text = *value;
  }
  const auto role = ParseRole(props).value_or(TextRole::Body);
  Text element(text, role);
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
  std::string label = StringProperty(props, "label").value_or(ChildText(props));
  return Button(label);
}

std::optional<ImageVariant> ImageFromProps(const quickjs::Value& props) {
  const auto source = StringProperty(props, "src");
  if (!source) {
    return std::nullopt;
  }
  try {
    return ImageAsset::FromFile(*source);
  } catch (...) {
    return std::nullopt;
  }
}

View BuildIconButton(RuntimeImpl& runtime, const quickjs::Value& props) {
  const auto image = ImageFromProps(props);
  if (!image) {
    return Text("[IconButton: missing src]");
  }
  return IconButton(*image, StringProperty(props, "label").value_or(""));
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
  TextEditingValue value;
  value.text = StringProperty(props, "value").value_or("");
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
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged(
        [impl = &runtime, function = *handler](const TextEditingValue& next) {
          const auto argument = impl->Context().NewString(next.text);
          if (argument.Succeeded()) {
            const quickjs::Value arguments[]{argument.Value()};
            impl->InvokeHandler(function, arguments);
          }
        }
    );
  }
  if (const auto handler = Property(props, "onSubmit"); handler && handler->IsFunction()) {
    element = std::move(element).OnSubmitted([impl = &runtime, function = *handler] {
      impl->InvokeHandler(function, {});
    });
  }
  return element;
}

View BuildCheckbox(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool checked = BoolProperty(props, "checked").value_or(false);
  Checkbox element = label.empty() ? Checkbox(checked) : Checkbox(label, checked);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments);
    });
  }
  return element;
}

View BuildRadioButton(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool selected = BoolProperty(props, "selected").value_or(false);
  RadioButton element = label.empty() ? RadioButton(selected) : RadioButton(label, selected);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments);
    });
  }
  return element;
}

View BuildSwitch(RuntimeImpl& runtime, const quickjs::Value& props) {
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool checked = BoolProperty(props, "checked").value_or(false);
  Switch element = label.empty() ? Switch(checked) : Switch(label, checked);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments);
    });
  }
  return element;
}

View BuildSlider(RuntimeImpl& runtime, const quickjs::Value& props) {
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
    element = std::move(element).OnChanged([impl = &runtime, function = *handler](float next) {
      const quickjs::Value arguments[]{impl->Context().Number(next)};
      impl->InvokeHandler(function, arguments);
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
  const std::string label = StringProperty(props, "label").value_or(ChildText(props));
  const bool selected = BoolProperty(props, "selected").value_or(false);
  Chip element(label, selected);
  if (const auto handler = Property(props, "onChange"); handler && handler->IsFunction()) {
    element = std::move(element).OnChanged([impl = &runtime, function = *handler](bool next) {
      const quickjs::Value arguments[]{impl->Context().Boolean(next)};
      impl->InvokeHandler(function, arguments);
    });
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
  if (tag == "Theme") {
    return BuildTheme(runtime, props);
  }
  return Text(std::string("Unknown TSX element <") + std::string(tag) + ">");
}

} // namespace huxerui::tsx::detail
