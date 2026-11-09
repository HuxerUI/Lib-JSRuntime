#include "props.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace huxerui::tsx::detail {
namespace {

using quickjs::Context;
using quickjs::Value;

[[noreturn]] void EventBuildFailure(std::string_view property, const quickjs::Error& error) {
  throw std::runtime_error(
      "could not build " + std::string(property) + " event: " + error.Message()
  );
}

Value StringValue(Context& context, std::string_view value) {
  const auto result = context.NewString(value);
  if (!result.Succeeded()) {
    EventBuildFailure("string", result.Error());
  }
  return result.Value();
}

Value ObjectValue(Context& context) {
  const auto result = context.NewObject();
  if (!result.Succeeded()) {
    EventBuildFailure("object", result.Error());
  }
  return result.Value();
}

void Set(Value& object, std::string_view name, Value value) {
  const auto result = object.SetProperty(name, std::move(value));
  if (!result.Succeeded()) {
    EventBuildFailure(name, result.Error());
  }
}

void SetString(Context& context, Value& object, std::string_view name, std::string_view value) {
  Set(object, name, StringValue(context, value));
}

void SetNumber(Context& context, Value& object, std::string_view name, double value) {
  Set(object, name, context.Number(value));
}

void SetBool(Context& context, Value& object, std::string_view name, bool value) {
  Set(object, name, context.Boolean(value));
}

Value MakeModifiers(Context& context, const KeyModifiers& modifiers) {
  auto object = ObjectValue(context);
  SetBool(context, object, "shift", modifiers.shift);
  SetBool(context, object, "control", modifiers.control);
  SetBool(context, object, "alt", modifiers.alt);
  SetBool(context, object, "meta", modifiers.meta);
  return object;
}

const char* PointerTypeName(PointerEventType type) noexcept {
  switch (type) {
    case PointerEventType::Down:
      return "down";
    case PointerEventType::Up:
      return "up";
    case PointerEventType::Move:
      return "move";
    case PointerEventType::Cancel:
      return "cancel";
  }
  return "cancel";
}

const char* DeviceName(PointerDeviceKind device) noexcept {
  switch (device) {
    case PointerDeviceKind::Mouse:
      return "mouse";
    case PointerDeviceKind::Touch:
      return "touch";
    case PointerDeviceKind::Pen:
      return "pen";
  }
  return "mouse";
}

const char* HoverTypeName(HoverEventType type) noexcept {
  switch (type) {
    case HoverEventType::Enter:
      return "enter";
    case HoverEventType::Move:
      return "move";
    case HoverEventType::Leave:
      return "leave";
  }
  return "move";
}

const char* ButtonName(PointerButton button) noexcept {
  switch (button) {
    case PointerButton::None:
      return "none";
    case PointerButton::Primary:
      return "primary";
    case PointerButton::Secondary:
      return "secondary";
    case PointerButton::Middle:
      return "middle";
    case PointerButton::Back:
      return "back";
    case PointerButton::Forward:
      return "forward";
  }
  return "none";
}

const char* KeyName(Key key) noexcept {
  switch (key) {
    case Key::Unknown:
      return "unknown";
    case Key::Backspace:
      return "Backspace";
    case Key::Tab:
      return "Tab";
    case Key::Enter:
      return "Enter";
    case Key::Escape:
      return "Escape";
    case Key::Space:
      return " ";
    case Key::Insert:
      return "Insert";
    case Key::Delete:
      return "Delete";
    case Key::Home:
      return "Home";
    case Key::End:
      return "End";
    case Key::PageUp:
      return "PageUp";
    case Key::PageDown:
      return "PageDown";
    case Key::ArrowLeft:
      return "ArrowLeft";
    case Key::ArrowRight:
      return "ArrowRight";
    case Key::ArrowUp:
      return "ArrowUp";
    case Key::ArrowDown:
      return "ArrowDown";
    case Key::A:
      return "a";
    case Key::B:
      return "b";
    case Key::C:
      return "c";
    case Key::D:
      return "d";
    case Key::E:
      return "e";
    case Key::F:
      return "f";
    case Key::G:
      return "g";
    case Key::H:
      return "h";
    case Key::I:
      return "i";
    case Key::J:
      return "j";
    case Key::K:
      return "k";
    case Key::L:
      return "l";
    case Key::M:
      return "m";
    case Key::N:
      return "n";
    case Key::O:
      return "o";
    case Key::P:
      return "p";
    case Key::Q:
      return "q";
    case Key::R:
      return "r";
    case Key::S:
      return "s";
    case Key::T:
      return "t";
    case Key::U:
      return "u";
    case Key::V:
      return "v";
    case Key::W:
      return "w";
    case Key::X:
      return "x";
    case Key::Y:
      return "y";
    case Key::Z:
      return "z";
    case Key::Digit0:
      return "0";
    case Key::Digit1:
      return "1";
    case Key::Digit2:
      return "2";
    case Key::Digit3:
      return "3";
    case Key::Digit4:
      return "4";
    case Key::Digit5:
      return "5";
    case Key::Digit6:
      return "6";
    case Key::Digit7:
      return "7";
    case Key::Digit8:
      return "8";
    case Key::Digit9:
      return "9";
    case Key::Backquote:
      return "`";
    case Key::Minus:
      return "-";
    case Key::Equal:
      return "=";
    case Key::BracketLeft:
      return "[";
    case Key::BracketRight:
      return "]";
    case Key::Backslash:
      return "\\";
    case Key::Semicolon:
      return ";";
    case Key::Quote:
      return "'";
    case Key::Comma:
      return ",";
    case Key::Period:
      return ".";
    case Key::Slash:
      return "/";
    case Key::IntlBackslash:
      return "IntlBackslash";
    case Key::IntlRo:
      return "IntlRo";
    case Key::IntlYen:
      return "IntlYen";
    case Key::ShiftLeft:
    case Key::ShiftRight:
      return "Shift";
    case Key::ControlLeft:
    case Key::ControlRight:
      return "Control";
    case Key::AltLeft:
    case Key::AltRight:
      return "Alt";
    case Key::MetaLeft:
    case Key::MetaRight:
      return "Meta";
    case Key::CapsLock:
      return "CapsLock";
    case Key::NumLock:
      return "NumLock";
    case Key::ScrollLock:
      return "ScrollLock";
    case Key::F1:
      return "F1";
    case Key::F2:
      return "F2";
    case Key::F3:
      return "F3";
    case Key::F4:
      return "F4";
    case Key::F5:
      return "F5";
    case Key::F6:
      return "F6";
    case Key::F7:
      return "F7";
    case Key::F8:
      return "F8";
    case Key::F9:
      return "F9";
    case Key::F10:
      return "F10";
    case Key::F11:
      return "F11";
    case Key::F12:
      return "F12";
    case Key::F13:
      return "F13";
    case Key::F14:
      return "F14";
    case Key::F15:
      return "F15";
    case Key::F16:
      return "F16";
    case Key::F17:
      return "F17";
    case Key::F18:
      return "F18";
    case Key::F19:
      return "F19";
    case Key::F20:
      return "F20";
    case Key::F21:
      return "F21";
    case Key::F22:
      return "F22";
    case Key::F23:
      return "F23";
    case Key::F24:
      return "F24";
    case Key::PrintScreen:
      return "PrintScreen";
    case Key::Pause:
      return "Pause";
    case Key::ContextMenu:
      return "ContextMenu";
    case Key::Help:
      return "Help";
    case Key::Numpad0:
      return "0";
    case Key::Numpad1:
      return "1";
    case Key::Numpad2:
      return "2";
    case Key::Numpad3:
      return "3";
    case Key::Numpad4:
      return "4";
    case Key::Numpad5:
      return "5";
    case Key::Numpad6:
      return "6";
    case Key::Numpad7:
      return "7";
    case Key::Numpad8:
      return "8";
    case Key::Numpad9:
      return "9";
    case Key::NumpadDecimal:
      return ".";
    case Key::NumpadDivide:
      return "/";
    case Key::NumpadMultiply:
      return "*";
    case Key::NumpadSubtract:
      return "-";
    case Key::NumpadAdd:
      return "+";
    case Key::NumpadEnter:
      return "Enter";
    case Key::NumpadEqual:
      return "=";
    case Key::NumpadComma:
      return ",";
    case Key::NumpadClear:
      return "Clear";
  }
  return "unknown";
}

std::optional<Value> Handler(const quickjs::Value& props, std::string_view name) {
  const auto value = Property(props, name);
  if (!value || !value->IsFunction()) {
    return std::nullopt;
  }
  return *value;
}

void Invoke(
    RuntimeImpl& runtime,
    const std::optional<Value>& handler,
    Value event,
    std::string_view component
) {
  if (!handler) {
    return;
  }
  const Value arguments[]{event};
  runtime.InvokeHandler(*handler, arguments, DiagnosticPhase::Event, component);
}

bool InvokeDecision(
    RuntimeImpl& runtime,
    const std::optional<Value>& handler,
    Value event,
    std::string_view component
) {
  if (!handler) {
    return false;
  }
  const Value arguments[]{event};
  return runtime.InvokeDecisionHandler(
      *handler,
      arguments,
      DiagnosticPhase::Event,
      component
  );
}

template <class Builder>
void InvokeBuilt(
    RuntimeImpl& runtime,
    const std::optional<Value>& handler,
    std::string_view component,
    Builder&& builder
) {
  if (!handler) {
    return;
  }
  try {
    Invoke(runtime, handler, builder(), component);
  } catch (const std::exception& error) {
    runtime.ReportMessage(
        DiagnosticPhase::Event,
        "pointer event",
        error.what(),
        component
    );
  }
}

template <class Builder>
bool InvokeDecisionBuilt(
    RuntimeImpl& runtime,
    const std::optional<Value>& handler,
    std::string_view component,
    Builder&& builder
) {
  if (!handler) {
    return false;
  }
  try {
    return InvokeDecision(runtime, handler, builder(), component);
  } catch (const std::exception& error) {
    runtime.ReportMessage(
        DiagnosticPhase::Event,
        "pointer event",
        error.what(),
        component
    );
    return false;
  }
}

} // namespace

