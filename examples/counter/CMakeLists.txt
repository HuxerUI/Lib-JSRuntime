cmake_minimum_required(VERSION 3.20)

project(huxerui_tsx_counter LANGUAGES CXX)

if (NOT TARGET HuxerUI::huxerui AND NOT TARGET HuxerUI::huxerui_static)
    if (NOT HUXERUI_HOME)
        set(HUXERUI_HOME "$ENV{HUXERUI_HOME}" CACHE PATH "HuxerUI SDK or source directory")
    endif ()
    if (HUXERUI_HOME AND EXISTS "${HUXERUI_HOME}/CMakeLists.txt"
            AND EXISTS "${HUXERUI_HOME}/include/huxerui/huxerui.h")
        set(HUXERUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(HUXERUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(HUXERUI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(HUXERUI_BUILD_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory("${HUXERUI_HOME}" "${CMAKE_BINARY_DIR}/huxerui-sdk" EXCLUDE_FROM_ALL)
    elseif (HUXERUI_HOME)
        find_package(HuxerUI CONFIG REQUIRED
                PATHS "${HUXERUI_HOME}"
                NO_DEFAULT_PATH
                NO_CMAKE_FIND_ROOT_PATH
        )
    else ()
        find_package(HuxerUI CONFIG REQUIRED)
    endif ()
endif ()

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
