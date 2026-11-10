#include "../text/glyph_effect_ordinals.hpp"
#include "maicord/core/memory.hpp"
#include "maicord/core/scratch_pages.hpp"
#include "maicord/gpu/api.hpp"
#include "../text/d3d11_color_glyph_owner.hpp"
#include "maicord/core/log.hpp"
#include "maicord/core/hash.hpp"
#include "maicord/ui/theme/background_presentation.hpp"
#include "maicord/ui/theme/foreground_color.hpp"
#include "maicord/ui/text/ansi_text.hpp"
#include "maicord/render/effects/colr_paint.hpp"
#include "maicord/media/image/regional_flag_art.hpp"

#include "maicord/text/fonts/run.hpp"
#include "maicord/sys/simd.hpp"
#include "maicord/platform/font/default_fonts.hpp"
#if !defined(_WIN32)
#include "maicord/text/opentype/bitmap_color.hpp"
#endif

#include "maicord/ui/chat/messages/markdown.hpp"

namespace maicord {
GpuSrv* d3d11_glyph_text_color_view(const D3D11GlyphTextOwner& owner) noexcept {
    return owner.color ? owner.color->gpu.view : nullptr;
}
GpuPagedTexture* d3d11_glyph_text_color_surface(const D3D11GlyphTextOwner& owner) noexcept {
    return owner.color ? owner.color->gpu.surface : nullptr;
}
namespace {
D3D11ColorGlyphOwner* color_owner(D3D11GlyphTextOwner* owner) noexcept {
    if (!owner->color)
        owner->color = static_cast<D3D11ColorGlyphOwner*>(
            commit_pages(sizeof(D3D11ColorGlyphOwner)));
    return owner->color;
}

// Worker-compiled programs borrow account-module storage only for this call.
bool rasterize_program(D3D11GlyphTextOwner* owner, GpuContext* context,
                       const ColrPaintProgram& program, const GlyphAtlasAllocation& allocation,
                       u16 extent, float pixels_per_unit, u16 target_width, u16 target_height,
                       u16 padding, GpuUav* target) noexcept {
    if (!owner || !owner->gpu.width || !context || !target || allocation.width != extent ||
        allocation.height != extent)
        return false;
    if (owner->color_batch)
        owner->color_batch->reset();
    if (!color_owner(owner))
        return false;
    const GlyphRasterBounds bounds{0, 0, extent, extent, extent, extent};
    return colr_paint_rasterize_target(&owner->color->gpu, context, program, bounds, allocation,
                                       pixels_per_unit, target_width, target_height,
                                       padding, target);
}
} // namespace
bool d3d11_glyph_text_rasterize_flag_program(D3D11GlyphTextOwner* owner, GpuContext* context,
                                             const ColrPaintProgram& program,
                                             const GlyphAtlasAllocation& allocation, u16 target_width,
                                             u16 target_height, u16 padding,
                                             GpuUav* target) noexcept {
    return rasterize_program(owner, context, program, allocation, 64, kRegionalFlagPixelsPerUnit,
                             target_width, target_height, padding, target);
}
// The frame was compiled at its tile's extent (lottie_raster_pixels).
bool d3d11_glyph_text_rasterize_lottie_program(D3D11GlyphTextOwner* owner, GpuContext* context,
    const ColrPaintProgram& program,const GlyphAtlasAllocation& allocation,u16 target_width,
    u16 target_height,u16 padding,GpuUav* target) noexcept {
    return rasterize_program(owner, context, program, allocation, static_cast<u16>(allocation.width),
                             static_cast<float>(allocation.width), target_width, target_height, padding, target);
}
namespace {
constexpr u16 color_face_flag = 2;
#if !defined(_WIN32)
// A colour bitmap face (CBLC/CBDT): its glyphs are decoded images placed in the colour atlas, not paint programs.
constexpr u16 bitmap_face_flag = 4;
constexpr u16 monochrome_bitmap_face_flag = 8;
#endif

// The nearest whole device pixel, also for the negative origins of text
// scrolled above its clip.
float whole_pixels(float pixels) noexcept {
    const float shifted = pixels + .5f;
    const i32 truncated = static_cast<i32>(shifted);
    return static_cast<float>(static_cast<float>(truncated) > shifted ? truncated - 1 : truncated);
}

struct GlyphTextStorage {
    TextGlyph glyphs[kRendererGlyphCapacity];
    TextRun runs[kRendererTextRunCapacity];
    u16 clusters[kRendererGlyphCapacity];

