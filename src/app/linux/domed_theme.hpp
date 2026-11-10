#pragma once
#include "maicord/sys/linux/process.hpp"
#include "maicord/platform/dbus/desktop_settings.hpp"
#include "maicord/core/color_hex.hpp"
#include "maicord/ui/theme/presets.hpp"

namespace maicord::app_detail {
// Match the shared DomeD/FLTK palette, with square controls and a solid background.
// The portal keeps light/dark and accent current; launcher values cover absent portals.
inline Theme domed_theme() noexcept {
    dbus::DesktopSettings desktop{};
    const bool portal=dbus::desktop_settings_get(&desktop);
    const char* mode=environment_get("DOMECORD_LIGHT");
    const bool light=portal && desktop.color_scheme!=dbus::ColorScheme::no_preference
        ? desktop.color_scheme==dbus::ColorScheme::light : mode && mode[0]=='1';
    Theme theme=light ? kLightTheme : kDefaultTheme;
    theme.base_lowest=theme.base_lower=color_from_rgb24(light ? 0xe5e9ef : 0x1f252d);
    theme.base_low=theme.surface=color_from_rgb24(light ? 0xffffff : 0x161c23);
    theme.surface_high=theme.interactive=color_from_rgb24(light ? 0xd4dce7 : 0x303b49);
    theme.text_primary=color_from_rgb24(light ? 0x1e252e : 0xe7ebf1);
    theme.text_secondary=color_from_rgb24(light ? 0x506074 : 0x9eafc4);
    u32 accent=0x6babff;
    const char* text=environment_get("DOMECORD_ACCENT");
    if (text) {u32 n=0;while(text[n] && n<16)++n;(void)color_hex_parse_rgb24({text,n},&accent);}
    theme.accent=portal && desktop.accent_valid
        ? Color{desktop.accent[0],desktop.accent[1],desktop.accent[2],1} : color_from_rgb24(accent);
    theme.scale.radius_xs=theme.scale.radius_sm=theme.scale.radius_md=theme.scale.radius_lg=
        theme.scale.radius_xl=theme.scale.window_radius=1;
    theme.brand_marks=false;
    theme=ui_theme_resolve_defaults(theme);
    theme.tokens.border.subtle=theme.tokens.border.strong=color_from_rgb24(light ? 0xaebac9 : 0x47576a);
    return theme;
}
}
