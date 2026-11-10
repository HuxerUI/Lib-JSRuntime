#include "props.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <huxerui/file_drop.h>
#include <huxerui/paint.h>

namespace huxerui::tsx::detail {
namespace {

std::string Lowercase(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

std::string Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

int HexNibble(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return 10 + character - 'a';
  }
  if (character >= 'A' && character <= 'F') {
    return 10 + character - 'A';
  }
  return -1;
}

std::optional<Color> ParseHexColor(std::string_view text) {
  std::array<int, 4> channels{0, 0, 0, 255};
  std::size_t count = 0;
  if (text.size() == 4 || text.size() == 5) {
    for (std::size_t index = 1; index < text.size(); ++index) {
      const int nibble = HexNibble(text[index]);
      if (nibble < 0) {
        return std::nullopt;
      }
      channels[count++] = nibble * 17;
    }
  } else if (text.size() == 7 || text.size() == 9) {
    for (std::size_t index = 1; index + 1 < text.size(); index += 2) {
      const int high = HexNibble(text[index]);
      const int low = HexNibble(text[index + 1]);
      if (high < 0 || low < 0) {
        return std::nullopt;
      }
      channels[count++] = high * 16 + low;
    }
  } else {
    return std::nullopt;
  }
  return Color::Rgb(channels[0], channels[1], channels[2], static_cast<float>(channels[3]) / 255.0F);
}

std::optional<Color> ParseFunctionalColor(std::string_view text) {
  const bool rgba = text.starts_with("rgba(");
  if (!rgba && !text.starts_with("rgb(")) {
    return std::nullopt;
  }
  if (!text.ends_with(')')) {
    return std::nullopt;
  }
  const std::string body(text.substr(text.find('(') + 1, text.size() - text.find('(') - 2));
  std::array<std::string, 4> parts;
  std::size_t part = 0;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= body.size() && part < parts.size(); ++index) {
    if (index == body.size() || body[index] == ',') {
      parts[part++] = Trim(std::string_view(body).substr(start, index - start));
      start = index + 1;
    }
  }
  if (part < 3) {
    return std::nullopt;
  }
  auto channel = [](const std::string& value) -> std::optional<int> {
    if (value.empty()) {
      return std::nullopt;
    }
    try {
      const double parsed = std::stod(value);
      if (!std::isfinite(parsed)) {
        return std::nullopt;
      }
      return static_cast<int>(std::clamp(parsed, 0.0, 255.0));
    } catch (...) {
      return std::nullopt;
    }
  };
  const auto red = channel(parts[0]);
  const auto green = channel(parts[1]);
  const auto blue = channel(parts[2]);
  if (!red || !green || !blue) {
    return std::nullopt;
  }
  float alpha = 1.0F;
  if (rgba && !parts[3].empty()) {
    try {
      alpha = static_cast<float>(std::clamp(std::stod(parts[3]), 0.0, 1.0));
    } catch (...) {
      return std::nullopt;
    }
  }
  return Color::Rgb(*red, *green, *blue, alpha);
}

std::optional<Color> NamedColor(std::string_view name) {
  if (name == "transparent") {
    return Color::Transparent();
  }
  if (name == "black") {
    return Color::Black();
  }
  if (name == "white") {
    return Color::White();
  }
  if (name == "red") {
    return Color::Rgb(220, 53, 69);
  }
  if (name == "green") {
    return Color::Rgb(40, 167, 69);
  }
  if (name == "blue") {
    return Color::Rgb(31, 111, 235);
  }
  if (name == "gray" || name == "grey") {
    return Color::Rgb(108, 117, 125);
  }
  if (name == "orange") {
    return Color::Rgb(253, 126, 20);
  }
  if (name == "yellow") {
    return Color::Rgb(255, 193, 7);
  }
  if (name == "purple") {
    return Color::Rgb(111, 66, 193);
  }
  return std::nullopt;
}

template <class Modifier> View AddModifier(View view, Modifier&& modifier) {
  return std::move(view).With(std::forward<Modifier>(modifier));
}

std::optional<double> ElementNumber(const quickjs::Value& value, std::uint32_t index) {
  const auto element = value.GetElement(index);
  if (!element.Succeeded()) {
    return std::nullopt;
  }
  const auto number = element.Value().ToNumber();
  if (!number.Succeeded() || !std::isfinite(number.Value())) {
    return std::nullopt;
  }
  return number.Value();
}

std::optional<double> MemberNumber(const quickjs::Value& object, std::string_view name) {
  const auto member = Property(object, name);
  if (!member) {
    return std::nullopt;
  }
  const auto number = member->ToNumber();
  if (!number.Succeeded() || !std::isfinite(number.Value())) {
    return std::nullopt;
  }
  return number.Value();
}

std::string AlignName(const quickjs::Value& value) {
  const auto text = value.ToString();
  if (!text.Succeeded()) {
    return {};
  }
  return Lowercase(text.Value());
}

std::optional<MainAxisAlignment> ParseMainAlign(std::string_view name) {
  if (name == "start" || name == "leading") {
    return MainAxisAlignment::Start;
  }
  if (name == "center") {
    return MainAxisAlignment::Center;
  }
  if (name == "end" || name == "trailing") {
    return MainAxisAlignment::End;
  }
  if (name == "spacebetween") {
    return MainAxisAlignment::SpaceBetween;
  }
  if (name == "spacearound") {
    return MainAxisAlignment::SpaceAround;
  }
  if (name == "spaceevenly") {
    return MainAxisAlignment::SpaceEvenly;
  }
  return std::nullopt;
}

std::optional<CrossAxisAlignment> ParseCrossAlign(std::string_view name) {
  if (name == "start" || name == "leading") {
    return CrossAxisAlignment::Start;
  }
  if (name == "center") {
    return CrossAxisAlignment::Center;
  }
  if (name == "end" || name == "trailing") {
    return CrossAxisAlignment::End;
  }
  if (name == "stretch") {
    return CrossAxisAlignment::Stretch;
  }
  return std::nullopt;
}

std::optional<HorizontalAlignment> ParseHorizontalAlign(std::string_view name) {
  if (name == "start" || name == "leading") {
    return HorizontalAlignment::Start;
  }
  if (name == "center") {
    return HorizontalAlignment::Center;
  }
  if (name == "end" || name == "trailing") {
    return HorizontalAlignment::End;
  }
  if (name == "stretch") {
    return HorizontalAlignment::Stretch;
  }
  return std::nullopt;
}

std::optional<VerticalAlignment> ParseVerticalAlign(std::string_view name) {
  if (name == "start" || name == "leading") {
    return VerticalAlignment::Start;
  }
  if (name == "center") {
    return VerticalAlignment::Center;
  }
  if (name == "end" || name == "trailing") {
    return VerticalAlignment::End;
  }
  if (name == "stretch") {
    return VerticalAlignment::Stretch;
  }
  return std::nullopt;
}

} // namespace