    GlyphAtlasEntry atlas_entries[kRendererAtlasEntryCapacity];
    GlyphAtlasRow atlas_rows[kRendererAtlasRowCapacity];
    GlyphRasterOrigin atlas_origins[kRendererAtlasEntryCapacity];
    GlyphAtlasIndex atlas_index;
    u16 atlas_index_buckets[kRendererAtlasEntryCapacity * 2];
    GlyphQuad title_quads[kRendererGlyphCapacity];
    bool geometry_stale;
};

// The owner header has a fixed size budget, so storage-resident state is
// reached through the storage block that owns atlas_origins.
GlyphTextStorage* text_storage(D3D11GlyphTextOwner* owner) noexcept {
    return reinterpret_cast<GlyphTextStorage*>(reinterpret_cast<u8*>(owner->atlas_origins) -
                                               offsetof(GlyphTextStorage, atlas_origins));
}

GlyphAtlasIndex* atlas_index(D3D11GlyphTextOwner* owner) noexcept {
    return owner->atlas_origins ? &text_storage(owner)->atlas_index : nullptr;
}

// u follows the atlas width, v the virtual height: both are fixed, so a quad
// stays valid while its glyph is resident.
struct AtlasTexel {
    float u, v;
};
inline AtlasTexel atlas_texel(const D3D11GlyphTextOwner& owner) noexcept {
    return {1.0f / owner.allocator.width, 1.0f / owner.allocator.height};
}

// Backs (or returns) the mono and colour surfaces' rows to match the allocator's pages.
bool sync_surfaces(D3D11GlyphTextOwner* owner, GpuContext* context) noexcept {
    bool ok = true;
    GpuPagedTexture* color = owner->color ? owner->color->gpu.surface : nullptr;
    for (u32 page = 0; page < owner->allocator.page_count; ++page) {
        const u32 rows = owner->allocator.pages[page].committed;
        ok = gpu_ok(gpu_paged_texture_commit(context, owner->gpu.surface, page, rows)) && ok;
        if (color)
            ok = gpu_ok(gpu_paged_texture_commit(context, color, page, rows)) && ok;
    }
    return ok;
}

// A glyph the atlas does not hold yet, with its rows backed on both surfaces.
// Glyphs used within kRendererGlyphProtectMs count as shown and are kept.
GlyphAtlasAllocation allocate_glyph(D3D11GlyphTextOwner* owner, GpuContext* context, u64 key, u16 width,
                                    u16 height) noexcept {
    GlyphAtlasAllocation allocation =
        glyph_atlas_allocate_unique_indexed(&owner->allocator, atlas_index(owner), key, width, height);
    if (allocation.ok() && !sync_surfaces(owner, context)) {
        glyph_atlas_release(&owner->allocator, allocation.handle);
        allocation.error = GlyphAtlasError::no_capacity;
    }
    return allocation;
}

// Where a glyph the atlas holds sits: its origin and the allocation's extent.
__forceinline GlyphRasterBounds cached_bounds(const D3D11GlyphTextOwner& owner,
                                              const GlyphAtlasAllocation& allocation) noexcept {
    const auto origin = owner.atlas_origins[allocation.handle.slot];
    return {origin.x, origin.y, origin.x + allocation.width, origin.y + allocation.height, allocation.width,
            allocation.height};
}

bool initialize_storage(D3D11GlyphTextOwner* owner) noexcept {
    if (!arena_initialize(&owner->storage,
                          sizeof(GlyphTextStorage) +
                              sizeof(D3D11GlyphTextFontFace) * kRendererFontFaceCapacity,
                          0, 4 * 1024)) {
        return false;
    }
    // VirtualAlloc supplies zero bytes in this fresh arena. Do not touch unused pages.
    auto* storage = arena_allocate<GlyphTextStorage>(&owner->storage);
    if (!storage) {
        return false;
    }
    owner->glyphs = storage->glyphs;
    owner->runs = storage->runs;
    owner->clusters = storage->clusters;

    owner->atlas_entries = storage->atlas_entries;
    owner->atlas_rows = storage->atlas_rows;
    owner->atlas_origins = storage->atlas_origins;
    owner->title_quads = storage->title_quads;
    owner->font_faces =
        arena_allocate<D3D11GlyphTextFontFace>(&owner->storage, glyph_font_initial_capacity);
    if (!owner->font_faces)
        return false;
    owner->font_face_capacity = static_cast<u16>(glyph_font_initial_capacity);
    return true;
}

bool reserve_font_record(D3D11GlyphTextOwner* owner) noexcept {
    if (owner->font_face_count < owner->font_face_capacity)
        return true;
    if (owner->font_face_capacity >= kRendererFontFaceCapacity)
        return false;
    const u32 added =
        minimum(glyph_font_initial_capacity, kRendererFontFaceCapacity - owner->font_face_capacity);
    // Font records are the arena's final allocation. Append in place so live
    // font IDs, outline bindings, atlas keys and retained quads stay valid.
    auto* next = arena_allocate_zero<D3D11GlyphTextFontFace>(&owner->storage, added);
    if (!next || next != owner->font_faces + owner->font_face_capacity)
        return false;
    owner->font_face_capacity = static_cast<u16>(owner->font_face_capacity + added);
    return true;
}

D3D11GlyphTextInitializeResult initialize_result(D3D11GlyphTextInitializeError error,
                                                 GpuStatus native_error = kGpuOk) noexcept {
    return D3D11GlyphTextInitializeResult{native_error, error};
}

// Renderer font IDs are stable and retain one native run reference each.
u16 retain_run_font(D3D11GlyphTextOwner* owner, const ShapedText& text, const TextRun& run) noexcept {
    if (!run.native_font || !text.native_fonts() || text.native_fonts() != owner->system.native_fonts)
        return 0;
    const NativeFontRunHandle handle = run.native_face;
    auto* store = native_font_session_runs(owner->system.native_fonts);
    const auto* source = store ? native_font_run_get(*store, handle) : nullptr;
    if (!source)
        return 0;
    for (u32 i = 0; i < owner->font_face_count; ++i) {
        const auto& face = owner->font_faces[i];
        if (face.native_face.slot == handle.slot && face.native_face.generation == handle.generation)
            return face.id;
    }
    if (!reserve_font_record(owner)) {
        log_u64(LogLevel::error, "render", "glyph_font_face_capacity", owner->font_face_count);
        return 0;
    }
    if (!native_font_run_retain(store, handle))
        return 0;
    auto& face = owner->font_faces[owner->font_face_count++];
    reset_value(&face);
    face.native_face = handle;
    face.id = static_cast<u16>(owner->font_face_count);
    const auto* font =
        native_font_get(*native_font_session_fonts(owner->system.native_fonts), source->source.face);
    if(!font || font->geometry.kind!=FontGeometryKind::bitmap)return 0;
    face.flags |= color_face_flag | bitmap_face_flag;
    CbdtFont bitmap{};
    if(cbdt_open(font->geometry.face,&bitmap)==SfntStatus::ok && bitmap.pixelated)
        face.flags |= monochrome_bitmap_face_flag;
    return face.id;
}



Color text_style_color(const Theme* theme, const Color& base, u32 flags,
                       const Color* inline_role_color) noexcept {
    if (!theme) {
        return base;
    }
    // Role colours are server data: no theme remaps them.
    if ((flags & text_style_inline_role_color) != 0 && inline_role_color &&
        inline_role_color->alpha > 0.0f) {
        return *inline_role_color;
    }
    if ((flags & text_style_syntax_comment) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.comment);
    }
    if ((flags & text_style_syntax_string) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.string);
    }
    if ((flags & text_style_syntax_number) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.number);
    }
    if ((flags & text_style_syntax_keyword) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.keyword);
    }
    if ((flags & text_style_syntax_type) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.type);
    }
    if ((flags & text_style_syntax_builtin) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.builtin);
    }
    if ((flags & text_style_syntax_preprocessor) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.preprocessor);
    }
    if ((flags & text_style_syntax_operator) != 0) {
        return ui_foreground_color(*theme, theme->tokens.syntax.operator_);
    }
    if ((flags & text_style_spoiler) != 0) {
        return theme->interactive;
    }
    // A hovered mention or link chip is an accent pill with on-accent text; role
    // mentions keep their colour.
    constexpr u32 hovered_mention = text_style_mention | text_style_interactive_hover;
    if ((flags & hovered_mention) == hovered_mention) {
        return theme->tokens.emphasis.text;
    }
    if ((flags & text_style_mention) != 0) {
        return ui_foreground_color(*theme, theme->tokens.mention.text);
    }
    if ((flags & text_style_link) != 0) {
        return ui_foreground_color(*theme, theme->tokens.text.link);
    }
    if ((flags & text_style_subtext) != 0) {
        return ui_background_muted_text_color(*theme);
    }
    return base;
}