Value MakePointerEvent(Context& context, const PointerEvent& event) {
  auto object = ObjectValue(context);
  SetString(context, object, "type", PointerTypeName(event.type));
  SetNumber(context, object, "pointerId", static_cast<double>(event.pointer_id));
  SetNumber(context, object, "x", static_cast<double>(event.position.x));
  SetNumber(context, object, "y", static_cast<double>(event.position.y));
  SetString(context, object, "device", DeviceName(event.device_kind));
  SetString(context, object, "button", ButtonName(event.changed_button));
  SetNumber(context, object, "buttons", static_cast<double>(static_cast<std::uint32_t>(event.pressed_buttons)));
  Set(object, "modifiers", MakeModifiers(context, event.modifiers));
  return object;
}

Value MakeHoverEvent(Context& context, const HoverEvent& event) {
  auto object = ObjectValue(context);
  SetString(context, object, "type", HoverTypeName(event.type));
  SetNumber(context, object, "pointerId", static_cast<double>(event.pointer_id));
  SetNumber(context, object, "x", static_cast<double>(event.position.x));
  SetNumber(context, object, "y", static_cast<double>(event.position.y));
  SetNumber(context, object, "windowX", static_cast<double>(event.window_position.x));
  SetNumber(context, object, "windowY", static_cast<double>(event.window_position.y));
  SetString(context, object, "device", DeviceName(event.device_kind));
  return object;
}