std::optional<std::pair<std::string, std::string>> ParseResourceId(const quickjs::Value& value) {
  if (!value.IsObject()) {
    return std::nullopt;
  }
  if (const auto resource = StringProperty(value, "resource")) {
    const auto separator = resource->find(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= resource->size()) {
      return std::nullopt;
    }
    return std::make_pair(resource->substr(0, separator), resource->substr(separator + 1));
  }
  const auto domain = StringProperty(value, "domain");
  const auto key = StringProperty(value, "key");
  if (!domain || !key || domain->empty() || key->empty()) {
    return std::nullopt;
  }
  return std::make_pair(*domain, *key);
}

std::optional<StringVariant> StringVariantFromValue(const quickjs::Value& value) {
  if (value.IsString()) {
    const auto text = value.ToString();
    if (text.Succeeded()) {
      return StringVariant(text.Value());
    }
    return std::nullopt;
  }
  if (const auto resource = ParseResourceId(value)) {
    return StringVariant(StringResource(resource->first, resource->second));
  }
  return std::nullopt;
}

std::optional<StringVariant> StringVariantProperty(
    const quickjs::Value& props,
    std::string_view name
) {
  const auto value = Property(props, name);
  if (!value) {
    return std::nullopt;
  }
  return StringVariantFromValue(*value);
}

std::optional<quickjs::Value> Property(const quickjs::Value& object, std::string_view name) {
  if (!object.IsObject()) {
    return std::nullopt;
  }
  auto result = object.GetProperty(name);
  if (!result.Succeeded() || result.Value().IsUndefined()) {
    return std::nullopt;
  }
  return result.Value();
}

bool HasProperty(const quickjs::Value& object, std::string_view name) {
  return Property(object, name).has_value();
}

std::optional<double> NumberProperty(const quickjs::Value& object, std::string_view name) {
  return MemberNumber(object, name);
}

std::optional<bool> BoolProperty(const quickjs::Value& object, std::string_view name) {
  const auto value = Property(object, name);
  if (!value) {
    return std::nullopt;
  }
  const auto result = value->ToBoolean();
  if (!result.Succeeded()) {
    return std::nullopt;
  }
  return result.Value();
}

std::optional<std::string> StringProperty(const quickjs::Value& object, std::string_view name) {
  const auto value = Property(object, name);
  if (!value) {
    return std::nullopt;
  }
  if (!value->IsString() && !value->IsNumber()) {
    return std::nullopt;
  }
  const auto result = value->ToString();
  if (!result.Succeeded()) {
    return std::nullopt;
  }
  return result.Value();
}

std::string TextContent(const quickjs::Value& value) {
  if (value.IsUndefined() || value.IsNull() || value.IsBoolean()) {
    return {};
  }
  if (value.IsString() || value.IsNumber()) {
    const auto text = value.ToString();
    return text.Succeeded() ? text.Value() : std::string{};
  }
  if (value.IsArray()) {
    const auto length = value.Length();
    if (!length.Succeeded()) {
      return {};
    }
    std::string result;
    for (std::int64_t index = 0; index < length.Value(); ++index) {
      const auto element = value.GetElement(static_cast<std::uint32_t>(index));
      if (element.Succeeded()) {
        result += TextContent(element.Value());
      }
    }
    return result;
  }
  return {};
}