Color text_decoration_color(const Theme* theme, const Color& base,
                            const TextDecoration& decoration) noexcept {
    if (!theme) {
        return base;
    }
    if (decoration.kind == TextDecorationKind::quote_bar) {
        return ui_background_muted_text_color(*theme);
    }
    // ```ansi lines take their decorating tag's colour, faded like the glyphs.
    if (const u16 line = ansi_decoration_line_color(decoration)) {
        const Color color = ansi_palette_color(*theme, static_cast<u8>(line - 1u));
        return Color{color.red, color.green, color.blue, base.alpha};
    }
    if (decoration.kind != TextDecorationKind::background) {
        return text_style_color(theme, base, decoration.style_flags, nullptr);
    }
    if (const u16 background = ansi_decoration_background(decoration)) {
        return ansi_palette_color(*theme, static_cast<u8>(background - 1u));
    }
    if ((decoration.style_flags & text_style_spoiler) != 0) {
        return theme->interactive;
    }
    if ((decoration.style_flags & text_style_mention) != 0) {
        return (decoration.style_flags & text_style_interactive_hover) ? theme->tokens.emphasis.fill
                                                                       : theme->tokens.mention.inline_background;
    }
    return theme->surface_high;
}

bool resolve_role_color(const D3D11GlyphTextAppendRequest& request, const MessageMarkdownSemantic& semantic,
                        Color* color) noexcept {
    return request.resolve_role_color && semantic.target != 0 &&
           request.resolve_role_color(request.role_context, request.message_id, semantic.kind, semantic.target,
                                      color);
}

