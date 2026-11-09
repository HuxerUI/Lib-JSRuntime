include_guard(GLOBAL)

# Optional explicit esbuild binary. When empty, huxerui_tsx_bundle() checks
# HUXERUI_ESBUILD, PATH, then the platform binary vendored under
# third_party/esbuild/.
set(HUXERUI_TSX_ESBUILD "" CACHE FILEPATH
        "esbuild binary used to bundle TSX (defaults to the vendored binary)")
set(HUXERUI_TSX_ESBUILD_PLATFORM "" CACHE STRING
        "Override the vendored esbuild platform id (for example linux-arm64)")

# Maps the build host to the vendored esbuild package name.
function(_huxerui_tsx_host_platform out)
    if (HUXERUI_TSX_ESBUILD_PLATFORM)
        set("${out}" "${HUXERUI_TSX_ESBUILD_PLATFORM}" PARENT_SCOPE)
        return()
    endif ()

    set(os "")
    if (CMAKE_HOST_WIN32)
        set(os "win32")
    elseif (CMAKE_HOST_APPLE)
        set(os "darwin")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(os "linux")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "FreeBSD")
        set(os "freebsd")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "OpenBSD")
        set(os "openbsd")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "NetBSD")
        set(os "netbsd")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "SunOS")
        set(os "sunos")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "AIX")
        set(os "aix")
    endif ()

    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" processor)
    set(arch "")
    if (os STREQUAL "win32")
        if (processor MATCHES "^(amd64|x86_64)$")
            set(arch "x64")
        elseif (processor MATCHES "^(x86|i[3-6]86)$")
            set(arch "ia32")
        elseif (processor MATCHES "^(arm64|aarch64)$")
            set(arch "arm64")
        endif ()
    elseif (os STREQUAL "darwin")
        if (processor MATCHES "^(arm64|aarch64)$")
            set(arch "arm64")
        elseif (processor MATCHES "^(x86_64|amd64)$")
            set(arch "x64")
        endif ()
    elseif (os STREQUAL "linux")
        if (processor MATCHES "^(x86_64|amd64)$")
            set(arch "x64")
        elseif (processor MATCHES "^(i[3-6]86|x86)$")
            set(arch "ia32")
        elseif (processor MATCHES "^(aarch64|arm64)$")
            set(arch "arm64")
        elseif (processor MATCHES "^(armv7l|armv7|armv6l|armv8l|arm)$")
            set(arch "arm")
        elseif (processor MATCHES "^(ppc64le|ppc64|powerpc64|powerpc64le)$")
            set(arch "ppc64")
        elseif (processor STREQUAL "s390x")
            set(arch "s390x")
        elseif (processor MATCHES "^(loongarch64|loong64)$")
            set(arch "loong64")
        elseif (processor STREQUAL "riscv64")
            set(arch "riscv64")
        elseif (processor STREQUAL "mips64el")
            set(arch "mips64el")
        endif ()
    elseif (os STREQUAL "freebsd" OR os STREQUAL "openbsd" OR os STREQUAL "netbsd")
        if (processor MATCHES "^(x86_64|amd64)$")
            set(arch "x64")
        elseif (processor MATCHES "^(arm64|aarch64)$")
            set(arch "arm64")
        endif ()
    elseif (os STREQUAL "sunos")
        if (processor MATCHES "^(x86_64|amd64)$")
            set(arch "x64")
        endif ()
    elseif (os STREQUAL "aix")
        if (processor MATCHES "^(ppc64|powerpc64)$")
            set(arch "ppc64")
        endif ()
    endif ()

    if (os AND arch)
        set("${out}" "${os}-${arch}" PARENT_SCOPE)
    else ()
        set("${out}" "" PARENT_SCOPE)
    endif ()
endfunction()

# Returns the vendored esbuild path for the build host, or an empty string.
function(_huxerui_tsx_vendored_esbuild out)
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/esbuild")
    _huxerui_tsx_host_platform(triplet)

    set(suffix "")
    if (CMAKE_HOST_WIN32)
        set(suffix ".exe")
    endif ()
    set(candidate "${root}/${triplet}/esbuild${suffix}")
    if (triplet AND EXISTS "${candidate}")
        set("${out}" "${candidate}" PARENT_SCOPE)
    else ()
        set("${out}" "" PARENT_SCOPE)
    endif ()
endfunction()

