#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace huxerui::tsx::detail {

/// One original source location resolved from a generated bundle position.
struct SourcePosition {
  std::string filename;
  int line = 0;   ///< 1-based.
  int column = 0; ///< 1-based.
};

/// Diagnostic-only index over a source-map v3 document.
///
/// The bridge does not transform sources; it only resolves generated
/// positions back to the original `.tsx` files for diagnostics. Parsing is
/// intentionally minimal: indexed maps (`sections`) and name mappings are
/// ignored because neither is emitted by esbuild for the supported bundle
/// shape.
class SourceMap final {
public:
  /// Parses the last inline `sourceMappingURL` data URI in a bundle.
  /// @return An empty optional when the bundle has no inline map or the map is
  ///         malformed.
  [[nodiscard]] static std::optional<SourceMap> FromBundle(std::string_view bundle);

  /// Parses source-map JSON text.
  [[nodiscard]] static std::optional<SourceMap> FromJson(std::string_view json);

  [[nodiscard]] bool Empty() const noexcept { return generated_lines_.empty(); }

  /// Maps one 1-based generated location.
  /// @param line Generated bundle line.
  /// @param column Generated bundle column.
  /// @param base Directory of the bundle file; relative sources resolve
  ///        against it. Pass an empty path for embedded bundles.
  /// @return The original location, or `std::nullopt` when the location is
  ///         not covered by the map.
  [[nodiscard]] std::optional<SourcePosition> Find(
      int line,
      int column,
      const std::filesystem::path& base = {}
  ) const;

  struct Segment {
    std::uint32_t generated_column = 0;
    std::uint32_t source = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    bool mapped = false;
  };

private:
  std::vector<std::vector<Segment>> generated_lines_;
  std::vector<std::string> sources_;
  std::string source_root_;
};

/// Rewrites every `generated_filename:line:column` frame in a QuickJS stack to
/// its original source location. Frames that are not covered by the map and
/// frames from other modules are preserved.
[[nodiscard]] std::string RemapStack(
    std::string_view stack,
    std::string_view generated_filename,
    const SourceMap& source_map,
    const std::filesystem::path& base
);

} // namespace huxerui::tsx::detail