bool role_color_for_run(const D3D11GlyphTextAppendRequest& request, const TextRun& run,
                        Color* color) noexcept {
    if ((run.style_flags & text_style_mention) == 0) {
        return false;
    }
    const u32 run_end = run.text_position + run.text_length;
    for (u32 index = 0; index < request.semantics.count; ++index) {
        const MessageMarkdownSemantic& semantic = request.semantics[index];
        if ((semantic.kind != MessageMarkdownSemanticKind::role_mention &&
             semantic.kind != MessageMarkdownSemanticKind::user_mention) ||
            semantic.byte_count == 0) {
            continue;
        }
        u32 first = 0;
        u32 last = 0;
        if (!utf8_utf16_position_for_byte_offset(request.source, semantic.byte_offset, &first) ||
            !utf8_utf16_position_for_byte_offset(request.source, semantic.byte_offset + semantic.byte_count,
                                                 &last) ||
            first >= run_end || last <= run.text_position) {
            continue;
        }
        return resolve_role_color(request, semantic, color);
    }
    return false;
}

bool role_color_for_decoration(const D3D11GlyphTextAppendRequest& request, const TextDecoration& decoration,
                               Color* color) noexcept {
    if ((decoration.style_flags & text_style_mention) == 0) {
        return false;
    }
    for (u32 index = 0; index < request.text->hit_region_count; ++index) {
        const TextHitRegion& region = request.text->hit_regions[index];
        if (region.semantic_index < request.semantic_base ||
            region.semantic_index - request.semantic_base >= request.semantics.count ||
            region.right <= decoration.left || region.left >= decoration.right ||
            region.bottom <= decoration.top || region.top >= decoration.bottom) {
            continue;
        }
        const MessageMarkdownSemantic& semantic =
            request.semantics[region.semantic_index - request.semantic_base];
        if ((semantic.kind == MessageMarkdownSemanticKind::role_mention ||
             semantic.kind == MessageMarkdownSemanticKind::user_mention) &&
            resolve_role_color(request, semantic, color)) {
            return true;
        }
    }
    return false;
}

#if !defined(_WIN32)
enum class BitmapGlyph : u8 { ready, absent, retry, failed };

// The scratch of one bitmap glyph: the decode, the scaled tile and the padded copy of the upload.
struct BitmapScratch {
    Arena arena{};
    BitmapScratch() noexcept { arena_initialize(&arena, 16u << 20, 0, 64u << 10); }
    ~BitmapScratch() noexcept { arena_release(&arena); }
    BitmapScratch(const BitmapScratch&) = delete;
    BitmapScratch& operator=(const BitmapScratch&) = delete;
};

