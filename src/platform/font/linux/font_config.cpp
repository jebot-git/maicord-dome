#include "font_config.hpp"
#include "maicord/sys/linux/process.hpp"
#include "maicord/sys/linux/path.hpp"
#include "maicord/platform/font/system_font_locator.hpp"
namespace maicord::font_locator {
void font_roots_collect(Arena* arena, FontRoots* roots) noexcept {
    roots->count=0;
    const char* directory=environment_get("DOMECORD_FONT_DIR");
    if(!directory || directory[0]!='/')return;
    auto* wide=arena_allocate<wchar_t>(arena,kSystemFontPathCapacity);
    if(wide && path_from_utf8(directory,wide,kSystemFontPathCapacity)) {
        roots->paths[0]=wide;roots->count=1;
    }
}
} // namespace maicord::font_locator
