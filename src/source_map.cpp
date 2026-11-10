#include "source_map.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace huxerui::tsx::detail {

namespace {

constexpr std::string_view kSourceMappingUrl = "sourceMappingURL=";
constexpr std::string_view kBase64Marker = ";base64,";
constexpr std::string_view kDataPrefix = "data:";

int Base64Value(char character) noexcept {
  if (character >= 'A' && character <= 'Z') {
    return character - 'A';
  }
  if (character >= 'a' && character <= 'z') {
    return character - 'a' + 26;
  }
  if (character >= '0' && character <= '9') {
    return character - '0' + 52;
  }
  if (character == '+') {
    return 62;
  }
  if (character == '/') {
    return 63;
  }
  return -1;
}

std::optional<std::string> DecodeBase64(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size() * 3 / 4);
  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const char character : text) {
    if (character == '=') {
      break;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    const int value = Base64Value(character);
    if (value < 0) {
      return std::nullopt;
    }
    accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<char>((accumulator >> static_cast<unsigned>(bits)) & 0xFFU));
      accumulator &= (1U << static_cast<unsigned>(bits)) - 1U;
    }
  }
  return decoded;
}

std::optional<std::string> ExtractInlineSourceMap(std::string_view bundle) {
  const auto marker = bundle.rfind(kSourceMappingUrl);
  if (marker == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view value = bundle.substr(marker + kSourceMappingUrl.size());
  if (const auto end = value.find_first_of("\r\n"); end != std::string_view::npos) {
    value = value.substr(0, end);
  }
  if (!value.starts_with(kDataPrefix)) {
    return std::nullopt;
  }
  const auto base64 = value.find(kBase64Marker);
  if (base64 == std::string_view::npos) {
    return std::nullopt;
  }
  return DecodeBase64(value.substr(base64 + kBase64Marker.size()));
}

void AppendUtf8(std::string& out, std::uint32_t code_point) {
  if (code_point <= 0x7FU) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

struct SourceMapFields {
  std::vector<std::string> sources;
  std::string source_root;
  std::string mappings;
};

/// Minimal JSON reader for the source-map fields the bridge consumes. Unknown
/// values are skipped structurally, including `sourcesContent` strings.
class JsonReader final {
public:
  explicit JsonReader(std::string_view text) noexcept : text_(text) {}

  bool Parse(SourceMapFields& fields) {
    SkipWhitespace();
    if (!Consume('{')) {
      return false;
    }
    SkipWhitespace();
    if (Consume('}')) {
      return AtEnd();
    }
    while (true) {
      std::string key;
      if (!ParseString(key)) {
        return false;
      }
      SkipWhitespace();
      if (!Consume(':')) {
        return false;
      }
      SkipWhitespace();
      if (key == "sources") {
        if (!ParseStringArray(fields.sources)) {
          return false;
        }
      } else if (key == "sourceRoot") {
        if (!ParseString(fields.source_root)) {
          return false;
        }
      } else if (key == "mappings") {
        if (!ParseString(fields.mappings)) {
          return false;
        }
      } else if (!SkipValue()) {
        return false;
      }
      SkipWhitespace();
      if (Consume(',')) {
        SkipWhitespace();
        continue;
      }
      return Consume('}') && AtEnd();
    }
  }

private:
  [[nodiscard]] bool AtEnd() {
    SkipWhitespace();
    return position_ >= text_.size();
  }

  bool Consume(char expected) {
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void SkipWhitespace() {
    while (position_ < text_.size()
           && std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
      ++position_;
    }
  }

  bool ParseHex4(std::uint32_t& value) {
    if (position_ + 4 > text_.size()) {
      return false;
    }
    std::uint32_t parsed = 0;
    for (int index = 0; index < 4; ++index) {
      const char character = text_[position_++];
      parsed <<= 4U;
      if (character >= '0' && character <= '9') {
        parsed |= static_cast<std::uint32_t>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        parsed |= static_cast<std::uint32_t>(character - 'a' + 10);
      } else if (character >= 'A' && character <= 'F') {
        parsed |= static_cast<std::uint32_t>(character - 'A' + 10);
      } else {
        return false;
      }
    }
    value = parsed;
    return true;
  }

  bool ParseString(std::string& out) {
    out.clear();
    if (!Consume('"')) {
      return false;
    }
    while (position_ < text_.size()) {
      const auto character = static_cast<unsigned char>(text_[position_++]);
      if (character == '"') {
        return true;
      }
      if (character < 0x20U) {
        return false;
      }
      if (character != '\\') {
        out.push_back(static_cast<char>(character));
        continue;
      }
      if (position_ >= text_.size()) {
        return false;
      }
      switch (text_[position_++]) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t code_point = 0;
          if (!ParseHex4(code_point)) {
            return false;
          }
          if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (position_ + 1 >= text_.size() || text_[position_] != '\\'
                || text_[position_ + 1] != 'u') {
              return false;
            }
            position_ += 2;
            std::uint32_t low = 0;
            if (!ParseHex4(low) || low < 0xDC00U || low > 0xDFFFU) {
              return false;
            }
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
          } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            return false;
          }
          AppendUtf8(out, code_point);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool ParseStringArray(std::vector<std::string>& out) {
    out.clear();
    if (!Consume('[')) {
      return false;
    }
    SkipWhitespace();
    if (Consume(']')) {
      return true;
    }
    while (true) {
      std::string value;
      if (!ParseString(value)) {
        return false;
      }
      out.push_back(std::move(value));
      SkipWhitespace();
      if (Consume(',')) {
        SkipWhitespace();
        continue;
      }
      return Consume(']');
    }
  }

  bool ConsumeLiteral(std::string_view literal) {
    if (text_.substr(position_, literal.size()) != literal) {
      return false;
    }
    position_ += literal.size();
    return true;
  }

  bool SkipValue() {
    SkipWhitespace();
    if (position_ >= text_.size()) {
      return false;
    }
    const char character = text_[position_];
    if (character == '"') {
      std::string ignored;
      return ParseString(ignored);
    }
    if (character == '{') {
      ++position_;
      SkipWhitespace();
      if (Consume('}')) {
        return true;
      }
      while (true) {
        std::string key;
        if (!ParseString(key)) {
          return false;
        }
        SkipWhitespace();
        if (!Consume(':')) {
          return false;
        }
        if (!SkipValue()) {
          return false;
        }
        SkipWhitespace();
        if (Consume(',')) {
          SkipWhitespace();
          continue;
        }
        return Consume('}');
      }
    }
    if (character == '[') {
      ++position_;
      SkipWhitespace();
      if (Consume(']')) {
        return true;
      }
      while (true) {
        if (!SkipValue()) {
          return false;
        }
        SkipWhitespace();
        if (Consume(',')) {
          SkipWhitespace();
          continue;
        }
        return Consume(']');
      }
    }
    if (character == 't') {
      return ConsumeLiteral("true");
    }
    if (character == 'f') {
      return ConsumeLiteral("false");
    }
    if (character == 'n') {
      return ConsumeLiteral("null");
    }
    if (character == '-' || (character >= '0' && character <= '9')) {
      ++position_;
      while (position_ < text_.size()) {
        const char next = text_[position_];
        const bool number_character = (next >= '0' && next <= '9') || next == '.'
            || next == 'e' || next == 'E' || next == '+' || next == '-';
        if (!number_character) {
          break;
        }
        ++position_;
      }
      return true;
    }
    return false;
  }

  std::string_view text_;
  std::size_t position_ = 0;
};

std::optional<int> DecodeVlq(std::string_view text, std::size_t& position) {
  std::uint32_t value = 0;
  int shift = 0;
  while (true) {
    if (position >= text.size()) {
      return std::nullopt;
    }
    const int digit = Base64Value(text[position++]);
    if (digit < 0) {
      return std::nullopt;
    }
    value |= static_cast<std::uint32_t>(digit & 31) << static_cast<unsigned>(shift);
    if ((digit & 32) == 0) {
      break;
    }
    shift += 5;
    if (shift > 30) {
      return std::nullopt;
    }
  }
  const bool negative = (value & 1U) != 0;
  const auto magnitude = static_cast<int>(value >> 1U);
  return negative ? -magnitude : magnitude;
}

bool BuildLines(
    std::string_view mappings,
    std::size_t source_count,
    std::vector<std::vector<SourceMap::Segment>>& lines
) {
  lines.clear();
  lines.emplace_back();
  std::int64_t source = 0;
  std::int64_t source_line = 0;
  std::int64_t source_column = 0;
  std::uint32_t generated_column = 0;
  std::size_t position = 0;
  while (position < mappings.size()) {
    const char character = mappings[position];
    if (character == ';') {
      lines.emplace_back();
      generated_column = 0;
      ++position;
      continue;
    }
    if (character == ',') {
      ++position;
      continue;
    }

    std::array<int, 5> fields{};
    std::size_t count = 0;
    while (position < mappings.size() && mappings[position] != ','
           && mappings[position] != ';') {
      if (count == fields.size()) {
        return false;
      }
      const auto field = DecodeVlq(mappings, position);
      if (!field) {
        return false;
      }
      fields[count++] = *field;
    }
    if (count == 0 || count == 2 || count == 3) {
      return false;
    }

    const std::int64_t next_column = static_cast<std::int64_t>(generated_column) + fields[0];
    if (next_column < 0) {
      return false;
    }
    generated_column = static_cast<std::uint32_t>(next_column);

    SourceMap::Segment segment;
    segment.generated_column = generated_column;
    if (count >= 4) {
      source += fields[1];
      source_line += fields[2];
      source_column += fields[3];
      if (source < 0 || source_line < 0 || source_column < 0) {
        return false;
      }
      segment.source = static_cast<std::uint32_t>(source);
      segment.line = static_cast<std::uint32_t>(source_line);
      segment.column = static_cast<std::uint32_t>(source_column);
      segment.mapped = segment.source < source_count;
    }
    lines.back().push_back(segment);
  }
  return true;
}

bool IsFrameBoundary(char character) noexcept {
  return character == '(' || character == ' ' || character == '\t' || character == '\n';
}

} // namespace

std::optional<SourceMap> SourceMap::FromBundle(std::string_view bundle) {
  const auto json = ExtractInlineSourceMap(bundle);
  if (!json) {
    return std::nullopt;
  }
  return FromJson(*json);
}

std::optional<SourceMap> SourceMap::FromJson(std::string_view json) {
  SourceMapFields fields;
  JsonReader reader(json);
  if (!reader.Parse(fields) || fields.mappings.empty() || fields.sources.empty()) {
    return std::nullopt;
  }

  SourceMap map;
  map.sources_ = std::move(fields.sources);
  map.source_root_ = std::move(fields.source_root);
  if (!BuildLines(fields.mappings, map.sources_.size(), map.generated_lines_)) {
    return std::nullopt;
  }
  return map;
}

std::optional<SourcePosition> SourceMap::Find(
    int line,
    int column,
    const std::filesystem::path& base
) const {
  if (line <= 0 || static_cast<std::size_t>(line) > generated_lines_.size()) {
    return std::nullopt;
  }
  const auto& segments = generated_lines_[static_cast<std::size_t>(line - 1)];
  if (segments.empty()) {
    return std::nullopt;
  }

  const auto target = static_cast<std::uint32_t>(column > 0 ? column - 1 : 0);
  const auto found = std::upper_bound(
      segments.begin(),
      segments.end(),
      target,
      [](std::uint32_t value, const Segment& segment) {
        return value < segment.generated_column;
      }
  );
  if (found == segments.begin()) {
    return std::nullopt;
  }
  const auto& segment = *std::prev(found);
  if (!segment.mapped || segment.source >= sources_.size()) {
    return std::nullopt;
  }

  std::filesystem::path path(sources_[segment.source]);
  if (path.is_relative() && !source_root_.empty()) {
    path = std::filesystem::path(source_root_) / path;
  }
  if (path.is_relative() && !base.empty()) {
    path = base / path;
  }
  return SourcePosition{
      path.lexically_normal().generic_string(),
      static_cast<int>(segment.line) + 1,
      static_cast<int>(segment.column) + 1,
  };
}

std::string RemapStack(
    std::string_view stack,
    std::string_view generated_filename,
    const SourceMap& source_map,
    const std::filesystem::path& base
) {
  if (stack.empty() || generated_filename.empty()) {
    return std::string(stack);
  }

  std::string remapped;
  remapped.reserve(stack.size());
  std::size_t cursor = 0;
  while (cursor < stack.size()) {
    const auto found = stack.find(generated_filename, cursor);
    if (found == std::string_view::npos) {
      remapped.append(stack.substr(cursor));
      break;
    }
    if (found > cursor && !IsFrameBoundary(stack[found - 1])) {
      remapped.append(stack.substr(cursor, found - cursor + generated_filename.size()));
      cursor = found + generated_filename.size();
      continue;
    }

    remapped.append(stack.substr(cursor, found - cursor));
    std::size_t after = found + generated_filename.size();
    if (after >= stack.size() || stack[after] != ':') {
      remapped.append(generated_filename);
      cursor = after;
      continue;
    }

    std::size_t line_start = after + 1;
    std::size_t line_end = line_start;
    while (line_end < stack.size() && std::isdigit(static_cast<unsigned char>(stack[line_end])) != 0) {
      ++line_end;
    }
    std::size_t column_end = line_end;
    if (line_end > line_start && line_end < stack.size() && stack[line_end] == ':') {
      column_end = line_end + 1;
      while (column_end < stack.size()
             && std::isdigit(static_cast<unsigned char>(stack[column_end])) != 0) {
        ++column_end;
      }
    }

    const bool has_location = line_end > line_start && column_end > line_end + 1;
    if (has_location) {
      const int line = std::atoi(std::string(stack.substr(line_start, line_end - line_start)).c_str());
      const int column = std::atoi(
          std::string(stack.substr(line_end + 1, column_end - line_end - 1)).c_str()
      );
      if (const auto mapped = source_map.Find(line, column, base)) {
        remapped.append(mapped->filename);
        remapped.push_back(':');
        remapped.append(std::to_string(mapped->line));
        remapped.push_back(':');
        remapped.append(std::to_string(mapped->column));
        cursor = column_end;
        continue;
      }
    }

    remapped.append(stack.substr(found, after - found));
    cursor = after;
  }
  return remapped;
}

} // namespace huxerui::tsx::detail