// A colour bitmap glyph that is not in the atlas: the image of the strike nearest the size, scaled to it, uploaded
// like a painted glyph (same atlas allocation, same invalidation rules). `absent`: the face has no image for it.
BitmapGlyph build_bitmap_glyph(D3D11GlyphTextOwner* owner, GpuContext* context,
                               const D3D11GlyphTextAppendRequest& request, const TextRun& run, u64 key, u16 glyph,
                               float pixels_per_em, GlyphAtlasAllocation* output) noexcept {
    auto& color = *owner->color;
    const auto* record = native_font_run_get(*native_font_session_runs(owner->system.native_fonts), run.native_face);
    const auto* face =
        record ? native_font_get(*native_font_session_fonts(owner->system.native_fonts), record->source.face) : nullptr;
    CbdtFont font{};
    if (!face || cbdt_open(face->geometry.face, &font) != SfntStatus::ok)
        return BitmapGlyph::failed;
    BitmapScratch scratch;
    if (!scratch.arena.base)
        return BitmapGlyph::failed;
    BitmapColorGlyph tile{};
    const SfntStatus built = bitmap_color_glyph_build(font, glyph, pixels_per_em, &scratch.arena, &tile);
    if (built == SfntStatus::glyph_out_of_range)
        return BitmapGlyph::absent;
    if (built != SfntStatus::ok)
        return BitmapGlyph::failed;
    const GlyphRasterBounds bounds{tile.left,
                                   tile.top,
                                   tile.left + static_cast<i32>(tile.width),
                                   tile.top + static_cast<i32>(tile.height),
                                   static_cast<u16>(tile.width),
                                   static_cast<u16>(tile.height)};
    if (!colr_paint_attach_surface(&color.gpu, gpu_context_device(context), owner->allocator))
        return BitmapGlyph::failed;
    const auto allocation = allocate_glyph(owner, context, key, bounds.width, bounds.height);
    if (!allocation.ok())
        return BitmapGlyph::failed;
    if (!colr_paint_upload_pixels(&color.gpu, context, allocation, owner->allocator.width, owner->allocator.height,
                                  owner->allocator.padding, tile.pixels, &scratch.arena, owner->color_batch)) {
        log_u64(LogLevel::error, "render", "bitmap_glyph_upload_failed", glyph);
        glyph_atlas_reset(&owner->allocator);
        return BitmapGlyph::failed;
    }
    owner->atlas_origins[allocation.handle.slot] = {bounds.left, bounds.top};
    ++owner->cache_builds;
    *output = allocation;
    if (allocation.evicted_entries || allocation.atlas_reset) {
        *request.atlas_invalidated = true;
        return BitmapGlyph::retry;
    }
    return BitmapGlyph::ready;
}
#endif
// `origin_x`/`origin_y` are the block's origin on whole device pixels.
bool append_color_glyph_run(D3D11GlyphTextOwner* owner, GpuContext* context,
                            const D3D11GlyphTextAppendRequest& request, const TextRun& run,
                            const Color& foreground, float origin_x, float origin_y) noexcept {
    if (!color_owner(owner))
        return false;
    const u16 face_id = retain_run_font(owner, *request.text, run);
    if (!face_id)
        return false;
    const float pixel_scale = request.dpi / 96.f, pixels_per_em = run.em_size * pixel_scale;
    const AtlasTexel texel = atlas_texel(*owner);
    const u32 em_fixed = static_cast<u32>(run.em_size * 64.f + .5f);
    float advance = 0;
    for (u32 i = 0; i < run.glyph_count; ++i) {
        const auto& glyph = request.text->glyphs[run.first_glyph + i];
        u64 key = (1ull << 63) | (static_cast<u64>(face_id) << 48) | (static_cast<u64>(request.dpi) << 32) |
                  (static_cast<u64>(em_fixed) << 16) | glyph.index;
        auto allocation = glyph_atlas_find_indexed(&owner->allocator, atlas_index(owner), key);
        if(!allocation.ok()) {
            const auto state=build_bitmap_glyph(owner,context,request,run,key,glyph.index,pixels_per_em,&allocation);
            if(state==BitmapGlyph::absent){advance+=glyph.advance;continue;}
            if(state!=BitmapGlyph::ready)return false;
        }
        const GlyphRasterBounds bounds=cached_bounds(*owner,allocation);
        if (*request.count >= request.capacity)
            return false;
        // Odd bidi levels carry a right-edge origin. Cached outlines/paint
        // programs use the ordinary left glyph origin, so consume this glyph's
        // advance before applying its directional offset.
        const bool rtl = (run.bidi_level & 1) != 0;
        const float x =
            origin_x + run.baseline_x +
            (rtl ? -(advance + glyph.advance + glyph.advance_offset) : advance + glyph.advance_offset);
        const float y = origin_y + run.baseline_y - glyph.ascender_offset;
        auto& q = request.output[(*request.count)++];
        q = {x + bounds.left / pixel_scale,
             y + bounds.top / pixel_scale,
             x + bounds.right / pixel_scale,
             y + bounds.bottom / pixel_scale,
             2.f + static_cast<float>(allocation.x) * texel.u,
             static_cast<float>(allocation.y) * texel.v,
             2.f + static_cast<float>(allocation.x + allocation.width) * texel.u,
             static_cast<float>(allocation.y + allocation.height) * texel.v,
             {1, 1, 1, foreground.alpha}};
#if !defined(_WIN32)
        if ((owner->font_faces[face_id-1u].flags & monochrome_bitmap_face_flag)!=0) {
            q.color=foreground;
            if(request.theme && glyph.reserved!=kAnsiColorNone) {
                const auto color=ansi_palette_color(*request.theme,static_cast<u8>(glyph.reserved-1u));
                q.color={color.red,color.green,color.blue,foreground.alpha};
            }
            const float snapped_x=__builtin_elementwise_floor(q.left*pixel_scale+.5f)/pixel_scale;
            const float snapped_y=__builtin_elementwise_floor(q.top*pixel_scale+.5f)/pixel_scale;
            q.right+=snapped_x-q.left; q.bottom+=snapped_y-q.top;
            q.left=snapped_x; q.top=snapped_y;
        }
#endif
        advance += glyph.advance;
    }
    return true;
}

// Empty hit-test rectangles are valid after trimming. Keep their validation off
// the visible-quad path; integer bits preserve NaN checks under /fp:fast.
__declspec(noinline) bool finite_decoration_bounds(const TextDecoration& decoration) noexcept {
    const float bounds[]{decoration.left, decoration.top, decoration.right, decoration.bottom};
    for (float bound : bounds) {
        u32 bits = 0;
        copy_bytes(&bits, &bound, sizeof(bits));
        if ((bits & 0x7f800000u) == 0x7f800000u)
            return false;
    }
    return true;
}

} // namespace

