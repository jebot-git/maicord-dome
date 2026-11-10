#pragma once
// The color glyph owner behind D3D11GlyphTextOwner::color; the GPU footprint probes read its resources.
#include "maicord/render/effects/colr_paint.hpp"
#include "maicord/render/d3d11/glyph_text.hpp"

namespace maicord {
struct D3D11ColorGlyphOwner {
    ColrPaintGpu gpu;
};
} // namespace maicord
