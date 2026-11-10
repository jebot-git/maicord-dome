#include "maicord/platform/font/default_fonts.hpp"
namespace maicord {
const wchar_t* default_font_family(DefaultFont role) noexcept {
    return role==DefaultFont::emoji ? L"DomeD Emoji" : L"DomeD Bitmap";
}
ConstSlice<u16> default_font_name(DefaultFont role) noexcept {
    const auto* text=default_font_family(role);u32 n=0;while(text[n])++n;
    return {reinterpret_cast<const u16*>(text),n};
}
bool default_fonts_set_portal(const char*,const char*) noexcept {return true;}
}