// The area test must preserve unordered comparisons before classifying empty
// geometry. /fp:fast otherwise permits NaN bounds to bypass the cold check.
#pragma float_control(precise, on, push)
bool d3d11_glyph_text_append_solid(const TextDecoration& decoration, float origin_x, float origin_y,
                                   const Color& color, GlyphQuad* output, u32 capacity,
                                   u32* count) noexcept {
    if (!output || !count || *count > capacity)
        return false;
    if (!(decoration.right > decoration.left) || !(decoration.bottom > decoration.top)) {
        return finite_decoration_bounds(decoration);
    }
    if (*count == capacity)
        return false;
    const bool circle = decoration.kind == TextDecorationKind::list_bullet;
    output[(*count)++] =
        GlyphQuad{origin_x + decoration.left,   origin_y + decoration.top, origin_x + decoration.right,
                  origin_y + decoration.bottom, circle ? -3.0f : -1.0f,    circle ? 0.0f : -1.0f,
                  circle ? -2.0f : -1.0f,       circle ? 1.0f : -1.0f,     color};
    return true;
}

#pragma float_control(pop)

// Caller-owned glyph buffers lend their reserved storage to effect preparation.
// No geometry or cluster-map field changes, and no submitted quad keeps a tag.
bool d3d11_glyph_text_append(D3D11GlyphTextOwner* owner, GpuContext* context,
                             const D3D11GlyphTextAppendRequest& request) noexcept {
    if (!owner || !context || !request.text || !request.output || !request.count ||
        !request.atlas_invalidated || *request.count > request.capacity) {
        return false;
    }
    const float pixel_scale=request.dpi/96.f;
    if(!(pixel_scale>0))return false;
    const float origin_x=whole_pixels(request.origin_x*pixel_scale)/pixel_scale;
    const float origin_y=whole_pixels(request.origin_y*pixel_scale)/pixel_scale;
    for (u32 index = 0; index < request.text->decoration_count; ++index) {
        const TextDecoration& decoration = request.text->decorations[index];
        if (decoration.kind != TextDecorationKind::background &&
            decoration.kind != TextDecorationKind::quote_bar &&
            decoration.kind != TextDecorationKind::list_bullet) {
            continue;
        }
        Color decoration_color = text_decoration_color(request.theme, request.color, decoration);
        Color role_color{};
        if (role_color_for_decoration(request, decoration, &role_color)) {
            role_color.alpha = (decoration.style_flags & text_style_interactive_hover) ? .3f : .18f;
            decoration_color = role_color;
        }
        if (!d3d11_glyph_text_append_solid(decoration, origin_x, origin_y, decoration_color,
                                           request.output, request.capacity, request.count)) {
            return false;
        }
    }
    if (request.selection_active) {
        Color selected = request.theme ? request.theme->accent : Color{.3451f, .3961f, .9490f, 1};
        selected.alpha = .38f;
        for (u32 i = 0; i < request.text->hit_region_count; ++i) {
            const auto& region = request.text->hit_regions[i];
            if (region.semantic_index != request.selection_semantic_index || region.right <= region.left ||
                region.bottom <= region.top)
                continue;
            const TextDecoration highlight{region.left,   region.top, region.right,
                                           region.bottom, 0,          TextDecorationKind::background};
            if (!d3d11_glyph_text_append_solid(highlight, origin_x, origin_y, selected,
                                               request.output, request.capacity, request.count))
                return false;
        }
    }
    if(request.theme)ansi_text_tag_glyphs(*request.text,request.source,request.styles);
    for(u32 i=0;i<request.text->run_count;++i) {
        const auto& run=request.text->runs[i];
        Color foreground=text_style_color(request.theme,request.color,run.style_flags,request.inline_role_color);
        Color role{};if(role_color_for_run(request,run,&role))foreground=role;
        if(!append_color_glyph_run(owner,context,request,run,foreground,origin_x,origin_y))return false;
    }
    for (u32 index = 0; index < request.text->decoration_count; ++index) {
        const TextDecoration& decoration = request.text->decorations[index];
        if (decoration.kind != TextDecorationKind::underline &&
            decoration.kind != TextDecorationKind::strikethrough) {
            continue;
        }
        Color decoration_color = text_decoration_color(request.theme, request.color, decoration);
        Color role_color{};
        if (role_color_for_decoration(request, decoration, &role_color)) {
            decoration_color = role_color;
        }
        if (!d3d11_glyph_text_append_solid(decoration, origin_x, origin_y, decoration_color,
                                           request.output, request.capacity, request.count)) {
            return false;
        }
    }
    return true;
}

bool d3d11_glyph_text_rebuild_title(D3D11GlyphTextOwner* owner, GpuContext* context, u32 dpi,
                                    bool reset_atlas) noexcept {
    if (!owner || !context || !owner->gpu.surface || !owner->title.glyphs) {
        return false;
    }
    // Login/content normally replaces the bootstrap title before the first draw.
    // Keep its shape and atlas entries absent until a caller actually needs them.
    if (owner->title.run_count == 0 &&
        !text_shape_utf8(&owner->system, {utf8_literal("MaiCord - GPU text"), {}, 600.0f, 64.0f}, &owner->title).ok()) {
        return false;
    }
    if (reset_atlas) {
        glyph_atlas_reset(&owner->allocator);
    }
    owner->title_quad_count = 0;
    bool invalidated = false;
    const D3D11GlyphTextAppendRequest request{
        &owner->title,
        owner->title_quads,
        &owner->title_quad_count,
        &invalidated,
        {},
        {},
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        kDefaultTheme.text_primary,
        0.0f,
        0.0f,
        0,
        0,
        kRendererGlyphCapacity,
        dpi,
    };
    return d3d11_glyph_text_append(owner, context, request) && !invalidated && owner->title_quad_count != 0;
}

