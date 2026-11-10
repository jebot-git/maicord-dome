#include "effect_inputs.hpp"
#include "maicord/core/arena.hpp"
#include "maicord/render/d3d11/renderer.hpp"
#include "maicord/text/fonts/run.hpp"
#include "maicord/text/opentype/bitmap_color.hpp"

namespace maicord {
// Plugin glyph effects sample the same bitmap strikes as the UI. No font outlines.
bool effect_glyph_grid_build(D3D11Renderer* renderer,GpuSrv** view) noexcept {
    *view=nullptr;
    if(!renderer || !renderer->glyph_text.ready)return false;
    Arena arena{};if(!arena_initialize(&arena,32u<<20,0,64u<<10))return false;
    struct Release {Arena& a;~Release() noexcept {arena_release(&a);}} release{arena};
    auto* pixels=arena_allocate_zero<u8>(&arena,512*512);if(!pixels)return false;
    const auto permanent=arena_mark(&arena);
    TextSystem system=renderer_text_system(renderer,UiTextFace::mono);
    for(u32 code=32;code<256;++code) {
        if(code==127 || (code>=128 && code<160))continue;
        char text[2];const u32 bytes=code<128?1:2;
        text[0]=code<128?code:0xc0|(code>>6);text[1]=0x80|(code&63);
        TextGlyph glyphs[8];TextRun runs[4];u16 clusters[8];ShapedText shaped{};
        if(!shaped_text_initialize(&shaped,{glyphs,8},{runs,4},{clusters,8}) ||
           !text_shape_utf8(&system,{{text,bytes},{},200.f,200.f},&shaped).ok())continue;
        if(shaped.run_count && shaped.glyph_count) {
            const auto* record=native_font_run_get(*native_font_session_runs(system.native_fonts),runs[0].native_face);
            const auto* face=record?native_font_get(*native_font_session_fonts(system.native_fonts),record->source.face):nullptr;
            CbdtFont font{};BitmapColorGlyph tile{};
            if(face && cbdt_open(face->geometry.face,&font)==SfntStatus::ok &&
               bitmap_color_glyph_build(font,glyphs[runs[0].first_glyph].index,24,&arena,&tile)==SfntStatus::ok &&
               tile.width<=32 && tile.height<=32) {
                const u32 x=(code&15)*32+(32-tile.width)/2,y=(code>>4)*32+(32-tile.height)/2;
                for(u32 row=0;row<tile.height;++row)for(u32 column=0;column<tile.width;++column)
                    pixels[(y+row)*512+x+column]=tile.pixels[row*tile.width+column]>>24;
            }
        }
        shaped_text_reset(&shaped);arena_rewind(&arena,permanent);
    }
    GpuTexture* texture=nullptr;const GpuInitialData initial{pixels,512};
    const auto status=gpu_texture_create(renderer->graphics.device,{512,512,GpuFormat::r8,GpuUsage::immutable,gpu_bind_srv},&initial,&texture);
    const bool ok=gpu_ok(status) && gpu_ok(gpu_srv_create(renderer->graphics.device,texture,{GpuViewKind::texture,GpuFormat::r8},view));
    gpu_release(texture);return ok;
}
}