std::optional<Color> ToColor(const quickjs::Value& value) {
  if (value.IsString()) {
    const auto text = value.ToString();
    if (!text.Succeeded()) {
      return std::nullopt;
    }
    const std::string color = Lowercase(Trim(text.Value()));
    if (color.empty()) {
      return std::nullopt;
    }
    if (color.front() == '#') {
      return ParseHexColor(color);
    }
    if (const auto named = NamedColor(color)) {
      return named;
    }
    return ParseFunctionalColor(color);
  }
  if (value.IsNumber()) {
    const auto number = value.ToNumber();
    if (!number.Succeeded() || number.Value() < 0 || number.Value() > 16777215.0) {
      return std::nullopt;
    }
    const auto packed = static_cast<std::uint32_t>(number.Value());
    return Color::Rgb(
        static_cast<int>((packed >> 16) & 0xFFU),
        static_cast<int>((packed >> 8) & 0xFFU),
        static_cast<int>(packed & 0xFFU)
    );
  }
  if (value.IsArray()) {
    const auto red = ElementNumber(value, 0);
    const auto green = ElementNumber(value, 1);
    const auto blue = ElementNumber(value, 2);
    if (!red || !green || !blue) {
      return std::nullopt;
    }
    const auto alpha = ElementNumber(value, 3);
    return Color::Rgb(
        static_cast<int>(std::clamp(*red, 0.0, 255.0)),
        static_cast<int>(std::clamp(*green, 0.0, 255.0)),
        static_cast<int>(std::clamp(*blue, 0.0, 255.0)),
        static_cast<float>(std::clamp(alpha.value_or(1.0), 0.0, 1.0))
    );
  }
  if (value.IsObject()) {
    const auto red = MemberNumber(value, "r");
    const auto green = MemberNumber(value, "g");
    const auto blue = MemberNumber(value, "b");
    if (!red || !green || !blue) {
      return std::nullopt;
    }
    const auto alpha = MemberNumber(value, "a");
    return Color::Rgb(
        static_cast<int>(std::clamp(*red, 0.0, 255.0)),
        static_cast<int>(std::clamp(*green, 0.0, 255.0)),
        static_cast<int>(std::clamp(*blue, 0.0, 255.0)),
        static_cast<float>(std::clamp(alpha.value_or(1.0), 0.0, 1.0))
    );
  }
  return std::nullopt;
}

std::optional<EdgeInsets> ToEdgeInsets(const quickjs::Value& value) {
  if (value.IsNumber()) {
    const auto all = value.ToNumber();
    if (!all.Succeeded() || !std::isfinite(all.Value())) {
      return std::nullopt;
    }
    return EdgeInsets::All(static_cast<float>(all.Value()));
  }
  if (value.IsArray()) {
    const auto length = value.Length();
    if (!length.Succeeded()) {
      return std::nullopt;
    }
    if (length.Value() == 1) {
      const auto all = ElementNumber(value, 0);
      return all ? std::optional<EdgeInsets>(EdgeInsets::All(static_cast<float>(*all))) : std::nullopt;
    }
    if (length.Value() == 2) {
      const auto horizontal = ElementNumber(value, 0);
      const auto vertical = ElementNumber(value, 1);
      if (!horizontal || !vertical) {
        return std::nullopt;
      }
      return EdgeInsets::Symmetric(static_cast<float>(*horizontal), static_cast<float>(*vertical));
    }
    if (length.Value() >= 4) {
      const auto left = ElementNumber(value, 0);
      const auto top = ElementNumber(value, 1);
      const auto right = ElementNumber(value, 2);
      const auto bottom = ElementNumber(value, 3);
      if (!left || !top || !right || !bottom) {
        return std::nullopt;
      }
      return EdgeInsets{
          .top = static_cast<float>(*top),
          .right = static_cast<float>(*right),
          .bottom = static_cast<float>(*bottom),
          .left = static_cast<float>(*left),
      };
    }
    return std::nullopt;
  }
  if (value.IsObject()) {
    const auto horizontal = MemberNumber(value, "horizontal");
    const auto vertical = MemberNumber(value, "vertical");
    if (horizontal || vertical) {
      return EdgeInsets::Symmetric(
          static_cast<float>(horizontal.value_or(0.0)),
          static_cast<float>(vertical.value_or(0.0))
      );
    }
    const auto left = MemberNumber(value, "left");
    const auto top = MemberNumber(value, "top");
    const auto right = MemberNumber(value, "right");
    const auto bottom = MemberNumber(value, "bottom");
    if (left || top || right || bottom) {
      return EdgeInsets{
          .top = static_cast<float>(top.value_or(0.0)),
          .right = static_cast<float>(right.value_or(0.0)),
          .bottom = static_cast<float>(bottom.value_or(0.0)),
          .left = static_cast<float>(left.value_or(0.0)),
      };
    }
  }
  return std::nullopt;
}