D3D11GlyphTextInitializeResult d3d11_glyph_text_initialize(D3D11GlyphTextOwner* owner, GpuDevice* device,
                                                           GpuContext* context, u32 dpi,
                                                           GlyphAtlasGpu* prestarted_atlas) noexcept {
    if (!owner || !device || !context || dpi == 0 || owner->storage.base || owner->system.native_text ||
        owner->gpu.surface || owner->color_glyphs) {
        return initialize_result(D3D11GlyphTextInitializeError::invalid_input, E_INVALIDARG);
    }
    if (!initialize_storage(owner)) {
        d3d11_glyph_text_release(owner);
        return initialize_result(D3D11GlyphTextInitializeError::shaped_storage, E_OUTOFMEMORY);
    }
#if defined(_WIN32)
    const TextShapeResult system = text_system_initialize(&owner->system, L"Segoe UI", 16.0f);
#else
    const TextShapeResult system =
        text_system_initialize(&owner->system, default_font_family(DefaultFont::ui), 16.0f);
#endif
    if (!system.ok()) {
        d3d11_glyph_text_release(owner);
        return initialize_result(D3D11GlyphTextInitializeError::text_system);
    }
    if (!shaped_text_initialize(&owner->title, Slice<TextGlyph>{owner->glyphs, kRendererGlyphCapacity},
                                Slice<TextRun>{owner->runs, kRendererTextRunCapacity},
                                Slice<u16>{owner->clusters, kRendererGlyphCapacity})) {
        d3d11_glyph_text_release(owner);
        return initialize_result(D3D11GlyphTextInitializeError::shaped_storage, E_INVALIDARG);
    }
    // Both glyph surfaces back whole 256-row bands on Direct3D 11 (one tile row of R8, two of RGBA8).
    const u16 commit_rows = gpu_paged_commit_rows(1);
    const GlyphAtlasPaging paging{kRendererGlyphAtlasPageRows, kRendererGlyphAtlasPages, commit_rows == 0,
                                  commit_rows, kRendererGlyphAtlasPageRows, kRendererGlyphProtectMs, true, {}};
    if (!glyph_atlas_initialize(&owner->allocator, kRendererGlyphAtlasExtent, kRendererGlyphAtlasPageRows, 1,
                                Slice<GlyphAtlasEntry>{owner->atlas_entries, kRendererAtlasEntryCapacity},
                                Slice<GlyphAtlasRow>{owner->atlas_rows, kRendererAtlasRowCapacity}) ||
        !glyph_atlas_configure_paging(&owner->allocator, paging) ||
        !glyph_atlas_index_initialize(atlas_index(owner), Slice<u16>{text_storage(owner)->atlas_index_buckets,
                                                                     kRendererAtlasEntryCapacity * 2})) {
        d3d11_glyph_text_release(owner);
        return initialize_result(D3D11GlyphTextInitializeError::atlas_storage, E_INVALIDARG);
    }
    const bool adopt = prestarted_atlas && prestarted_atlas->surface && prestarted_atlas->texture_view &&
                       prestarted_atlas->width == owner->allocator.width &&
                       prestarted_atlas->height == owner->allocator.height &&
                       prestarted_atlas->command_capacity == kRendererPathCapacity;
    GlyphAtlasGpuResult gpu{S_OK, GlyphAtlasGpuError::none};
    if (adopt) {
        owner->gpu = *prestarted_atlas;
        reset_value<GlyphAtlasGpu>(prestarted_atlas);
    } else {
        gpu = glyph_atlas_gpu_initialize_paged(&owner->gpu, device, kRendererGlyphAtlasExtent,
                                               kRendererGlyphAtlasPageRows, kRendererGlyphAtlasPages,
                                               kRendererPathCapacity);
    }
    if (!gpu.ok()) {
        d3d11_glyph_text_release(owner);
        return initialize_result(D3D11GlyphTextInitializeError::atlas_gpu, gpu.native_error);
    }
    owner->color_glyphs = true;
    owner->ready = true;
    return initialize_result(D3D11GlyphTextInitializeError::none);
}

void d3d11_glyph_text_reset_atlas(D3D11GlyphTextOwner* owner) noexcept {
    if (owner) {
        if (owner->color_batch)
            owner->color_batch->reset();
        glyph_atlas_reset(&owner->allocator);
    }
}

