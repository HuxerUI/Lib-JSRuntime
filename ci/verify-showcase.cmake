if (NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "missing bundle: ${INPUT}")
endif ()

file(READ "${INPUT}" content)

foreach (needle
        "@huxerui/tsx/jsx-runtime"
        "HuxerUI TSX"
        "VirtualList"
        "useBottomSheet")
    string(FIND "${content}" "${needle}" position)
    if (position EQUAL -1)
        message(FATAL_ERROR "showcase bundle does not contain ${needle}: ${INPUT}")
    endif ()
endforeach ()

message(STATUS "verified showcase bundle: ${INPUT}")