Value MakeScrollEvent(Context& context, const ScrollInputEvent& event) {
  auto object = ObjectValue(context);
  SetNumber(context, object, "x", static_cast<double>(event.position.x));
  SetNumber(context, object, "y", static_cast<double>(event.position.y));
  SetNumber(context, object, "deltaX", static_cast<double>(event.delta_x));
  SetNumber(context, object, "deltaY", static_cast<double>(event.delta_y));
  Set(object, "modifiers", MakeModifiers(context, event.modifiers));
  return object;
}

Value MakeKeyEvent(Context& context, const KeyEvent& event) {
  auto object = ObjectValue(context);
  SetString(context, object, "type", event.type == KeyEventType::Down ? "down" : "up");
  SetString(context, object, "key", KeyName(event.key));
  SetString(context, object, "text", event.text);
  SetBool(context, object, "repeat", event.repeat);
  Set(object, "modifiers", MakeModifiers(context, event.modifiers));
  return object;
}

Value MakeContextMenuEvent(Context& context, Point position) {
  auto object = ObjectValue(context);
  SetNumber(context, object, "x", static_cast<double>(position.x));
  SetNumber(context, object, "y", static_cast<double>(position.y));
  return object;
}

Value MakeDragEvent(Context& context, const DragEvent& event) {
  auto object = ObjectValue(context);
  SetNumber(context, object, "pointerId", static_cast<double>(event.pointer_id));
  SetString(context, object, "device", DeviceName(event.device_kind));

  auto point = [&](std::string_view name, Point value) {
    auto coordinates = ObjectValue(context);
    SetNumber(context, coordinates, "x", static_cast<double>(value.x));
    SetNumber(context, coordinates, "y", static_cast<double>(value.y));
    Set(object, name, coordinates);
  };
  point("origin", event.origin);
  point("position", event.position);
  point("windowPosition", event.window_position);
  point("delta", event.delta);
  point("translation", event.translation);
  point("velocity", event.velocity);
  return object;
}

