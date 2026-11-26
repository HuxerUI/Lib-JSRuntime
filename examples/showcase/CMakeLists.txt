cmake_minimum_required(VERSION 3.20)

project(huxerui_tsx_showcase LANGUAGES CXX)

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

set(HUXERUI_TSX_SHOWCASE_ESBUILD "" CACHE FILEPATH
        "esbuild binary for the TSX showcase (defaults to the vendored binary)")
if (NOT HUXERUI_TSX_SHOWCASE_ESBUILD AND DEFINED ENV{HUXERUI_ESBUILD})
    set(HUXERUI_TSX_SHOWCASE_ESBUILD "$ENV{HUXERUI_ESBUILD}")
endif ()
if (NOT HUXERUI_TSX_SHOWCASE_ESBUILD)
    set(HUXERUI_TSX_SHOWCASE_ESBUILD "${HUXERUI_TSX_ESBUILD}")
endif ()
if (NOT HUXERUI_TSX_SHOWCASE_ESBUILD)
    find_program(HUXERUI_TSX_SHOWCASE_ESBUILD esbuild)
endif ()
if (NOT HUXERUI_TSX_SHOWCASE_ESBUILD)
    _huxerui_tsx_vendored_esbuild(HUXERUI_TSX_SHOWCASE_ESBUILD)
endif ()
if (NOT HUXERUI_TSX_SHOWCASE_ESBUILD)
    message(FATAL_ERROR
            "The TSX showcase needs esbuild. Set HUXERUI_TSX_SHOWCASE_ESBUILD or add a "
            "vendored binary under third_party/esbuild/.")
endif ()
get_filename_component(HUXERUI_TSX_SHOWCASE_ESBUILD
        "${HUXERUI_TSX_SHOWCASE_ESBUILD}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(HUXERUI_TSX_SHOWCASE_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/src/App.tsx")
set(HUXERUI_TSX_SHOWCASE_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/app.js")

huxerui_add_app(huxerui_tsx_showcase
        SOURCES
            src/main.cpp
)

huxerui_tsx_bundle("${HUXERUI_TSX_SHOWCASE_BUNDLE}"
        ENTRY "${HUXERUI_TSX_SHOWCASE_ENTRY}"
        ESBUILD "${HUXERUI_TSX_SHOWCASE_ESBUILD}"
)

add_custom_target(huxerui_tsx_showcase_bundle
        DEPENDS "${HUXERUI_TSX_SHOWCASE_BUNDLE}"
)
add_dependencies(huxerui_tsx_showcase huxerui_tsx_showcase_bundle)

target_compile_definitions(huxerui_tsx_showcase PRIVATE
        HUXERUI_TSX_BUNDLE_PATH="${HUXERUI_TSX_SHOWCASE_BUNDLE}"
)

huxerui_use_library(huxerui_tsx_showcase
        TARGET HuxerUI::TSX
        PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

# Separate watcher target. esbuild rebuilds app.js on every TSX save; the app
# reloads the layout when the file stamp changes.
add_custom_target(tsx_showcase_watch
        COMMAND "${HUXERUI_TSX_SHOWCASE_ESBUILD}"
            "${HUXERUI_TSX_SHOWCASE_ENTRY}"
            --bundle
            --format=esm
            --platform=neutral
            --jsx=automatic
            --jsx-import-source=@huxerui/tsx
            --target=es2020
            "--outfile=${HUXERUI_TSX_SHOWCASE_BUNDLE}"
            --log-level=warning
            "--external:@huxerui/tsx"
            "--external:@huxerui/tsx/jsx-runtime"
            --sourcemap=inline
            --sources-content=false
            --watch=forever
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Watching App.tsx with esbuild (Ctrl+C to stop)"
        USES_TERMINAL
)

# One-command debug session: starts the esbuild watcher and the native app
# together. Ctrl+C stops both. The app reloads the layout after each rebuild.
set(HUXERUI_TSX_SHOWCASE_DEV_TOOL
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/huxerui-tsx-dev.sh")

add_custom_target(tsx_showcase
        COMMAND "${HUXERUI_TSX_SHOWCASE_DEV_TOOL}"
            --entry "${HUXERUI_TSX_SHOWCASE_ENTRY}"
            --out "${HUXERUI_TSX_SHOWCASE_BUNDLE}"
            --app "$<TARGET_FILE:huxerui_tsx_showcase>"
            --esbuild "${HUXERUI_TSX_SHOWCASE_ESBUILD}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS huxerui_tsx_showcase
        COMMENT "Starting TSX showcase (app + esbuild watcher; Ctrl+C stops both)"
        USES_TERMINAL
)