// A lost device took the atlas textures with it: new ones at the height the allocator holds, an empty allocator
// (every glyph is rasterized again on demand) and the stale-geometry flag so retained quads are rebuilt.
bool d3d11_glyph_text_recreate_gpu(D3D11GlyphTextOwner* owner, GpuDevice* device) noexcept {
    if (!owner || !device || !owner->atlas_origins)
        return false;
    if (owner->color_batch)
        owner->color_batch->reset();
    owner->color_batch = nullptr;
    if (owner->color)
        colr_paint_gpu_release(&owner->color->gpu);
    glyph_atlas_gpu_release(&owner->gpu);
    owner->ready = glyph_atlas_gpu_initialize_paged(&owner->gpu, device, kRendererGlyphAtlasExtent,
                                                    kRendererGlyphAtlasPageRows, kRendererGlyphAtlasPages,
                                                    kRendererPathCapacity)
                       .ok();
    glyph_atlas_reset(&owner->allocator);
    ++owner->allocator.full_resets;
    text_storage(owner)->geometry_stale = true;
    owner->title_quad_count = 0;
    return owner->ready;
}

u64 d3d11_glyph_text_trim_deadline(const D3D11GlyphTextOwner& owner) noexcept {
    return owner.atlas_origins ? glyph_atlas_trim_deadline(owner.allocator, kRendererGlyphIdleTrimMs) : 0;
}

// Idle trim drops glyphs nothing used within kRendererGlyphProtectMs and returns the rows that empty; what stays
// keeps its place, so only quads of evicted glyphs are rebuilt. The title stream (the sign-in screen, the bootstrap
// title) is drawn every frame from quads nothing builds again after an eviction, so its glyphs stay: they are pinned.
bool d3d11_glyph_text_trim(D3D11GlyphTextOwner* owner, GpuContext* context, u64 now_ms) noexcept {
    if (!owner || !context || !owner->atlas_origins)
        return false;
    // The atlas has no clock until the first trim check observes the caller's: what the first screens allocated
    // before it counts as allocated now. (At clock zero that check found them idle since time zero and trimmed the
    // sign-in screen's glyphs in its first frames.)
    if (!owner->allocator.now_ms)
        owner->allocator.last_activity_ms = now_ms;
    glyph_atlas_observe_time(&owner->allocator, now_ms);
    const u64 deadline = glyph_atlas_trim_deadline(owner->allocator, kRendererGlyphIdleTrimMs);
    if (!deadline || now_ms < deadline)
        return false;
    if (owner->color_batch)
        owner->color_batch->reset();
    GlyphAtlasAllocator& atlas = owner->allocator;
    glyph_atlas_clear_pins(&atlas);
    for (u32 i = 0; i < owner->title_quad_count; ++i) {
        // Mono tiles take u in [0, 1), colour tiles in [2, 3); decorations and masks sample no tile.
        const GlyphQuad& quad = owner->title_quads[i];
        const float u = quad.u0 >= 2.0f ? quad.u0 - 2.0f : quad.u0;
        if (!(u >= 0.0f && u < 1.0f))
            continue;
        const u32 x = static_cast<u32>(u * atlas.width + 0.5f), y = static_cast<u32>(quad.v0 * atlas.height + 0.5f);
        for (u32 slot = 0; slot < atlas.entry_capacity; ++slot) {
            const GlyphAtlasEntry& entry = atlas.entries[slot];
            if (entry.occupied && entry.x == x && entry.y == y) {
                glyph_atlas_pin(&atlas, GlyphAtlasHandle{static_cast<u16>(slot), entry.generation});
                break;
            }
        }
    }
    const bool evicted = glyph_atlas_trim(&atlas) != 0;
    sync_surfaces(owner, context);
    if (evicted)
        text_storage(owner)->geometry_stale = true;
    return evicted;
}

bool d3d11_glyph_text_take_stale(D3D11GlyphTextOwner* owner) noexcept {
    if (!owner || !owner->atlas_origins || !text_storage(owner)->geometry_stale)
        return false;
    text_storage(owner)->geometry_stale = false;
    return true;
}

void d3d11_glyph_text_release(D3D11GlyphTextOwner* owner) noexcept {
    if (!owner) {
        return;
    }
    if (owner->color_batch)
        owner->color_batch->reset();
    owner->color_batch = nullptr;

    if (owner->color) {
        colr_paint_gpu_release(&owner->color->gpu);
        pages_release(owner->color);
        owner->color = nullptr;
    }
    glyph_atlas_gpu_release(&owner->gpu);
    shaped_text_reset(&owner->title);
    for (u32 index = 0; owner->font_faces && index < owner->font_face_count; ++index) {
        native_font_run_release(native_font_session_runs(owner->system.native_fonts),
                                owner->font_faces[index].native_face);
        reset_value<D3D11GlyphTextFontFace>(&owner->font_faces[index]);
    }
    text_system_release(&owner->system);
    arena_release(&owner->storage);
    reset_value<D3D11GlyphTextOwner>(owner);
}

} // namespace maicord