Value MakeLongPressEvent(Context& context, const LongPressEvent& event) {
  auto object = ObjectValue(context);
  SetNumber(context, object, "pointerId", static_cast<double>(event.pointer_id));
  SetString(context, object, "device", DeviceName(event.device_kind));
  auto position = ObjectValue(context);
  SetNumber(context, position, "x", static_cast<double>(event.position.x));
  SetNumber(context, position, "y", static_cast<double>(event.position.y));
  Set(object, "position", position);
  auto window_position = ObjectValue(context);
  SetNumber(context, window_position, "x", static_cast<double>(event.window_position.x));
  SetNumber(context, window_position, "y", static_cast<double>(event.window_position.y));
  Set(object, "windowPosition", window_position);
  return object;
}

Value MakeMultiTapEvent(Context& context, const MultiTapEvent& event) {
  auto object = ObjectValue(context);
  SetNumber(context, object, "pointerId", static_cast<double>(event.pointer_id));
  SetString(context, object, "device", DeviceName(event.device_kind));
  auto position = ObjectValue(context);
  SetNumber(context, position, "x", static_cast<double>(event.position.x));
  SetNumber(context, position, "y", static_cast<double>(event.position.y));
  Set(object, "position", position);
  auto window_position = ObjectValue(context);
  SetNumber(context, window_position, "x", static_cast<double>(event.window_position.x));
  SetNumber(context, window_position, "y", static_cast<double>(event.window_position.y));
  Set(object, "windowPosition", window_position);
  SetNumber(context, object, "count", static_cast<double>(event.count));
  return object;
}

Value MakeTextEditingEvent(Context& context, const TextEditingValue& value) {
  auto object = ObjectValue(context);
  SetString(context, object, "text", value.text);
  auto selection = ObjectValue(context);
  SetNumber(context, selection, "anchor", static_cast<double>(value.selection.anchor));
  SetNumber(context, selection, "active", static_cast<double>(value.selection.active));
  SetString(
      context,
      selection,
      "affinity",
      value.selection.affinity == TextAffinity::Upstream ? "upstream" : "downstream"
  );
  Set(object, "selection", selection);
  if (value.composition) {
    auto composition = ObjectValue(context);
    SetNumber(context, composition, "start", static_cast<double>(value.composition->start));
    SetNumber(context, composition, "end", static_cast<double>(value.composition->end));
    Set(object, "composition", composition);
  } else {
    Set(object, "composition", context.Null());
  }
  return object;
}

