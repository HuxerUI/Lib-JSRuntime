cmake_minimum_required(VERSION 3.20)

# CMake initializes Apple deployment defaults during the first project() call.
# Explicit settings and toolchains retain ownership of their target platform.
if (CMAKE_HOST_APPLE AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        AND NOT CMAKE_TOOLCHAIN_FILE AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL ""
        AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target")
endif ()

project(huxerui_jsruntime VERSION 0.1.0 LANGUAGES NONE)

include("${CMAKE_CURRENT_SOURCE_DIR}/HuxerUIProject.cmake")
if (HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif ()

# QuickJS is compiled into HuxerUI::TSX itself. There is no separately built
# engine library and no sibling Lib-QuickJS checkout.
if (NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    enable_language(C)
endif ()

# The JavaScript bridge is shipped as a real ES module and embedded at configure
# time, so the library has no runtime asset dependency.
set(HUXERUI_TSX_RUNTIME_JS "${CMAKE_CURRENT_SOURCE_DIR}/js/runtime.js")
if (NOT EXISTS "${HUXERUI_TSX_RUNTIME_JS}")
    message(FATAL_ERROR "Missing embedded JavaScript runtime: ${HUXERUI_TSX_RUNTIME_JS}")
endif ()
file(READ "${HUXERUI_TSX_RUNTIME_JS}" HUXERUI_TSX_RUNTIME_SOURCE)
set(HUXERUI_TSX_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HUXERUI_TSX_GENERATED_DIR}")
file(WRITE "${HUXERUI_TSX_GENERATED_DIR}/embedded_js.h"
"// Generated from js/runtime.js. Do not edit.
#pragma once

#include <string_view>

namespace huxerui::tsx::detail {

inline constexpr std::string_view kTsxRuntimeModule = R\"HUXERUIJS(
${HUXERUI_TSX_RUNTIME_SOURCE}
)HUXERUIJS\";

} // namespace huxerui::tsx::detail
")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HUXERUI_TSX_RUNTIME_JS}")

file(GLOB_RECURSE LIBRARY_SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/src/*.cxx"
)

set(HUXERUI_QUICKJS_ENGINE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/third_party/quickjs/quickjs.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/third_party/quickjs/dtoa.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/third_party/quickjs/libregexp.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/third_party/quickjs/libunicode.c"
)
list(APPEND LIBRARY_SOURCE_FILES ${HUXERUI_QUICKJS_ENGINE_SOURCES})

huxerui_add_library(huxerui_tsx
        SOURCES
            ${LIBRARY_SOURCE_FILES}
)
add_library(HuxerUI::TSX ALIAS huxerui_tsx)
add_library(HuxerUI::JSRuntime ALIAS huxerui_tsx)
if (NOT TARGET HuxerUI::QuickJS)
    add_library(HuxerUI::QuickJS ALIAS huxerui_tsx)
endif ()

if (HUXERUI_LIBRARY_GRAPH_ONLY)
    return()
endif ()

target_include_directories(huxerui_tsx PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/quickjs/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_include_directories(huxerui_tsx PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${HUXERUI_TSX_GENERATED_DIR}"
)
target_include_directories(huxerui_tsx SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/quickjs/third_party/quickjs"
)

target_compile_features(huxerui_tsx PUBLIC cxx_std_20)
set_target_properties(huxerui_tsx PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON
)

if (WIN32)
    set_source_files_properties(${HUXERUI_QUICKJS_ENGINE_SOURCES}
            PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;_WIN32_WINNT=0x0601"
    )
else ()
    set_source_files_properties(${HUXERUI_QUICKJS_ENGINE_SOURCES}
            PROPERTIES COMPILE_DEFINITIONS "_GNU_SOURCE"
    )
endif ()

# Third-party engine sources are compiled as-is and do not follow the library
# warning policy.
if (MSVC)
    set_source_files_properties(${HUXERUI_QUICKJS_ENGINE_SOURCES}
            PROPERTIES COMPILE_OPTIONS "/W0"
    )
else ()
    set_source_files_properties(${HUXERUI_QUICKJS_ENGINE_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-w"
    )
endif ()

if (NOT WIN32)
    target_link_libraries(huxerui_tsx PRIVATE ${CMAKE_DL_LIBS})
endif ()
if (NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(huxerui_tsx PRIVATE m)
endif ()
if (NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(huxerui_tsx PRIVATE Threads::Threads)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(HUXERUI_TSX_BUILD_TESTS_DEFAULT ON)
else ()
    set(HUXERUI_TSX_BUILD_TESTS_DEFAULT OFF)
endif ()
option(HUXERUI_TSX_BUILD_TESTS "Build HuxerUI TSX bridge tests" ${HUXERUI_TSX_BUILD_TESTS_DEFAULT})

if (HUXERUI_TSX_BUILD_TESTS AND NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    enable_language(CXX)
    enable_testing()
    add_subdirectory(tests)
endif ()
