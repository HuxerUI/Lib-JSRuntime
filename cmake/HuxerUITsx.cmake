include_guard(GLOBAL)

# Optional explicit esbuild binary. When empty, huxerui_tsx_bundle() checks
# HUXERUI_ESBUILD, PATH, then a local third_party/esbuild/ override, or downloads
# and SHA256-verifies the pinned @esbuild/<platform>@0.25.0 binary on demand.
set(HUXERUI_TSX_ESBUILD "" CACHE FILEPATH
        "esbuild binary used to bundle TSX (downloaded on demand when unset)")
set(HUXERUI_TSX_ESBUILD_PLATFORM "" CACHE STRING
        "Override the esbuild platform id (for example linux-arm64)")
set(HUXERUI_TSX_ESBUILD_BASE_URL "https://registry.npmjs.org" CACHE STRING
        "Base npm registry URL for on-demand @esbuild/<platform>@0.25.0 downloads")
set(HUXERUI_TSX_ESBUILD_CACHE_DIR "" CACHE PATH
        "Cache directory for downloaded esbuild binaries (defaults to CMAKE_BINARY_DIR/.huxerui-esbuild)")
set(_HUXERUI_TSX_ESBUILD_VERSION "0.25.0")

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

# Looks up the pinned SHA256 digest for `triplet` in third_party/esbuild/SHA256SUMS.
function(_huxerui_tsx_esbuild_sha256 triplet out)
    set(sums_file "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/esbuild/SHA256SUMS")
    if (NOT EXISTS "${sums_file}" OR NOT triplet)
        set("${out}" "" PARENT_SCOPE)
        return()
    endif ()

    set(suffix "")
    if (triplet MATCHES "^win32-")
        set(suffix ".exe")
    endif ()

    file(STRINGS "${sums_file}" sum_lines)
    set(found "")
    foreach (line IN LISTS sum_lines)
        if (line MATCHES "^([0-9a-fA-F][0-9a-fA-F]+)[ \t]+\\./${triplet}/esbuild${suffix}$")
            string(LENGTH "${CMAKE_MATCH_1}" digest_len)
            if (digest_len EQUAL 64)
                string(TOLOWER "${CMAKE_MATCH_1}" found)
                break()
            endif ()
        endif ()
    endforeach ()
    set("${out}" "${found}" PARENT_SCOPE)
endfunction()

