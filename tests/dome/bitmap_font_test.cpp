#include "maicord/core/arena.hpp"
#include "maicord/core/byte_order.hpp"
#include "maicord/os/file.hpp"
#include "maicord/sys/linux/path.hpp"
#include "maicord/sys/linux/process.hpp"
#include "maicord/text/fonts/geometry.hpp"
#include "maicord/text/opentype/bitmap_color.hpp"
#include "../support/test_output.hpp"
namespace maicord {
static u32 failures=0,checks=0;
void check(bool value,const char* name) noexcept {
    ++checks;if(!value){++failures;test_write_text("FAIL ");test_write_text(name);test_write_text("\n");}
}
}
MAICORD_TEST_ENTRY(domed_bitmap_entry) {
    using namespace maicord;
    wchar_t path[1024];
    const char* fixture=environment_get("DOMECORD_BITMAP_FIXTURE");
    check(fixture && path_from_utf8(fixture,path,1024),"fixture_path");
    if(failures)test_process_exit(1);
    auto file=file_open(path,{FileAccess::read,FileCreate::existing,kFileShareRead,0});
    FileMap map{};check(file_map(file,false,0,&map),"map");
    if(failures)test_process_exit(1);
    SfntFace face{};check(sfnt_open({static_cast<const u8*>(map.data),map.bytes},0,&face)==SfntStatus::ok,"sfnt");
    FontGeometry geometry{};check(font_geometry_open(face,&geometry)==SfntStatus::ok,"bitmap_face");
    CbdtFont font{};check(cbdt_open(face,&font)==SfntStatus::ok && font.pixelated,"bitmap_marker");
    SfntCmap cmap{};u16 glyph=0;check(sfnt_cmap_open(face,&cmap)==SfntStatus::ok &&
        sfnt_cmap_glyph(cmap,'A',0,&glyph)==SfntStatus::ok && glyph==1,"bitmap_cmap");
    Arena arena{};check(arena_initialize(&arena,8u<<20,0,4096),"arena");
    const float sizes[]={8,12,16,23.25f};
    for(float size:sizes) {
        arena_reset(&arena);BitmapColorGlyph tile{};
        const bool built=bitmap_color_glyph_build(font,glyph,size,&arena,&tile)==SfntStatus::ok;
        check(built,"bitmap_decode");
        if(!built)continue;
        bool binary=true;for(u32 i=0;i<tile.width*tile.height;++i)binary&=tile.pixels[i]==0 || tile.pixels[i]==0xffffffffu;
        check(binary,"binary_pixels_at_every_size");
        if(size==8) {
            check(tile.width==5 && tile.height==7 && tile.top==-7,"native_metrics");
            const u8 expected[]={0x70,0x88,0x88,0xf8,0x88,0x88,0x88};
            bool exact=true;for(u32 y=0;y<7;++y)for(u32 x=0;x<5;++x)
                exact&=(tile.pixels[y*5+x]!=0)==((expected[y]>>(7-x))&1);
            check(exact,"exact_bdf_pixels");
        }
    }
    const u32 tags[]={sfnt_tag('g','l','y','f'),sfnt_tag('C','F','F',' '),sfnt_tag('C','F','F','2'),sfnt_tag('C','O','L','R')};
    for(u32 tag:tags) {
        u8 entry[16]{};entry[0]=tag>>24;entry[1]=tag>>16;entry[2]=tag>>8;entry[3]=tag;
        SfntFace outline=face;outline.directory={entry,16};
        check(font_geometry_open(outline,&geometry)==SfntStatus::unsupported,"reject_outline_font");
        // Preserve every valid bitmap table and add an outline table: reject genuine hybrids too.
        u8 directory[32*16]{};u32 at=0;bool inserted=false;
        for(u32 i=0;i<face.directory.count/16;++i) {
            const auto* current=face.directory.data+i*16;
            if(!inserted && byte_load_be32(current)>tag) {
                copy_bytes(directory+at,entry,16);at+=16;inserted=true;
            }
            copy_bytes(directory+at,current,16);at+=16;
        }
        if(!inserted){copy_bytes(directory+at,entry,16);at+=16;}
        outline.directory={directory,at};
        check(font_geometry_open(outline,&geometry)==SfntStatus::unsupported,"reject_bitmap_outline_hybrid");

    }
    arena_release(&arena);file_unmap(&map);file_close(file);
    test_write_text("checks=");test_write_u64(checks);test_write_text(failures?" FAIL\n":" PASS bitmap-only\n");
    test_process_exit(failures?1:0);
}
