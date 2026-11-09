cmake_minimum_required(VERSION 3.20)

if (NOT TARGET HuxerUI::testing)
    find_package(HuxerUI CONFIG REQUIRED COMPONENTS static testing
            PATHS "${HUXERUI_HOME}"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
    )
endif ()

if (TARGET HuxerUI::huxerui_static)
    set(HUXERUI_TSX_TEST_FRAMEWORK HuxerUI::huxerui_static)
else ()
    set(HUXERUI_TSX_TEST_FRAMEWORK HuxerUI::huxerui)
endif ()

add_executable(huxerui_tsx_tests tsx_tests.cpp)
target_compile_features(huxerui_tsx_tests PRIVATE cxx_std_20)
target_link_libraries(huxerui_tsx_tests PRIVATE
        HuxerUI::TSX
        ${HUXERUI_TSX_TEST_FRAMEWORK}
        HuxerUI::testing
)

if (MSVC)
    target_compile_options(huxerui_tsx_tests PRIVATE /UNDEBUG)
else ()
    target_compile_options(huxerui_tsx_tests PRIVATE -UNDEBUG)
endif ()

add_test(NAME huxerui_tsx_tests COMMAND huxerui_tsx_tests)
set_tests_properties(huxerui_tsx_tests PROPERTIES TIMEOUT 30)

add_subdirectory("${PROJECT_SOURCE_DIR}/quickjs/tests"
        "${CMAKE_CURRENT_BINARY_DIR}/quickjs-tests"
)

# Optional end-to-end check: bundle the example TSX with esbuild, then load
# the emitted module through the runtime. The vendored esbuild binary is used
# when present; no Node wrapper is involved.
include("${PROJECT_SOURCE_DIR}/cmake/HuxerUITsx.cmake")

set(HUXERUI_TSX_TEST_ESBUILD "" CACHE FILEPATH "esbuild binary used by the compiled-TSX test")
if (NOT HUXERUI_TSX_TEST_ESBUILD AND DEFINED ENV{HUXERUI_ESBUILD})
    set(HUXERUI_TSX_TEST_ESBUILD "$ENV{HUXERUI_ESBUILD}")
endif ()
if (NOT HUXERUI_TSX_TEST_ESBUILD)
    find_program(HUXERUI_TSX_TEST_ESBUILD esbuild)
endif ()
if (NOT HUXERUI_TSX_TEST_ESBUILD)
    _huxerui_tsx_vendored_esbuild(HUXERUI_TSX_TEST_ESBUILD)
endif ()

if (HUXERUI_TSX_TEST_ESBUILD)
    get_filename_component(HUXERUI_TSX_TEST_ESBUILD "${HUXERUI_TSX_TEST_ESBUILD}" ABSOLUTE
            BASE_DIR "${PROJECT_SOURCE_DIR}")
    set(HUXERUI_TSX_TEST_SOURCE "${PROJECT_SOURCE_DIR}/examples/counter/src/App.tsx")
    set(HUXERUI_TSX_TEST_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/compiled_app.js")

    add_test(NAME huxerui_tsx_compiled_bundle_build
            COMMAND "${HUXERUI_TSX_TEST_ESBUILD}"
                "${HUXERUI_TSX_TEST_SOURCE}"
                --bundle
                --format=esm
                --platform=neutral
                --jsx=automatic
                --jsx-import-source=@huxerui/tsx
                --target=es2020
                "--outfile=${HUXERUI_TSX_TEST_BUNDLE}"
                --log-level=warning
                "--external:@huxerui/tsx"
                "--external:@huxerui/tsx/jsx-runtime"
    )
    set_tests_properties(huxerui_tsx_compiled_bundle_build PROPERTIES
            FIXTURES_SETUP huxerui_tsx_bundle
            TIMEOUT 60
    )

    add_test(NAME huxerui_tsx_compiled_bundle
            COMMAND "${CMAKE_COMMAND}" -E env
                "HUXERUI_TSX_TEST_BUNDLE=${HUXERUI_TSX_TEST_BUNDLE}"
                "$<TARGET_FILE:huxerui_tsx_tests>"
    )
    set_tests_properties(huxerui_tsx_compiled_bundle PROPERTIES
            FIXTURES_REQUIRED huxerui_tsx_bundle
            TIMEOUT 60
    )
endif ()
