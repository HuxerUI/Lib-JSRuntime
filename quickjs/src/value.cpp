#include "internal.h"

#include <cstring>
#include <limits>
#include <utility>

namespace huxerui::quickjs {

namespace detail {

namespace {

static_assert(sizeof(JSValue) <= sizeof(std::uint64_t) * 2, "Value storage is too small for JSValue");

void MakeRaw(std::uint64_t (&words)[2], JSValue value) noexcept {
  std::memcpy(words, &value, sizeof(value));
}

} // namespace

JSContext* ValueAccess::ContextOf(const Value& value) noexcept {
  return static_cast<JSContext*>(value.context_);
}

JSValue ValueAccess::Raw(const Value& value) noexcept {
  if (!value.context_) {
    return JS_UNDEFINED;
  }
  JSValue raw = JS_UNDEFINED;
  std::memcpy(&raw, value.opaque_.words, sizeof(raw));
  return raw;
}

Value ValueAccess::Make(JSContext* context, JSValue value) noexcept {
  Value result(context, Value::Opaque{});
  MakeRaw(result.opaque_.words, value);
  return result;
}

JSValue ValueAccess::Release(Value& value) noexcept {
  if (!value.context_) {
    return JS_UNDEFINED;
  }
  const JSValue raw = Raw(value);
  value.context_ = nullptr;
  value.opaque_ = Value::Opaque{};
  return raw;
}

} // namespace detail

Value::Value() noexcept = default;

Value::Value(void* context, Opaque opaque) noexcept : context_(context), opaque_(opaque) {}

Value::Value(const Value& other) {
  context_ = other.context_;
  if (!context_) {
    opaque_ = other.opaque_;
    return;
  }
  opaque_ = Opaque{};
  detail::MakeRaw(opaque_.words, JS_DupValue(detail::ValueAccess::ContextOf(other), detail::ValueAccess::Raw(other)));
}

Value::Value(Value&& other) noexcept : context_(other.context_), opaque_(other.opaque_) {
  other.context_ = nullptr;
  other.opaque_ = Opaque{};
}

Value& Value::operator=(const Value& other) {
  if (this == &other) {
    return *this;
  }
  Value copy(other);
  *this = std::move(copy);
  return *this;
}

Value& Value::operator=(Value&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (context_) {
    JS_FreeValue(detail::ValueAccess::ContextOf(*this), detail::ValueAccess::Raw(*this));
  }
  context_ = other.context_;
  opaque_ = other.opaque_;
  other.context_ = nullptr;
  other.opaque_ = Opaque{};
  return *this;
}

Value::~Value() {
  if (context_) {
    JS_FreeValue(detail::ValueAccess::ContextOf(*this), detail::ValueAccess::Raw(*this));
  }
}

bool Value::IsUndefined() const noexcept {
  if (!context_) {
    return true;
  }
  return JS_IsUndefined(detail::ValueAccess::Raw(*this));
}

bool Value::IsNull() const noexcept {
  return context_ && JS_IsNull(detail::ValueAccess::Raw(*this));
}

bool Value::IsBoolean() const noexcept {
  return context_ && JS_IsBool(detail::ValueAccess::Raw(*this));
}

bool Value::IsNumber() const noexcept {
  return context_ && JS_IsNumber(detail::ValueAccess::Raw(*this));
}

bool Value::IsString() const noexcept {
  return context_ && JS_IsString(detail::ValueAccess::Raw(*this));
}

bool Value::IsBigInt() const noexcept {
  return context_ && JS_IsBigInt(detail::ValueAccess::Raw(*this));
}

bool Value::IsSymbol() const noexcept {
  return context_ && JS_IsSymbol(detail::ValueAccess::Raw(*this));
}

bool Value::IsObject() const noexcept {
  return context_ && JS_IsObject(detail::ValueAccess::Raw(*this));
}

bool Value::IsFunction() const noexcept {
  return context_ && JS_IsFunction(detail::ValueAccess::ContextOf(*this), detail::ValueAccess::Raw(*this));
}

bool Value::IsArray() const noexcept {
  return context_ && JS_IsArray(detail::ValueAccess::Raw(*this));
}

bool Value::IsPromise() const noexcept {
  return context_ && JS_IsPromise(detail::ValueAccess::Raw(*this));
}

bool Value::IsError() const noexcept {
  return context_ && JS_IsError(detail::ValueAccess::Raw(*this));
}

Result<bool> Value::ToBoolean() const {
  if (!context_) {
    return detail::Succeed(false);
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  const int result = JS_ToBool(context, detail::ValueAccess::Raw(*this));
  if (result < 0) {
    return Result<bool>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(result != 0);
}

Result<double> Value::ToNumber() const {
  if (!context_) {
    return detail::Succeed(std::numeric_limits<double>::quiet_NaN());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  double result = 0.0;
  if (JS_ToFloat64(context, &result, detail::ValueAccess::Raw(*this)) < 0) {
    return Result<double>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(result);
}

Result<std::int32_t> Value::ToInt32() const {
  if (!context_) {
    return detail::Succeed(std::int32_t{0});
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  std::int32_t result = 0;
  if (JS_ToInt32(context, &result, detail::ValueAccess::Raw(*this)) < 0) {
    return Result<std::int32_t>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(result);
}

Result<std::string> Value::ToString() const {
  if (!context_) {
    return detail::Succeed(std::string{"undefined"});
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  auto text = detail::ToUtf8(context, detail::ValueAccess::Raw(*this));
  if (!text) {
    return Result<std::string>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(std::move(*text));
}

Result<std::string> Value::ToJson(std::string_view indent) const {
  if (!context_) {
    return Result<std::string>::Failure(
        detail::TypeError("A context-free undefined value is not JSON serializable")
    );
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  JSValue space = JS_UNDEFINED;
  if (!indent.empty()) {
    space = detail::NewStringRaw(context, indent);
    if (JS_IsException(space)) {
      return Result<std::string>::Failure(detail::CaptureException(context));
    }
  }
  JSValue result = JS_JSONStringify(context, detail::ValueAccess::Raw(*this), JS_UNDEFINED, space);
  JS_FreeValue(context, space);
  if (JS_IsException(result)) {
    return Result<std::string>::Failure(detail::CaptureException(context));
  }
  if (!JS_IsString(result)) {
    JS_FreeValue(context, result);
    return Result<std::string>::Failure(
        detail::TypeError("The value has no JSON representation")
    );
  }
  auto text = detail::ToUtf8(context, result);
  JS_FreeValue(context, result);
  if (!text) {
    return Result<std::string>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(std::move(*text));
}

Result<Value> Value::GetProperty(std::string_view name) const {
  if (!context_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  const char* text = name.empty() ? "" : name.data();
  const JSAtom atom = JS_NewAtomLen(context, text, name.size());
  if (atom == JS_ATOM_NULL) {
    return Result<Value>::Failure(detail::CaptureException(context));
  }
  JSValue result = JS_GetProperty(context, detail::ValueAccess::Raw(*this), atom);
  JS_FreeAtom(context, atom);
  if (JS_IsException(result)) {
    return Result<Value>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(detail::ValueAccess::Make(context, result));
}

Result<Value> Value::GetElement(std::uint32_t index) const {
  if (!context_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  JSValue result = JS_GetPropertyUint32(context, detail::ValueAccess::Raw(*this), index);
  if (JS_IsException(result)) {
    return Result<Value>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(detail::ValueAccess::Make(context, result));
}

Result<void> Value::SetProperty(std::string_view name, Value value) const {
  if (!context_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  if (auto error = detail::ValidateValue(context, value)) {
    return Result<void>::Failure(std::move(*error));
  }
  const char* text = name.empty() ? "" : name.data();
  const JSAtom atom = JS_NewAtomLen(context, text, name.size());
  if (atom == JS_ATOM_NULL) {
    return Result<void>::Failure(detail::CaptureException(context));
  }
  const int result = JS_SetProperty(
      context,
      detail::ValueAccess::Raw(*this),
      atom,
      detail::DuplicateRaw(context, value)
  );
  JS_FreeAtom(context, atom);
  if (result < 0) {
    return Result<void>::Failure(detail::CaptureException(context));
  }
  return Result<void>::Success();
}

Result<void> Value::SetElement(std::uint32_t index, Value value) const {
  if (!context_) {
    return Result<void>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  if (auto error = detail::ValidateValue(context, value)) {
    return Result<void>::Failure(std::move(*error));
  }
  const int result = JS_SetPropertyUint32(
      context,
      detail::ValueAccess::Raw(*this),
      index,
      detail::DuplicateRaw(context, value)
  );
  if (result < 0) {
    return Result<void>::Failure(detail::CaptureException(context));
  }
  return Result<void>::Success();
}

Result<bool> Value::HasProperty(std::string_view name) const {
  if (!context_) {
    return Result<bool>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  const char* text = name.empty() ? "" : name.data();
  const JSAtom atom = JS_NewAtomLen(context, text, name.size());
  if (atom == JS_ATOM_NULL) {
    return Result<bool>::Failure(detail::CaptureException(context));
  }
  const int result = JS_HasProperty(context, detail::ValueAccess::Raw(*this), atom);
  JS_FreeAtom(context, atom);
  if (result < 0) {
    return Result<bool>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(result != 0);
}

Result<std::int64_t> Value::Length() const {
  if (!context_) {
    return Result<std::int64_t>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  std::int64_t length = 0;
  if (JS_GetLength(context, detail::ValueAccess::Raw(*this), &length) < 0) {
    return Result<std::int64_t>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(length);
}

Result<std::vector<std::string>> Value::PropertyNames(bool enumerable_only) const {
  if (!context_) {
    return Result<std::vector<std::string>>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  JSPropertyEnum* entries = nullptr;
  uint32_t count = 0;
  int flags = JS_GPN_STRING_MASK;
  if (enumerable_only) {
    flags |= JS_GPN_ENUM_ONLY;
  }
  if (JS_GetOwnPropertyNames(context, &entries, &count, detail::ValueAccess::Raw(*this), flags) < 0) {
    return Result<std::vector<std::string>>::Failure(detail::CaptureException(context));
  }

  std::vector<std::string> names;
  names.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    JSValue key = JS_AtomToString(context, entries[index].atom);
    if (JS_IsException(key)) {
      const Error error = detail::CaptureException(context);
      JS_FreePropertyEnum(context, entries, count);
      return Result<std::vector<std::string>>::Failure(error);
    }
    auto text = detail::ToUtf8(context, key);
    JS_FreeValue(context, key);
    if (!text) {
      const Error error = detail::CaptureException(context);
      JS_FreePropertyEnum(context, entries, count);
      return Result<std::vector<std::string>>::Failure(error);
    }
    names.push_back(std::move(*text));
  }
  JS_FreePropertyEnum(context, entries, count);
  return detail::Succeed(std::move(names));
}

Result<Value> Value::Call() const {
  return Call(std::span<const Value>{});
}

Result<Value> Value::Call(std::span<const Value> arguments) const {
  if (!context_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  return Call(Value{}, arguments);
}

Result<Value> Value::Call(Value this_value, std::span<const Value> arguments) const {
  if (!context_) {
    return Result<Value>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  if (!JS_IsFunction(context, detail::ValueAccess::Raw(*this))) {
    return Result<Value>::Failure(detail::TypeError("The value is not callable"));
  }
  if (auto error = detail::ValidateValue(context, this_value)) {
    return Result<Value>::Failure(std::move(*error));
  }

  std::vector<JSValue> raw_arguments;
  raw_arguments.reserve(arguments.size());
  for (const Value& argument : arguments) {
    if (auto error = detail::ValidateValue(context, argument)) {
      return Result<Value>::Failure(std::move(*error));
    }
    raw_arguments.push_back(detail::ValueAccess::Raw(argument));
  }

  JSValue result = JS_Call(
      context,
      detail::ValueAccess::Raw(*this),
      detail::ValueAccess::Raw(this_value),
      static_cast<int>(raw_arguments.size()),
      raw_arguments.data()
  );
  if (JS_IsException(result)) {
    return Result<Value>::Failure(detail::CaptureException(context));
  }
  return detail::Succeed(detail::ValueAccess::Make(context, result));
}

Result<bool> Value::StrictEquals(const Value& other) const {
  if (!context_) {
    return Result<bool>::Failure(detail::ClosedError());
  }
  JSContext* context = detail::ValueAccess::ContextOf(*this);
  if (auto error = detail::ValidateValue(context, other)) {
    return Result<bool>::Failure(std::move(*error));
  }
  return detail::Succeed(JS_IsStrictEqual(
      context,
      detail::ValueAccess::Raw(*this),
      detail::ValueAccess::Raw(other)
  ));
}

} // namespace huxerui::quickjs
