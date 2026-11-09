if (NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "missing bundle: ${INPUT}")
endif ()

file(READ "${INPUT}" content)

foreach (needle
        "@huxerui/tsx/jsx-runtime"
        "jsx"
        "Column"
        "Count: ")
    string(FIND "${content}" "${needle}" position)
    if (position EQUAL -1)
        message(FATAL_ERROR "bundle does not contain ${needle}: ${INPUT}")
    endif ()
endforeach ()

message(STATUS "verified esbuild bundle: ${INPUT}")
