cmake_minimum_required(VERSION 3.20)

project(huxerui_esbuild_smoke LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HuxerUITsx.cmake")

set(HUXERUI_ESBUILD_SMOKE_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/app.js")

huxerui_tsx_bundle("${HUXERUI_ESBUILD_SMOKE_BUNDLE}"
        ENTRY "${CMAKE_CURRENT_LIST_DIR}/App.tsx"
)

add_custom_target(esbuild_smoke ALL
        DEPENDS "${HUXERUI_ESBUILD_SMOKE_BUNDLE}"
)