# Downloads, extracts, and SHA256-verifies the pinned @esbuild/<triplet>@0.25.0 binary.
function(_huxerui_tsx_fetch_esbuild triplet out)
    _huxerui_tsx_esbuild_sha256("${triplet}" expected_sha256)
    if (NOT expected_sha256)
        set("${out}" "" PARENT_SCOPE)
        return()
    endif ()

    set(suffix "")
    if (triplet MATCHES "^win32-")
        set(suffix ".exe")
    endif ()

    set(cache_root "${HUXERUI_TSX_ESBUILD_CACHE_DIR}")
    if (NOT cache_root AND DEFINED ENV{HUXERUI_ESBUILD_CACHE_DIR})
        set(cache_root "$ENV{HUXERUI_ESBUILD_CACHE_DIR}")
    endif ()
    if (NOT cache_root)
        set(cache_root "${CMAKE_BINARY_DIR}/.huxerui-esbuild")
    endif ()
    get_filename_component(cache_root "${cache_root}" ABSOLUTE)

    set(cached_dir "${cache_root}/${_HUXERUI_TSX_ESBUILD_VERSION}/${triplet}")
    set(cached_bin "${cached_dir}/esbuild${suffix}")
    if (EXISTS "${cached_bin}")
        file(SHA256 "${cached_bin}" actual_sha256)
        if (actual_sha256 STREQUAL expected_sha256)
            if (NOT CMAKE_HOST_WIN32)
                file(CHMOD "${cached_bin}"
                        PERMISSIONS
                        OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE)
            endif ()
            set("${out}" "${cached_bin}" PARENT_SCOPE)
            return()
        endif ()
        file(REMOVE "${cached_bin}")
    endif ()

    set(base_url "${HUXERUI_TSX_ESBUILD_BASE_URL}")
    string(REGEX REPLACE "/+$" "" base_url "${base_url}")
    set(package_url
            "${base_url}/@esbuild/${triplet}/-/${triplet}-${_HUXERUI_TSX_ESBUILD_VERSION}.tgz")

    set(stage_dir "${cache_root}/${_HUXERUI_TSX_ESBUILD_VERSION}/.staging-${triplet}")
    file(REMOVE_RECURSE "${stage_dir}")
    file(MAKE_DIRECTORY "${stage_dir}/extract")
    set(tarball "${stage_dir}/${triplet}-${_HUXERUI_TSX_ESBUILD_VERSION}.tgz")

    message(STATUS
            "HuxerUI TSX: downloading pinned esbuild ${_HUXERUI_TSX_ESBUILD_VERSION} (${triplet}) from ${package_url}")
    file(DOWNLOAD
            "${package_url}"
            "${tarball}"
            STATUS download_status
            TLS_VERIFY ON
    )
    list(GET download_status 0 download_code)
    if (NOT download_code EQUAL 0)
        list(GET download_status 1 download_message)
        file(REMOVE_RECURSE "${stage_dir}")
        message(FATAL_ERROR
                "Failed to download esbuild ${_HUXERUI_TSX_ESBUILD_VERSION} for ${triplet} "
                "from ${package_url} (${download_message}). Provide -DHUXERUI_TSX_ESBUILD=/path/to/esbuild, "
                "override HUXERUI_TSX_ESBUILD_BASE_URL, or pre-populate "
                "third_party/esbuild/${triplet}/esbuild${suffix}.")
    endif ()

    file(ARCHIVE_EXTRACT
            INPUT "${tarball}"
            DESTINATION "${stage_dir}/extract"
    )

    if (triplet MATCHES "^win32-")
        set(extracted_bin "${stage_dir}/extract/package/esbuild.exe")
    else ()
        set(extracted_bin "${stage_dir}/extract/package/bin/esbuild")
    endif ()
    if (NOT EXISTS "${extracted_bin}")
        file(REMOVE_RECURSE "${stage_dir}")
        message(FATAL_ERROR
                "Downloaded @esbuild/${triplet}@${_HUXERUI_TSX_ESBUILD_VERSION} archive did not contain "
                "the expected binary for ${triplet}.")
    endif ()

    file(SHA256 "${extracted_bin}" actual_sha256)
    if (NOT actual_sha256 STREQUAL expected_sha256)
        file(REMOVE_RECURSE "${stage_dir}")
        message(FATAL_ERROR
                "SHA256 mismatch for downloaded esbuild (${triplet}): expected ${expected_sha256}, "
                "got ${actual_sha256}.")
    endif ()

    file(MAKE_DIRECTORY "${cached_dir}")
    configure_file("${extracted_bin}" "${cached_bin}" COPYONLY)
    if (NOT CMAKE_HOST_WIN32)
        file(CHMOD "${cached_bin}"
                PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE)
    endif ()
    file(REMOVE_RECURSE "${stage_dir}")
    set("${out}" "${cached_bin}" PARENT_SCOPE)
endfunction()

# Returns a local or cached/downloaded esbuild path for the target host triplet, or empty string.
function(_huxerui_tsx_vendored_esbuild out)
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/esbuild")
    _huxerui_tsx_host_platform(triplet)
    if (NOT triplet)
        set("${out}" "" PARENT_SCOPE)
        return()
    endif ()

    set(suffix "")
    if (triplet MATCHES "^win32-")
        set(suffix ".exe")
    endif ()
    set(candidate "${root}/${triplet}/esbuild${suffix}")
    if (EXISTS "${candidate}")
        set("${out}" "${candidate}" PARENT_SCOPE)
        return()
    endif ()

    _huxerui_tsx_fetch_esbuild("${triplet}" fetched)
    set("${out}" "${fetched}" PARENT_SCOPE)
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
    if (NOT HUXERUI_TSX_ESBUILD_BIN AND HUXERUI_TSX_ESBUILD_PLATFORM)
        _huxerui_tsx_vendored_esbuild(HUXERUI_TSX_ESBUILD_BIN)
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        find_program(HUXERUI_TSX_ESBUILD_BIN esbuild)
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        _huxerui_tsx_vendored_esbuild(HUXERUI_TSX_ESBUILD_BIN)
    endif ()
    if (NOT HUXERUI_TSX_ESBUILD_BIN)
        message(FATAL_ERROR
                "huxerui_tsx_bundle() could not find or fetch esbuild. Set HUXERUI_TSX_ESBUILD "
                "to an esbuild binary, put esbuild on PATH, or pre-populate "
                "third_party/esbuild/<platform>/esbuild for this host platform.")
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
            --sourcemap=inline
            --sources-content=false
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