View ApplyEventProps(RuntimeImpl& runtime, View view, const quickjs::Value& props) {
  const std::string component = runtime.CurrentComponentPath();
  const auto click = Handler(props, "onClick");
  if (click) {
    view = std::move(view).OnClick([impl = &runtime, handler = *click, component] {
      impl->InvokeHandler(handler, {}, DiagnosticPhase::Event, component);
    });
  }

  const auto pointer = Handler(props, "onPointer");
  const auto pointer_down = Handler(props, "onPointerDown");
  const auto pointer_move = Handler(props, "onPointerMove");
  const auto pointer_up = Handler(props, "onPointerUp");
  const auto pointer_cancel = Handler(props, "onPointerCancel");
  if (pointer || pointer_down || pointer_move || pointer_up || pointer_cancel) {
    view = std::move(view).On<ViewEvents::Pointer>(
        [impl = &runtime,
         pointer,
         pointer_down,
         pointer_move,
         pointer_up,
         pointer_cancel,
         component](const PointerEvent& event) {
          try {
            const auto value = MakePointerEvent(impl->Context(), event);
            Invoke(*impl, pointer, value, component);
            switch (event.type) {
              case PointerEventType::Down:
                Invoke(*impl, pointer_down, value, component);
                break;
              case PointerEventType::Move:
                Invoke(*impl, pointer_move, value, component);
                break;
              case PointerEventType::Up:
                Invoke(*impl, pointer_up, value, component);
                break;
              case PointerEventType::Cancel:
                Invoke(*impl, pointer_cancel, value, component);
                break;
            }
          } catch (const std::exception& error) {
            impl->ReportMessage(
                DiagnosticPhase::Event,
                "pointer event",
                error.what(),
                component
            );
          }
        }
    );
  }

  const auto pointer_intercept = Handler(props, "onPointerIntercept");
  if (pointer_intercept) {
    view = std::move(view).On<ViewEvents::PointerIntercept>(
        [impl = &runtime, handler = *pointer_intercept, component](const PointerEvent& event) {
          try {
            return InvokeDecision(
                *impl,
                handler,
                MakePointerEvent(impl->Context(), event),
                component
            );
          } catch (const std::exception& error) {
            impl->ReportMessage(
                DiagnosticPhase::Event,
                "pointer event",
                error.what(),
                component
            );
            return false;
          }
        }
    );
  }

  const auto hover = Handler(props, "onHover");
  const auto pointer_enter = Handler(props, "onPointerEnter");
  const auto pointer_leave = Handler(props, "onPointerLeave");
  if (hover || pointer_enter || pointer_leave) {
    view = std::move(view).On<ViewEvents::Hover>(
        [impl = &runtime,
         hover,
         pointer_enter,
         pointer_leave,
         component](const HoverEvent& event) {
          try {
            const auto value = MakeHoverEvent(impl->Context(), event);
            Invoke(*impl, hover, value, component);
            if (event.type == HoverEventType::Enter) {
              Invoke(*impl, pointer_enter, value, component);
            } else if (event.type == HoverEventType::Leave) {
              Invoke(*impl, pointer_leave, value, component);
            }
          } catch (const std::exception& error) {
            impl->ReportMessage(
                DiagnosticPhase::Event,
                "hover event",
                error.what(),
                component
            );
          }
        }
    );
  }

  const auto scroll = Handler(props, "onScroll");
  if (scroll) {
    view = std::move(view).On<ViewEvents::ScrollInput>(
        [impl = &runtime, handler = *scroll, component](const ScrollInputEvent& event) {
          try {
            return InvokeDecision(
                *impl,
                handler,
                MakeScrollEvent(impl->Context(), event),
                component
            );
          } catch (const std::exception& error) {
            impl->ReportMessage(
                DiagnosticPhase::Event,
                "scroll event",
                error.what(),
                component
            );
            return false;
          }
        }
    );
  }

  const auto key_down = Handler(props, "onKeyDown");
  if (key_down) {
    view = std::move(view).On<ViewEvents::KeyDown>(
        [impl = &runtime, handler = *key_down, component](const KeyEvent& event) {
          return InvokeDecisionBuilt(
              *impl,
              handler,
              component,
              [impl, &event] {
                return MakeKeyEvent(impl->Context(), event);
              }
          );
        }
    );
  }

  const auto key_up = Handler(props, "onKeyUp");
  if (key_up) {
    view = std::move(view).On<ViewEvents::KeyUp>(
        [impl = &runtime, handler = *key_up, component](const KeyEvent& event) {
          return InvokeDecisionBuilt(
              *impl,
              handler,
              component,
              [impl, &event] {
                return MakeKeyEvent(impl->Context(), event);
              }
          );
        }
    );
  }

  const auto key_intercept = Handler(props, "onKeyIntercept");
  if (key_intercept) {
    view = std::move(view).On<ViewEvents::KeyIntercept>(
        [impl = &runtime, handler = *key_intercept, component](const KeyEvent& event) {
          return InvokeDecisionBuilt(
              *impl,
              handler,
              component,
              [impl, &event] {
                return MakeKeyEvent(impl->Context(), event);
              }
          );
        }
    );
  }

  const auto focus = Handler(props, "onFocus");
  const auto blur = Handler(props, "onBlur");
  const auto focus_changed = Handler(props, "onFocusChanged");
  if (focus || blur || focus_changed) {
    view = std::move(view).On<ViewEvents::FocusChanged>(
        [impl = &runtime, focus, blur, focus_changed, component](bool focused) {
          if (focus_changed) {
            const Value arguments[]{impl->Context().Boolean(focused)};
            impl->InvokeHandler(
                *focus_changed,
                arguments,
                DiagnosticPhase::Event,
                component
            );
          }
          if (focused) {
            Invoke(*impl, focus, impl->Context().Undefined(), component);
          } else {
            Invoke(*impl, blur, impl->Context().Undefined(), component);
          }
        }
    );
  }

  const auto context_menu = Handler(props, "onContextMenu");
  if (context_menu) {
    view = std::move(view).On<ViewEvents::ContextMenuRequested>(
        [impl = &runtime, handler = *context_menu, component](Point position) {
          InvokeBuilt(
              *impl,
              handler,
              component,
              [impl, position] {
                return MakeContextMenuEvent(impl->Context(), position);
              }
          );
        }
    );
  }

  const auto back = Handler(props, "onBack");
  if (back) {
    view = std::move(view).On<ViewEvents::BackRequested>(
        [impl = &runtime, handler = *back, component] {
          impl->InvokeHandler(handler, {}, DiagnosticPhase::Event, component);
        }
    );
  }

  const auto drag_start = Handler(props, "onDragStart");
  const auto drag = Handler(props, "onDrag");
  const auto drag_end = Handler(props, "onDragEnd");
  const auto drag_cancel = Handler(props, "onDragCancel");
  if (drag_start || drag || drag_end || drag_cancel) {
    DragGesture gesture;
    if (const auto axis = StringProperty(props, "dragAxis"); axis) {
      gesture.axis = *axis == "horizontal" ? Axis::Horizontal : Axis::Vertical;
    }
    if (const auto threshold = NumberProperty(props, "dragThreshold"); threshold) {
      gesture.minimum_distance = static_cast<float>(std::max(0.0, *threshold));
    }
    if (const auto delay = NumberProperty(props, "dragDelay"); delay) {
      gesture.minimum_press_duration = std::chrono::duration<double>(std::max(0.0, *delay));
    }
    view = std::move(view).With(std::move(gesture));

    if (drag_start) {
      view = std::move(view).On<DragEvents::Started>(
          [impl = &runtime, handler = *drag_start, component](const DragEvent& event) {
            try {
              const auto value = MakeDragEvent(impl->Context(), event);
              const quickjs::Value arguments[]{value};
              impl->InvokeHandler(handler, arguments, DiagnosticPhase::Event, component);
            } catch (const std::exception& error) {
              impl->ReportMessage(
                  DiagnosticPhase::Event,
                  "drag event",
                  error.what(),
                  component
              );
            }
          }
      );
    }
    if (drag) {
      view = std::move(view).On<DragEvents::Changed>(
          [impl = &runtime, handler = *drag, component](const DragEvent& event) {
            try {
              const auto value = MakeDragEvent(impl->Context(), event);
              const quickjs::Value arguments[]{value};
              impl->InvokeHandler(handler, arguments, DiagnosticPhase::Event, component);
            } catch (const std::exception& error) {
              impl->ReportMessage(
                  DiagnosticPhase::Event,
                  "drag event",
                  error.what(),
                  component
              );
            }
          }
      );
    }
    if (drag_end) {
      view = std::move(view).On<DragEvents::Ended>(
          [impl = &runtime, handler = *drag_end, component](const DragEvent& event) {
            try {
              const auto value = MakeDragEvent(impl->Context(), event);
              const quickjs::Value arguments[]{value};
              impl->InvokeHandler(handler, arguments, DiagnosticPhase::Event, component);
            } catch (const std::exception& error) {
              impl->ReportMessage(
                  DiagnosticPhase::Event,
                  "drag event",
                  error.what(),
                  component
              );
            }
          }
      );
    }
    if (drag_cancel) {
      view = std::move(view).On<DragEvents::Canceled>(
          [impl = &runtime, handler = *drag_cancel, component](const DragEvent& event) {
            try {
              const auto value = MakeDragEvent(impl->Context(), event);
              const quickjs::Value arguments[]{value};
              impl->InvokeHandler(handler, arguments, DiagnosticPhase::Event, component);
            } catch (const std::exception& error) {
              impl->ReportMessage(
                  DiagnosticPhase::Event,
                  "drag event",
                  error.what(),
                  component
              );
            }
          }
      );
    }
  }

  const auto long_press_start = Handler(props, "onLongPressStart");
  const auto long_press_end = Handler(props, "onLongPressEnd");
  const auto long_press_cancel = Handler(props, "onLongPressCancel");
  if (long_press_start || long_press_end || long_press_cancel) {
    LongPressGesture gesture;
    if (const auto duration = NumberProperty(props, "longPressDuration"); duration) {
      gesture.minimum_duration = std::chrono::duration<double>(std::max(0.0, *duration));
    }
    if (const auto movement = NumberProperty(props, "longPressMovement"); movement) {
      gesture.maximum_movement = static_cast<float>(std::max(0.0, *movement));
    }
    view = std::move(view).With(std::move(gesture));

    if (long_press_start) {
      view = std::move(view).On<LongPressEvents::Started>(
          [impl = &runtime, handler = *long_press_start, component](const LongPressEvent& event) {
            InvokeBuilt(
                *impl,
                handler,
                component,
                [impl, event] { return MakeLongPressEvent(impl->Context(), event); }
            );
          }
      );
    }
    if (long_press_end) {
      view = std::move(view).On<LongPressEvents::Ended>(
          [impl = &runtime, handler = *long_press_end, component](const LongPressEvent& event) {
            InvokeBuilt(
                *impl,
                handler,
                component,
                [impl, event] { return MakeLongPressEvent(impl->Context(), event); }
            );
          }
      );
    }
    if (long_press_cancel) {
      view = std::move(view).On<LongPressEvents::Canceled>(
          [impl = &runtime, handler = *long_press_cancel, component](const LongPressEvent& event) {
            InvokeBuilt(
                *impl,
                handler,
                component,
                [impl, event] { return MakeLongPressEvent(impl->Context(), event); }
            );
          }
      );
    }
  }

  const auto multi_tap = Handler(props, "onMultiTap");
  if (multi_tap) {
    MultiTapGesture gesture;
    if (const auto count = NumberProperty(props, "multiTapCount"); count) {
      gesture.count = static_cast<std::uint32_t>(std::max(2.0, *count));
    }
    if (const auto interval = NumberProperty(props, "multiTapInterval"); interval) {
      gesture.maximum_interval = std::chrono::duration<double>(std::max(0.0, *interval));
    }
    if (const auto movement = NumberProperty(props, "multiTapMovement"); movement) {
      gesture.maximum_movement = static_cast<float>(std::max(0.0, *movement));
    }
    view = std::move(view).With(std::move(gesture));
    view = std::move(view).On<MultiTapEvents::Recognized>(
        [impl = &runtime, handler = *multi_tap, component](const MultiTapEvent& event) {
          InvokeBuilt(
              *impl,
              handler,
              component,
              [impl, event] { return MakeMultiTapEvent(impl->Context(), event); }
          );
        }
    );
  }

  return view;
}

} // namespace huxerui::tsx::detail
