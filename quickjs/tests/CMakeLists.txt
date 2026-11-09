cmake_minimum_required(VERSION 3.20)

if (TARGET HuxerUI::huxerui_static)
    set(HUXERUI_QUICKJS_TEST_FRAMEWORK HuxerUI::huxerui_static)
else ()
    set(HUXERUI_QUICKJS_TEST_FRAMEWORK HuxerUI::huxerui)
endif ()

add_executable(huxerui_quickjs_tests
        quickjs_tests.cpp
)
target_compile_features(huxerui_quickjs_tests PRIVATE cxx_std_20)
target_link_libraries(huxerui_quickjs_tests PRIVATE
        HuxerUI::TSX
        ${HUXERUI_QUICKJS_TEST_FRAMEWORK}
)

if (MSVC)
    target_compile_options(huxerui_quickjs_tests PRIVATE /UNDEBUG)
else ()
    target_compile_options(huxerui_quickjs_tests PRIVATE -UNDEBUG)
endif ()

add_test(NAME huxerui_quickjs_tests COMMAND huxerui_quickjs_tests)
