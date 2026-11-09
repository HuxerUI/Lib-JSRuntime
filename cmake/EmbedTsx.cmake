# Generates a C++ header that embeds one JavaScript bundle.
# Invoked by huxerui_tsx_embed_header():
#   cmake -DINPUT=<js> -DOUTPUT=<h> -DNAMESPACE=<ns> -P EmbedTsx.cmake
if (NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED NAMESPACE)
    message(FATAL_ERROR "EmbedTsx.cmake requires INPUT, OUTPUT, and NAMESPACE")
endif ()

file(READ "${INPUT}" HUXERUI_TSX_EMBED_SOURCE)
get_filename_component(HUXERUI_TSX_EMBED_DIRECTORY "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${HUXERUI_TSX_EMBED_DIRECTORY}")
file(WRITE "${OUTPUT}"
"// Generated from ${INPUT}. Do not edit.
#pragma once

#include <string_view>

namespace ${NAMESPACE} {

inline constexpr std::string_view kBundle = R\"HUXERUI_TSX_APP(
${HUXERUI_TSX_EMBED_SOURCE}
)HUXERUI_TSX_APP\";

} // namespace ${NAMESPACE}
")
