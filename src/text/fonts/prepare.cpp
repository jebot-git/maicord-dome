#include "maicord/text/fonts/prepare.hpp"
#include "maicord/core/memory.hpp"
namespace maicord {
NativeFontResult native_font_prepare(NativeFontSession* session,NativeFontHandle face,ConstSlice<FontAxisSetting> settings,
    u32 simulations,bool,Arena* scratch,NativeFontInstanceHandle* output,bool* oblique,bool* bold) noexcept {
    if(oblique)*oblique=false;if(bold)*bold=false;if(output)reset_value(output);
    if(!session||!scratch||!scratch->base||!output||simulations>3 || (settings.count&&!settings.data))
        return {NativeFontError::invalid_input,SfntStatus::invalid_input,0};
    // Bitmap strikes never execute font programs, synthesize contours or apply variable axes.
    return native_font_session_instance(session,face,{},false,{},output);
}
}