# Bundles one TSX entry with esbuild into the ES module consumed by
# HuxerUI::TSX. The command is registered as a build output; list it in
# target_sources() or depend on it explicitly.
#
#   huxerui_tsx_bundle(${CMAKE_CURRENT_BINARY_DIR}/app.js
#       ENTRY src/App.tsx
#       ARGS --minify
#   )
function(huxerui_tsx_bundle output)
    cmake_parse_arguments(HUXERUI_TSX_BUNDLE
            ""
            "ENTRY;ESBUILD"
            "ARGS"
            ${ARGN}
    )
    if (HUXERUI_TSX_BUNDLE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "huxerui_tsx_bundle() received unknown arguments: ${HUXERUI_TSX_BUNDLE_UNPARSED_ARGUMENTS}")
    endif ()
    if (NOT HUXERUI_TSX_BUNDLE_ENTRY)
        message(FATAL_ERROR "huxerui_tsx_bundle() requires ENTRY")
    endif ()

    get_filename_component(HUXERUI_TSX_ENTRY "${HUXERUI_TSX_BUNDLE_ENTRY}" ABSOLUTE)
    get_filename_component(HUXERUI_TSX_OUTPUT "${output}" ABSOLUTE)
    get_filename_component(HUXERUI_TSX_OUTPUT_DIRECTORY "${HUXERUI_TSX_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${HUXERUI_TSX_OUTPUT_DIRECTORY}")

    set(HUXERUI_TSX_ESBUILD_BIN "${HUXERUI_TSX_BUNDLE_ESBUILD}")
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        set(HUXERUI_TSX_ESBUILD_BIN "${HUXERUI_TSX_ESBUILD}")
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN AND DEFINED ENV{HUXERUI_ESBUILD})
        set(HUXERUI_TSX_ESBUILD_BIN "$ENV{HUXERUI_ESBUILD}")
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        find_program(HUXERUI_TSX_ESBUILD_BIN esbuild)
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        _huxerui_tsx_vendored_esbuild(HUXERUI_TSX_ESBUILD_BIN)
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        message(FATAL_ERROR
                "huxerui_tsx_bundle() could not find esbuild. Set HUXERUI_TSX_ESBUILD "
                "to an esbuild binary, put esbuild on PATH, or add a vendored binary "
                "under third_party/esbuild/ for this host platform.")
    endif ()
    get_filename_component(HUXERUI_TSX_ESBUILD_BIN
            "${HUXERUI_TSX_ESBUILD_BIN}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    set(HUXERUI_TSX_COMMAND
            "${HUXERUI_TSX_ESBUILD_BIN}"
            "${HUXERUI_TSX_ENTRY}"
            --bundle
            --format=esm
            --platform=neutral
            --jsx=automatic
            --jsx-import-source=@huxerui/tsx
            --target=es2020
            "--outfile=${HUXERUI_TSX_OUTPUT}"
            --log-level=warning
            "--external:@huxerui/tsx"
            "--external:@huxerui/tsx/jsx-runtime"
    )
    list(APPEND HUXERUI_TSX_COMMAND ${HUXERUI_TSX_BUNDLE_ARGS})

    add_custom_command(
            OUTPUT "${HUXERUI_TSX_OUTPUT}"
            COMMAND ${HUXERUI_TSX_COMMAND}
            DEPENDS "${HUXERUI_TSX_ENTRY}" "${HUXERUI_TSX_ESBUILD_BIN}"
            COMMENT "Bundling TSX with esbuild: ${HUXERUI_TSX_ENTRY}"
            VERBATIM
    )
endfunction()

# Embeds a bundled JavaScript module into a C++ header as a string_view.
#
#   huxerui_tsx_embed_header(${CMAKE_CURRENT_BINARY_DIR}/app_bundle.h
#       ${CMAKE_CURRENT_BINARY_DIR}/app.js
#       NAMESPACE counter
#   )
function(huxerui_tsx_embed_header output input)
    cmake_parse_arguments(HUXERUI_TSX_EMBED
            ""
            "NAMESPACE"
            ""
            ${ARGN}
    )
    if (NOT HUXERUI_TSX_EMBED_NAMESPACE)
        set(HUXERUI_TSX_EMBED_NAMESPACE "huxerui::tsx::embedded")
    endif ()
    get_filename_component(HUXERUI_TSX_EMBED_INPUT "${input}" ABSOLUTE)
    get_filename_component(HUXERUI_TSX_EMBED_OUTPUT "${output}" ABSOLUTE)
    get_filename_component(HUXERUI_TSX_EMBED_DIRECTORY "${HUXERUI_TSX_EMBED_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${HUXERUI_TSX_EMBED_DIRECTORY}")

    add_custom_command(
            OUTPUT "${HUXERUI_TSX_EMBED_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${HUXERUI_TSX_EMBED_INPUT}"
                "-DOUTPUT=${HUXERUI_TSX_EMBED_OUTPUT}"
                "-DNAMESPACE=${HUXERUI_TSX_EMBED_NAMESPACE}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedTsx.cmake"
            DEPENDS "${HUXERUI_TSX_EMBED_INPUT}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedTsx.cmake"
            COMMENT "Embedding TSX bundle: ${HUXERUI_TSX_EMBED_INPUT}"
            VERBATIM
    )
endfunction()