std::optional<CornerRadii> ToCornerRadii(const quickjs::Value& value) {
  if (value.IsNumber()) {
    const auto all = value.ToNumber();
    return all.Succeeded() ? std::optional<CornerRadii>(CornerRadii(static_cast<float>(all.Value()))) : std::nullopt;
  }
  if (value.IsArray()) {
    if (const auto uniform = ElementNumber(value, 0); uniform) {
      return CornerRadii(static_cast<float>(*uniform));
    }
    return std::nullopt;
  }
  return std::nullopt;
}

void ApplyKey(View& view, const quickjs::Value& node) {
  const auto key = Property(node, "key");
  if (!key || key->IsUndefined() || key->IsNull()) {
    return;
  }
  if (key->IsString()) {
    const auto text = key->ToString();
    if (text.Succeeded()) {
      view = std::move(view).Key(text.Value());
    }
    return;
  }
  if (key->IsNumber()) {
    const auto number = key->ToNumber();
    if (number.Succeeded() && std::isfinite(number.Value())) {
      view = std::move(view).Key(static_cast<std::int64_t>(number.Value()));
    }
  }
}

std::optional<Semantics> SemanticsFromProps(const quickjs::Value& props) {
  const auto object = Property(props, "semantics");
  if (!object || !object->IsObject()) {
    return std::nullopt;
  }
  Semantics semantics;
  if (const auto role = StringProperty(*object, "role")) {
    const auto name = Lowercase(*role);
    if (name == "generic") {
      semantics.role = SemanticRole::Generic;
    } else if (name == "text") {
      semantics.role = SemanticRole::Text;
    } else if (name == "heading") {
      semantics.role = SemanticRole::Heading;
    } else if (name == "image") {
      semantics.role = SemanticRole::Image;
    } else if (name == "button") {
      semantics.role = SemanticRole::Button;
    } else if (name == "link") {
      semantics.role = SemanticRole::Link;
    } else if (name == "checkbox") {
      semantics.role = SemanticRole::Checkbox;
    } else if (name == "radio" || name == "radiobutton") {
      semantics.role = SemanticRole::RadioButton;
    } else if (name == "switch") {
      semantics.role = SemanticRole::Switch;
    } else if (name == "slider") {
      semantics.role = SemanticRole::Slider;
    } else if (name == "progress" || name == "progressindicator") {
      semantics.role = SemanticRole::ProgressIndicator;
    } else if (name == "combobox") {
      semantics.role = SemanticRole::ComboBox;
    } else if (name == "textfield" || name == "textbox") {
      semantics.role = SemanticRole::TextField;
    } else if (name == "searchfield" || name == "search") {
      semantics.role = SemanticRole::SearchField;
    } else if (name == "tab") {
      semantics.role = SemanticRole::Tab;
    } else if (name == "tablist") {
      semantics.role = SemanticRole::TabList;
    } else if (name == "menu") {
      semantics.role = SemanticRole::Menu;
    } else if (name == "menuitem") {
      semantics.role = SemanticRole::MenuItem;
    } else if (name == "dialog") {
      semantics.role = SemanticRole::Dialog;
    } else if (name == "navigation") {
      semantics.role = SemanticRole::Navigation;
    } else if (name == "list") {
      semantics.role = SemanticRole::List;
    } else if (name == "listitem") {
      semantics.role = SemanticRole::ListItem;
    } else if (name == "grid") {
      semantics.role = SemanticRole::Grid;
    } else if (name == "gridcell") {
      semantics.role = SemanticRole::GridCell;
    } else if (name == "scrollview") {
      semantics.role = SemanticRole::ScrollView;
    } else if (name == "tree") {
      semantics.role = SemanticRole::Tree;
    } else if (name == "treeitem") {
      semantics.role = SemanticRole::TreeItem;
    } else if (name == "separator") {
      semantics.role = SemanticRole::Separator;
    }
  }
  const auto variant = [&object](const char* name) -> std::optional<StringVariant> {
    const auto value = Property(*object, name);
    return value ? StringVariantFromValue(*value) : std::nullopt;
  };
  if (auto label = variant("label")) {
    semantics.label = std::move(*label);
  }
  if (auto value = variant("value")) {
    semantics.value = std::move(*value);
  }
  if (auto placeholder = variant("placeholder")) {
    semantics.placeholder = std::move(*placeholder);
  }
  if (auto hint = variant("hint")) {
    semantics.hint = std::move(*hint);
  }
  if (auto error = variant("error")) {
    semantics.error = std::move(*error);
  }
  if (auto description = variant("stateDescription")) {
    semantics.state_description = std::move(*description);
  }
  if (const auto identifier = StringProperty(*object, "identifier")) {
    semantics.identifier = *identifier;
  }
  if (const auto checked = Property(*object, "checked")) {
    if (checked->IsBoolean()) {
      const auto value = checked->ToBoolean();
      if (value.Succeeded()) {
        semantics.checked = value.Value() ? SemanticCheckedState::Checked
                                          : SemanticCheckedState::Unchecked;
      }
    } else if (const auto name = checked->ToString(); name.Succeeded()) {
      const auto normalized = Lowercase(name.Value());
      if (normalized == "mixed") {
        semantics.checked = SemanticCheckedState::Mixed;
      } else if (normalized == "checked") {
        semantics.checked = SemanticCheckedState::Checked;
      } else if (normalized == "unchecked") {
        semantics.checked = SemanticCheckedState::Unchecked;
      }
    }
  }
  const auto boolean = [&object](const char* name) -> std::optional<bool> {
    const auto value = Property(*object, name);
    if (!value) {
      return std::nullopt;
    }
    const auto result = value->ToBoolean();
    return result.Succeeded() ? std::optional<bool>(result.Value()) : std::nullopt;
  };
  if (const auto selected = boolean("selected")) {
    semantics.selected = *selected;
  }
  if (const auto expanded = boolean("expanded")) {
    semantics.expanded = *expanded;
  }
  if (const auto busy = boolean("busy")) {
    semantics.busy = *busy;
  }
  if (const auto read_only = boolean("readOnly")) {
    semantics.read_only = *read_only;
  }
  if (const auto required = boolean("required")) {
    semantics.required = *required;
  }
  if (const auto invalid = boolean("invalid")) {
    semantics.invalid = *invalid;
  }
  if (const auto hidden = boolean("hidden")) {
    semantics.hidden = *hidden;
  }
  if (const auto level = NumberProperty(*object, "headingLevel");
      level && *level >= 1.0 && *level <= 6.0) {
    semantics.heading_level = static_cast<unsigned int>(*level);
  }
  if (const auto descendants = StringProperty(*object, "descendants")) {
    const auto name = Lowercase(*descendants);
    if (name == "exclude") {
      semantics.descendants = SemanticDescendantPolicy::Exclude;
    } else if (name == "preserve") {
      semantics.descendants = SemanticDescendantPolicy::Preserve;
    }
  }
  if (const auto live = StringProperty(*object, "liveRegion")) {
    const auto name = Lowercase(*live);
    if (name == "polite") {
      semantics.live_region = SemanticLiveRegion::Polite;
    } else if (name == "assertive") {
      semantics.live_region = SemanticLiveRegion::Assertive;
    } else if (name == "none") {
      semantics.live_region = SemanticLiveRegion::None;
    }
  }
  return semantics;
}

