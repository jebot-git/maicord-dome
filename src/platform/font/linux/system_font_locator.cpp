// DomeD's private bitmap catalog. System outline fonts and fontconfig never enter this path.
#include "maicord/platform/font/system_font_locator.hpp"
#include "font_scan.hpp"
#include "font_names.hpp"
namespace maicord {
bool system_font_locator_find(SystemFontLocator* locator,const SystemFontRequest& request,SystemFontFile* output) noexcept {
    return locator && output && font_locator::scan_find(locator,request,output);
}
u32 system_font_locator_role(SystemFontLocator*,SystemFontRole role,SystemFontName* output) noexcept {
    if(!output)return 0;
    font_locator::utf8_to_name(role==SystemFontRole::emoji ? "DomeD Emoji" : "DomeD Bitmap",output);
    return output->count;
}
u32 system_font_locator_fallbacks(SystemFontLocator*,u32 scalar,SystemFontName* output,u32 capacity) noexcept {
    if(!output || scalar>0x10ffff)return 0;
    const char* names[]={"DomeD Fallback","DomeD Emoji"};
    u32 count=0;for(const char* name:names)if(count<capacity)font_locator::utf8_to_name(name,&output[count++]);
    return count;
}
void system_font_locator_idle(SystemFontLocator*) noexcept {}
void system_font_locator_release(SystemFontLocator* locator) noexcept {if(locator)font_locator::scan_release(locator);}
u32 system_font_language(char (&output)[16]) noexcept {output[0]='e';output[1]='n';output[2]=0;return 2;}
}
