#include "maicord/text/fonts/instance.hpp"
#include "maicord/core/memory.hpp"
#include "../opentype/sfnt_read.hpp"

#include "maicord/core/virtual_memory.hpp"
#include "maicord/core/win32_kernel.hpp"
#include "maicord/core/win32_process.hpp"
namespace maicord {namespace {
using namespace sfnt_read;
NativeFontResult result(NativeFontError error,SfntStatus font=SfntStatus::ok,u32 native=0) noexcept {return {error,font,native};}
bool valid(const NativeFontInstanceStore& s) noexcept {return s.fonts&&s.slots.data&&s.slots.count;}
NativeFontInstanceSlot* slot(const NativeFontInstanceStore& s,NativeFontInstanceHandle h) noexcept {
    if(!h.slot||h.slot>s.slots.count||!h.generation)return nullptr;
    auto& p=s.slots[h.slot-1];return p.references&&p.generation==h.generation?&p:nullptr;
}

}
// Preparing an instance runs once per face and settings: compiled for size. The
// handle operations below stay /O2.
__attribute__((minsize)) NativeFontResult native_font_instance_acquire(NativeFontInstanceStore* store,NativeFontHandle handle,
    ConstSlice<FontAxisSetting> settings,bool instructions,NativeFontPrepareScratch scratch,
    NativeFontInstanceHandle* output,u32 work,u32* used,usize state_limit) noexcept {
    WorkReport report{work,work,used};
    if(output)reset_value(output);if(!store||!valid(*store)||!output)return result(NativeFontError::invalid_input);
    const auto* face=native_font_get(*store->fonts,handle);if(!face)return result(NativeFontError::invalid_input);
    const auto& geometry=face->geometry;
    if(instructions || settings.count)return result(NativeFontError::font,SfntStatus::unsupported);
    (void)scratch;(void)state_limit;
    u32 free_slot=store->slots.count;
    for(u32 i=0;i<store->slots.count;++i){
        if(!spend(work))return result(NativeFontError::font,SfntStatus::resource_limit);auto& p=store->slots[i];
        if(!p.references){if(free_slot==store->slots.count&&p.generation!=0xffffffffu)free_slot=i;continue;}
        if(p.instance.face.slot!=handle.slot||p.instance.face.generation!=handle.generation)continue;
        if(p.references==0xffffffffu)return result(NativeFontError::capacity);
        ++p.references;*output={i+1,p.generation};return result(NativeFontError::none);
    }
    if(free_slot==store->slots.count)return result(NativeFontError::capacity);
    Arena state{};
    NativeFontInstance instance{handle,{&geometry}};
    if(!native_font_retain(store->fonts,handle))return result(NativeFontError::capacity);
    auto& p=store->slots[free_slot];p.instance=instance;p.state=state;reset_value(&state);p.references=1;++p.generation;
    *output={free_slot+1,p.generation};return result(NativeFontError::none);
}
bool native_font_instance_retain(NativeFontInstanceStore* s,NativeFontInstanceHandle h) noexcept {
    if(!s)return false;auto* p=slot(*s,h);if(!p||p->references==0xffffffffu)return false;++p->references;return true;
}
bool native_font_instance_release(NativeFontInstanceStore* s,NativeFontInstanceHandle h) noexcept {
    if(!s)return false;auto* p=slot(*s,h);if(!p)return false;if(--p->references)return true;
    const auto face=p->instance.face;const u32 generation=p->generation;arena_release(&p->state);reset_value(p);p->generation=generation;return native_font_release(s->fonts,face);
}
const NativeFontInstance* native_font_instance_get(const NativeFontInstanceStore& s,NativeFontInstanceHandle h) noexcept {
    const auto* p=slot(s,h);return p?&p->instance:nullptr;
}
} // namespace maicord