const char* FileTypeName(FileType type) noexcept {
  switch (type) {
    case FileType::File:
      return "file";
    case FileType::Directory:
      return "directory";
    case FileType::Other:
      return "other";
  }
  return "other";
}

const char* IoErrorName(IoErrorCode code) noexcept {
  switch (code) {
    case IoErrorCode::NotFound:
      return "notFound";
    case IoErrorCode::PermissionDenied:
      return "permissionDenied";
    case IoErrorCode::NotDirectory:
      return "notDirectory";
    case IoErrorCode::IsDirectory:
      return "isDirectory";
    case IoErrorCode::TooLarge:
      return "tooLarge";
    case IoErrorCode::InvalidEncoding:
      return "invalidEncoding";
    case IoErrorCode::Unsupported:
      return "unsupported";
    case IoErrorCode::Io:
      return "io";
    case IoErrorCode::AlreadyExists:
      return "alreadyExists";
    case IoErrorCode::Timeout:
      return "timeout";
    case IoErrorCode::NoSpace:
      return "noSpace";
  }
  return "io";
}

quickjs::Result<quickjs::Value> MakeFileDropOffer(
    quickjs::Context& context,
    const FileDropOffer& offer
) {
  auto object = context.NewObject();
  if (!object.Succeeded()) {
    return object;
  }
  if (offer.item_count) {
    static_cast<void>(object.Value().SetProperty(
        "count",
        context.Number(static_cast<double>(*offer.item_count))
    ));
  }
  auto types = context.NewArray();
  if (types.Succeeded()) {
    for (std::size_t index = 0; index < offer.content_types.size(); ++index) {
      const auto text = context.NewString(offer.content_types[index]);
      if (text.Succeeded()) {
        static_cast<void>(types.Value().SetElement(
            static_cast<std::uint32_t>(index),
            text.Value()
        ));
      }
    }
    static_cast<void>(object.Value().SetProperty("contentTypes", types.Value()));
  }
  return object;
}

quickjs::Result<quickjs::Value> MakeFileDropEvent(
    quickjs::Context& context,
    const FileDropEvent& event
) {
  auto object = context.NewObject();
  if (!object.Succeeded()) {
    return object;
  }
  static_cast<void>(object.Value().SetProperty("x", context.Number(event.position.x)));
  static_cast<void>(object.Value().SetProperty("y", context.Number(event.position.y)));
  static_cast<void>(object.Value().SetProperty(
      "windowX",
      context.Number(event.window_position.x)
  ));
  static_cast<void>(object.Value().SetProperty(
      "windowY",
      context.Number(event.window_position.y)
  ));
  return object;
}

