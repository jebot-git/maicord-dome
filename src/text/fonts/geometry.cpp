#include "maicord/text/fonts/geometry.hpp"
#include "maicord/core/memory.hpp"
namespace maicord {
SfntStatus font_geometry_open(const SfntFace& face,FontGeometry* output) noexcept {
    if(!output)return SfntStatus::invalid_input;
    reset_value(output);
    // Reject hybrid fonts too: installing a vector face can never enable outline text.
    ConstSlice<u8> bytes{};
    const u32 outlines[]={sfnt_tag('g','l','y','f'),sfnt_tag('C','F','F',' '),sfnt_tag('C','F','F','2'),sfnt_tag('C','O','L','R')};
    for(u32 tag : outlines)
        if(sfnt_table(face,tag,&bytes)==SfntStatus::ok)return SfntStatus::unsupported;
    if(sfnt_table(face,sfnt_tag('C','B','L','C'),&bytes)!=SfntStatus::ok ||
       sfnt_table(face,sfnt_tag('C','B','D','T'),&bytes)!=SfntStatus::ok)return SfntStatus::unsupported;
    FontGeometry font{};font.face=face; font.kind=FontGeometryKind::bitmap;
    const auto status=sfnt_horizontal_metrics_open(face,&font.metrics);
    if(status==SfntStatus::ok)*output=font;
    return status;
}
}
