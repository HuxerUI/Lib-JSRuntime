#include "internal.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <utility>

namespace huxerui::quickjs {

namespace detail {

namespace {

std::optional<std::string> ReadStringProperty(
    JSContext* context,
    JSValueConst object,
    const char* name
) {
  JSValue value = JS_GetPropertyStr(context, object, name);
  if (JS_IsException(value)) {
    JS_FreeValue(context, JS_GetException(context));
    return std::nullopt;
  }
  auto text = ToUtf8(context, value);
  if (!text && JS_HasException(context)) {
    JS_FreeValue(context, JS_GetException(context));
  }
  JS_FreeValue(context, value);
  return text;
}

bool IsDigit(char character) {
  return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool ParseLocationLine(
    std::string_view line,
    std::string& filename,
    int& source_line,
    int& source_column
) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
    line.remove_suffix(1);
  }
  if (!line.empty() && line.back() == ')') {
    line.remove_suffix(1);
  }

  std::size_t column_end = line.size();
  std::size_t column_start = column_end;
  while (column_start > 0 && IsDigit(line[column_start - 1])) {
    --column_start;
  }
  if (column_start == column_end || column_start == 0 || line[column_start - 1] != ':') {
    return false;
  }
  const int column = std::atoi(std::string(line.substr(column_start)).c_str());

  const std::size_t line_separator = column_start - 1;
  std::size_t line_start = line_separator;
  while (line_start > 0 && IsDigit(line[line_start - 1])) {
    --line_start;
  }
  if (line_start == line_separator || line_start == 0 || line[line_start - 1] != ':') {
    return false;
  }
  const int parsed_line = std::atoi(
      std::string(line.substr(line_start, line_separator - line_start)).c_str()
  );

  const std::size_t name_separator = line_start - 1;
  std::size_t name_start = 0;
  if (const std::size_t opening = line.rfind('(', name_separator); opening != std::string_view::npos) {
    name_start = opening + 1;
  } else {
    while (name_start < name_separator
           && std::isspace(static_cast<unsigned char>(line[name_start])) != 0) {
      ++name_start;
    }
    if (line.substr(name_start, 3) == "at ") {
      name_start += 3;
    }
  }
  while (name_start < name_separator && std::isspace(static_cast<unsigned char>(line[name_start])) != 0) {
    ++name_start;
  }
  std::string parsed_filename(line.substr(name_start, name_separator - name_start));
  if (parsed_filename.empty()) {
    return false;
  }

  filename = std::move(parsed_filename);
  source_line = parsed_line;
  source_column = column;
  return true;
}

bool ParseStackLocation(
    std::string_view stack,
    std::string& filename,
    int& source_line,
    int& source_column
) {
  std::size_t offset = 0;
  while (offset < stack.size()) {
    const std::size_t line_end = stack.find('\n', offset);
    const std::string_view line =
        stack.substr(offset, line_end == std::string_view::npos ? stack.size() - offset : line_end - offset);
    if (ParseLocationLine(line, filename, source_line, source_column)) {
      return true;
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    offset = line_end + 1;
  }
  return false;
}

ErrorCode MapErrorCode(std::string_view name, std::string_view message) {
  if (name == "SyntaxError") {
    return ErrorCode::Syntax;
  }
  if (message.find("out of memory") != std::string_view::npos) {
    return ErrorCode::OutOfMemory;
  }
  if (message.find("stack overflow") != std::string_view::npos
      || message.find("Maximum call stack size exceeded") != std::string_view::npos) {
    return ErrorCode::StackOverflow;
  }
  if (message.find("interrupted") != std::string_view::npos) {
    return ErrorCode::Interrupted;
  }
  return ErrorCode::Exception;
}

} // namespace

JSValue NewStringRaw(JSContext* context, std::string_view value) {
  static constexpr char empty[] = "";
  return JS_NewStringLen(context, value.empty() ? empty : value.data(), value.size());
}

std::optional<std::string> ToUtf8(JSContext* context, JSValueConst value) {
  std::size_t length = 0;
  const char* text = JS_ToCStringLen(context, &length, value);
  if (!text) {
    return std::nullopt;
  }
  std::string result(text, length);
  JS_FreeCString(context, text);
  return result;
}

Error ErrorFromValue(JSContext* context, JSValueConst value, std::string filename) {
  std::string name;
  std::string message;
  std::string stack;

  if (JS_IsObject(value)) {
    if (auto property = ReadStringProperty(context, value, "name")) {
      name = std::move(*property);
    }
    if (auto property = ReadStringProperty(context, value, "message")) {
      message = std::move(*property);
    }
    if (auto property = ReadStringProperty(context, value, "stack")) {
      stack = std::move(*property);
    }
  }

  if (message.empty()) {
    if (auto text = ToUtf8(context, value)) {
      message = std::move(*text);
    } else {
      if (JS_HasException(context)) {
        JS_FreeValue(context, JS_GetException(context));
      }
      message = "Unknown JavaScript exception";
    }
  }
  if (message.empty()) {
    message = "JavaScript exception";
  }

  int source_line = 0;
  int source_column = 0;
  if (!stack.empty()) {
    ParseStackLocation(stack, filename, source_line, source_column);
  }

  return Error{
      MapErrorCode(name, message),
      std::move(message),
      std::move(filename),
      source_line,
      source_column,
      std::move(stack),
  };
}

Error CaptureException(JSContext* context, std::string filename) {
  JSValue exception = JS_GetException(context);
  Error error = ErrorFromValue(context, exception, std::move(filename));
  JS_FreeValue(context, exception);
  return error;
}

Error TakeContextError(ContextState& state, JSContext* context, std::string filename) {
  if (state.loader_error) {
    Error error = std::move(*state.loader_error);
    state.loader_error.reset();
    return error;
  }
  return CaptureException(context, std::move(filename));
}

Error ClosedError(std::string message) {
  return Error{ErrorCode::Closed, std::move(message)};
}

Error TypeError(std::string message) {
  return Error{ErrorCode::Type, std::move(message)};
}

std::optional<Error> ValidateValue(JSContext* context, const Value& value) {
  JSContext* value_context = ValueAccess::ContextOf(value);
  if (value_context && value_context != context) {
    return TypeError("The value belongs to a different JavaScript context");
  }
  return std::nullopt;
}

JSValue DuplicateRaw(JSContext* context, const Value& value) {
  if (!ValueAccess::ContextOf(value)) {
    return JS_UNDEFINED;
  }
  return JS_DupValue(context, ValueAccess::Raw(value));
}

} // namespace detail

Error::Error(
    ErrorCode code,
    std::string message,
    std::string filename,
    int line,
    int column,
    std::string stack
)
    : code_(code),
      message_(std::move(message)),
      filename_(std::move(filename)),
      line_(line),
      column_(column),
      stack_(std::move(stack)) {}

ErrorCode Error::Code() const noexcept {
  return code_;
}

const std::string& Error::Message() const noexcept {
  return message_;
}

const std::string& Error::Filename() const noexcept {
  return filename_;
}

int Error::Line() const noexcept {
  return line_;
}

int Error::Column() const noexcept {
  return column_;
}

const std::string& Error::Stack() const noexcept {
  return stack_;
}

} // namespace huxerui::quickjs