quickjs::Result<quickjs::Value> MakeFileReferenceValue(
    quickjs::Context& context,
    const FileReference& reference
) {
  auto object = context.NewObject();
  if (!object.Succeeded()) {
    return object;
  }
  if (const auto text = context.NewString(reference.Name()); text.Succeeded()) {
    static_cast<void>(object.Value().SetProperty("name", text.Value()));
  }
  if (const auto size = reference.Size()) {
    static_cast<void>(object.Value().SetProperty(
        "size",
        context.Number(static_cast<double>(*size))
    ));
  }
  if (const auto content_type = reference.ContentType()) {
    if (const auto text = context.NewString(*content_type); text.Succeeded()) {
      static_cast<void>(object.Value().SetProperty("contentType", text.Value()));
    }
  }
  static_cast<void>(object.Value().SetProperty(
      "canWrite",
      context.Boolean(reference.CanWrite())
  ));
  if (const auto text = context.NewString(FileTypeName(reference.Type())); text.Succeeded()) {
    static_cast<void>(object.Value().SetProperty("type", text.Value()));
  }
  if (const auto file = reference.AsFile()) {
    if (const auto text = context.NewString(file->Path()); text.Succeeded()) {
      static_cast<void>(object.Value().SetProperty("path", text.Value()));
    }
  }
  return object;
}

quickjs::Result<quickjs::Value> MakeFileReferenceArray(
    quickjs::Context& context,
    const std::vector<FileReference>& references
) {
  auto array = context.NewArray();
  if (!array.Succeeded()) {
    return array;
  }
  for (std::size_t index = 0; index < references.size(); ++index) {
    const auto value = MakeFileReferenceValue(context, references[index]);
    if (value.Succeeded()) {
      static_cast<void>(array.Value().SetElement(
          static_cast<std::uint32_t>(index),
          value.Value()
      ));
    }
  }
  return array;
}

quickjs::Result<quickjs::Value> MakeIoErrorValue(
    quickjs::Context& context,
    const IoError& error
) {
  auto object = context.NewObject();
  if (!object.Succeeded()) {
    return object;
  }
  if (const auto text = context.NewString(IoErrorName(error.code)); text.Succeeded()) {
    static_cast<void>(object.Value().SetProperty("code", text.Value()));
  }
  if (const auto text = context.NewString(error.message); text.Succeeded()) {
    static_cast<void>(object.Value().SetProperty("message", text.Value()));
  }
  return object;
}

std::optional<FileDropOptions> FileDropOptionsFromProps(const quickjs::Value& props) {
  const auto value = Property(props, "fileDrop");
  if (!value) {
    return std::nullopt;
  }
  if (value->IsBoolean()) {
    const auto enabled = value->ToBoolean();
    if (!enabled.Succeeded() || !enabled.Value()) {
      return std::nullopt;
    }
    return FileDropOptions{};
  }
  if (!value->IsObject()) {
    return std::nullopt;
  }
  FileDropOptions options;
  const auto read_list = [&value](const char* name, std::vector<std::string>& output) {
    const auto list = Property(*value, name);
    if (!list || !list->IsArray()) {
      return;
    }
    const auto length = list->Length();
    if (!length.Succeeded()) {
      return;
    }
    for (std::int64_t index = 0; index < length.Value(); ++index) {
      const auto element = list->GetElement(static_cast<std::uint32_t>(index));
      if (!element.Succeeded()) {
        continue;
      }
      const auto text = element.Value().ToString();
      if (text.Succeeded()) {
        output.push_back(text.Value());
      }
    }
  };
  read_list("extensions", options.extensions);
  read_list("contentTypes", options.content_types);
  if (const auto multiple = BoolProperty(*value, "allowsMultiple")) {
    options.allows_multiple = *multiple;
  }
  return options;
}

