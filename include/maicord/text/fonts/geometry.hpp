#pragma once
#include "maicord/text/fonts/axes.hpp"
namespace maicord {
// Only bitmap strikes are accepted. Layout still uses SFNT cmap/metrics and GSUB/GPOS.
enum class FontGeometryKind : u8 { bitmap };
struct FontGeometry {
    SfntFace face;
    SfntHorizontalMetrics metrics;
    FontAxes axes;
    FontGeometryKind kind;
};
constexpr bool font_ignores_bold(const FontGeometry&) noexcept {return true;}
struct FontGeometryInstance {const FontGeometry* font;};
SfntStatus font_geometry_open(const SfntFace&,FontGeometry*) noexcept;
}
