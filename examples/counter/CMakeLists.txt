cmake_minimum_required(VERSION 3.20)

project(huxerui_tsx_counter LANGUAGES CXX)

if (NOT HUXERUI_HOME)
    set(HUXERUI_HOME "$ENV{HUXERUI_HOME}" CACHE PATH "HuxerUI SDK or source directory")
endif ()

find_package(HuxerUI CONFIG REQUIRED
        PATHS "${HUXERUI_HOME}"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HuxerUITsx.cmake")

huxerui_add_app(huxerui_tsx_counter
        SOURCES
            src/main.cpp
)

set(HUXERUI_TSX_COUNTER_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/app.js")
huxerui_tsx_bundle("${HUXERUI_TSX_COUNTER_BUNDLE}"
        ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/src/App.tsx"
)
set(HUXERUI_TSX_COUNTER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/app_bundle.h")
huxerui_tsx_embed_header("${HUXERUI_TSX_COUNTER_HEADER}"
        "${HUXERUI_TSX_COUNTER_BUNDLE}"
        NAMESPACE counter
)

target_sources(huxerui_tsx_counter PRIVATE
        "${HUXERUI_TSX_COUNTER_BUNDLE}"
        "${HUXERUI_TSX_COUNTER_HEADER}"
)
target_include_directories(huxerui_tsx_counter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

huxerui_use_library(huxerui_tsx_counter
        TARGET HuxerUI::TSX
        PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
