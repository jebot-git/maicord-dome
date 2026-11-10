#pragma once
#include "maicord/core/base.hpp"
namespace maicord::dbus {
struct ApplicationMenuEntry {
    u32 parent;
    const char* label;
    u32 command;
    bool enabled=true;
    bool checkable=false;
    bool checked=false;
    const char* modifier=nullptr;
    const char* key=nullptr;
};
using ApplicationMenuHandler=void(*)(u32 command) noexcept;
using ApplicationMenuBind=void(*)(const char* service,const char* path) noexcept;
// Entries have stable 1-based IDs. A parent must precede its children; 0 is the menu bar.
bool application_menu_start(ApplicationMenuHandler,ApplicationMenuBind) noexcept;
void application_menu_update(const ApplicationMenuEntry*,u32 count) noexcept;
void application_menu_stop() noexcept;
}
