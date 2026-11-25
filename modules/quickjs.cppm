module;

#include <typeinfo>
#include <huxerui/quickjs.h>

export module huxerui.jsruntime.quickjs;

export import huxerui;

export namespace huxerui::quickjs {
    using huxerui::quickjs::Context;
    using huxerui::quickjs::Error;
    using huxerui::quickjs::ErrorCode;
    using huxerui::quickjs::EvaluateOptions;
    using huxerui::quickjs::ModuleLoader;
    using huxerui::quickjs::NativeFunction;
    using huxerui::quickjs::Result;
    using huxerui::quickjs::Runtime;
    using huxerui::quickjs::RuntimeOptions;
    using huxerui::quickjs::Value;
}