View ApplyCommonProps(RuntimeImpl& runtime, View view, const quickjs::Value& props) {
  if (const auto spacing = NumberProperty(props, "spacing")) {
    view = AddModifier(std::move(view), Spacing(static_cast<float>(*spacing)));
  }
  if (const auto value = Property(props, "padding")) {
    if (const auto insets = ToEdgeInsets(*value)) {
      view = AddModifier(std::move(view), Padding(*insets));
    }
  }
  if (const auto value = Property(props, "margin")) {
    if (const auto insets = ToEdgeInsets(*value)) {
      // HuxerUI has no margin modifier; padding on a wrapper would change the
      // element itself, so margins are intentionally not translated.
      (void)insets;
    }
  }
  if (const auto value = Property(props, "frame")) {
    bool any = false;
    Frame frame;
    if (!value->IsObject()) {
      // A number is a uniform width/height shortcut.
      if (const auto number = value->ToNumber(); number.Succeeded()) {
        frame.width = static_cast<float>(number.Value());
        frame.height = static_cast<float>(number.Value());
        any = true;
      }
    } else {
      if (const auto number = MemberNumber(*value, "width")) {
        frame.width = static_cast<float>(*number);
        any = true;
      }
      if (const auto number = MemberNumber(*value, "height")) {
        frame.height = static_cast<float>(*number);
        any = true;
      }
      if (const auto number = MemberNumber(*value, "minWidth")) {
        frame.min_width = static_cast<float>(*number);
        any = true;
      }
      if (const auto number = MemberNumber(*value, "maxWidth")) {
        frame.max_width = static_cast<float>(*number);
        any = true;
      }
      if (const auto number = MemberNumber(*value, "minHeight")) {
        frame.min_height = static_cast<float>(*number);
        any = true;
      }
      if (const auto number = MemberNumber(*value, "maxHeight")) {
        frame.max_height = static_cast<float>(*number);
        any = true;
      }
    }
    if (any) {
      view = AddModifier(std::move(view), frame);
    }
  }
  Frame shorthand;
  bool shorthand_any = false;
  if (const auto number = NumberProperty(props, "width")) {
    shorthand.width = static_cast<float>(*number);
    shorthand_any = true;
  }
  if (const auto number = NumberProperty(props, "height")) {
    shorthand.height = static_cast<float>(*number);
    shorthand_any = true;
  }
  if (shorthand_any) {
    view = AddModifier(std::move(view), shorthand);
  }
  if (const auto value = Property(props, "background")) {
    if (const auto color = ToColor(*value)) {
      view = AddModifier(std::move(view), Background(VisualFill(*color)));
    }
  }
  if (const auto value = Property(props, "foreground")) {
    if (const auto color = ToColor(*value)) {
      view = AddModifier(std::move(view), Foreground(*color));
    }
  }
  if (const auto value = Property(props, "border")) {
    if (const auto color = ToColor(*value)) {
      view = AddModifier(std::move(view), Border{.color = *color, .width = 1.0F});
    } else if (value->IsObject()) {
      Color color = Color::Rgb(120, 120, 130);
      float width = 1.0F;
      if (const auto member = Property(*value, "color")) {
        if (const auto parsed = ToColor(*member)) {
          color = *parsed;
        }
      }
      if (const auto number = MemberNumber(*value, "width")) {
        width = static_cast<float>(*number);
      }
      view = AddModifier(std::move(view), Border{.color = color, .width = width});
    }
  }
  if (const auto value = Property(props, "cornerRadius")) {
    if (const auto radii = ToCornerRadii(*value)) {
      view = AddModifier(std::move(view), CornerRadius(*radii));
    }
  }
  if (const auto value = Property(props, "shadow")) {
    if (value->IsObject()) {
      Shadow shadow;
      if (const auto color = Property(*value, "color")) {
        if (const auto parsed = ToColor(*color)) {
          shadow.color = *parsed;
        }
      }
      if (const auto offset = Property(*value, "offset")) {
        if (const auto x = ElementNumber(*offset, 0); x) {
          shadow.offset.x = static_cast<float>(*x);
        }
        if (const auto y = ElementNumber(*offset, 1); y) {
          shadow.offset.y = static_cast<float>(*y);
        }
      }
      if (const auto number = MemberNumber(*value, "blurRadius")) {
        shadow.blur_radius = static_cast<float>(*number);
      }
      if (const auto number = MemberNumber(*value, "spread")) {
        shadow.spread = static_cast<float>(*number);
      }
      view = AddModifier(std::move(view), shadow);
    }
  }
  if (const auto value = Property(props, "clipChildren")) {
    if (const auto enabled = value->ToBoolean(); enabled.Succeeded() && enabled.Value()) {
      view = AddModifier(std::move(view), ClipChildren());
    }
  }
  if (const auto value = Property(props, "enabled")) {
    if (const auto enabled = value->ToBoolean(); enabled.Succeeded()) {
      view = AddModifier(std::move(view), Enabled{.value = enabled.Value()});
    }
  }
  if (const auto value = Property(props, "focusable")) {
    if (const auto enabled = value->ToBoolean(); enabled.Succeeded()) {
      view = AddModifier(std::move(view), Focusable{.value = enabled.Value()});
    }
  }
  if (const auto value = Property(props, "mainAlign")) {
    if (const auto alignment = ParseMainAlign(AlignName(*value))) {
      view = AddModifier(std::move(view), MainAlign(*alignment));
    }
  }
  if (const auto value = Property(props, "crossAlign")) {
    if (const auto alignment = ParseCrossAlign(AlignName(*value))) {
      view = AddModifier(std::move(view), CrossAlign(*alignment));
    }
  }
  if (const auto value = Property(props, "align")) {
    if (value->IsString()) {
      const auto name = AlignName(*value);
      const auto horizontal = ParseHorizontalAlign(name);
      const auto vertical = ParseVerticalAlign(name);
      if (horizontal && vertical) {
        view = AddModifier(std::move(view), Align(*horizontal, *vertical));
      }
    } else if (value->IsObject()) {
      const auto horizontal = Property(*value, "horizontal");
      const auto vertical = Property(*value, "vertical");
      const auto horizontal_value = horizontal ? ParseHorizontalAlign(AlignName(*horizontal))
                                               : std::optional<HorizontalAlignment>(HorizontalAlignment::Center);
      const auto vertical_value = vertical ? ParseVerticalAlign(AlignName(*vertical))
                                           : std::optional<VerticalAlignment>(VerticalAlignment::Center);
      if (horizontal_value && vertical_value) {
        view = AddModifier(std::move(view), Align(*horizontal_value, *vertical_value));
      }
    }
  }
  if (const auto value = Property(props, "grow")) {
    if (value->IsBoolean()) {
      const auto enabled = value->ToBoolean();
      if (enabled.Succeeded() && enabled.Value()) {
        view = AddModifier(std::move(view), Grow());
      }
    } else if (const auto number = value->ToNumber(); number.Succeeded()) {
      view = AddModifier(std::move(view), Grow(static_cast<float>(number.Value())));
    }
  }
  if (const auto value = Property(props, "textAlign")) {
    (void)value; // Text consumes this property itself; ignore on other elements.
  }
  if (const auto semantics = SemanticsFromProps(props)) {
    view = std::move(view).With(*semantics);
  }
  if (const auto tooltip = StringProperty(props, "tooltip"); tooltip && !tooltip->empty()) {
    view = std::move(view).With(huxerui::Tooltip(*tooltip));
  }
  if (const auto drop = FileDropOptionsFromProps(props)) {
    view = std::move(view).With(FileDropTarget::Accepts(*drop));
    const std::string component = runtime.CurrentComponentPath();
    if (const auto handler = Property(props, "onFileDrop");
        handler && handler->IsFunction()) {
      view = std::move(view).On<FileDropEvents::Dropped>(
          [impl = &runtime, function = *handler, component](
              const std::vector<FileReference>& files,
              const FileDropEvent& event) {
            const auto array = MakeFileReferenceArray(impl->Context(), files);
            const auto point = MakeFileDropEvent(impl->Context(), event);
            if (array.Succeeded() && point.Succeeded()) {
              const quickjs::Value arguments[]{array.Value(), point.Value()};
              impl->InvokeHandler(
                  function,
                  arguments,
                  DiagnosticPhase::Event,
                  component
              );
            }
          }
      );
    }
    if (const auto handler = Property(props, "onFileDropEnter");
        handler && handler->IsFunction()) {
      view = std::move(view).On<FileDropEvents::Entered>(
          [impl = &runtime, function = *handler, component](
              const FileDropOffer& offer,
              const FileDropEvent& event) {
            const auto offer_value = MakeFileDropOffer(impl->Context(), offer);
            const auto point = MakeFileDropEvent(impl->Context(), event);
            if (offer_value.Succeeded() && point.Succeeded()) {
              const quickjs::Value arguments[]{offer_value.Value(), point.Value()};
              impl->InvokeHandler(
                  function,
                  arguments,
                  DiagnosticPhase::Event,
                  component
              );
            }
          }
      );
    }
    if (const auto handler = Property(props, "onFileDropMove");
        handler && handler->IsFunction()) {
      view = std::move(view).On<FileDropEvents::Moved>(
          [impl = &runtime, function = *handler, component](
              const FileDropOffer& offer,
              const FileDropEvent& event) {
            const auto offer_value = MakeFileDropOffer(impl->Context(), offer);
            const auto point = MakeFileDropEvent(impl->Context(), event);
            if (offer_value.Succeeded() && point.Succeeded()) {
              const quickjs::Value arguments[]{offer_value.Value(), point.Value()};
              impl->InvokeHandler(
                  function,
                  arguments,
                  DiagnosticPhase::Event,
                  component
              );
            }
          }
      );
    }
    if (const auto handler = Property(props, "onFileDropExit");
        handler && handler->IsFunction()) {
      view = std::move(view).On<FileDropEvents::Exited>(
          [impl = &runtime, function = *handler, component](
              const FileDropOffer& offer,
              const FileDropEvent& event) {
            const auto offer_value = MakeFileDropOffer(impl->Context(), offer);
            const auto point = MakeFileDropEvent(impl->Context(), event);
            if (offer_value.Succeeded() && point.Succeeded()) {
              const quickjs::Value arguments[]{offer_value.Value(), point.Value()};
              impl->InvokeHandler(
                  function,
                  arguments,
                  DiagnosticPhase::Event,
                  component
              );
            }
          }
      );
    }
    if (const auto handler = Property(props, "onFileDropFailed");
        handler && handler->IsFunction()) {
      view = std::move(view).On<FileDropEvents::Failed>(
          [impl = &runtime, function = *handler, component](
              const IoError& error,
              const FileDropEvent& event) {
            const auto error_value = MakeIoErrorValue(impl->Context(), error);
            const auto point = MakeFileDropEvent(impl->Context(), event);
            if (error_value.Succeeded() && point.Succeeded()) {
              const quickjs::Value arguments[]{error_value.Value(), point.Value()};
              impl->InvokeHandler(
                  function,
                  arguments,
                  DiagnosticPhase::Event,
                  component
              );
            }
          }
      );
    }
  }
  view = runtime.ApplyMenuAnchor(std::move(view), props);
  view = runtime.ApplyPopupAnchor(std::move(view), props);
  view = runtime.ApplyFocusRef(std::move(view), props);
  return ApplyEventProps(runtime, std::move(view), props);
}

} // namespace huxerui::tsx::detail
