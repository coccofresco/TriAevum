// Ported from mm3d-dekui src/mm3d/submenu_header.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
// Settings metadata only. GAR archive installation belongs to the host setup layer.
namespace Mm3dDekui::SubmenuHeader {
struct Options {
    float width=480.f, y=0.f;
    float title_scale=1.f, title_x=0.f, title_y=0.f;
    float time_scale=1.f, time_x=0.f, time_y=0.f;
};
struct Field { const char* label; const char* key; float Options::*member; float lo,hi; };
inline constexpr std::array<Field,8> Fields{{
    {"Background width","header_width",&Options::width,400,540},
    {"Header Y","header_y",&Options::y,-60,40},
    {"Title scale","header_title_scale",&Options::title_scale,.5f,1.3f},
    {"Title X","header_title_x",&Options::title_x,-80,80},
    {"Title Y","header_title_y",&Options::title_y,-40,40},
    {"Day/time scale","header_time_scale",&Options::time_scale,.6f,1.3f},
    {"Day/time X","header_time_x",&Options::time_x,-100,50},
    {"Day/time Y","header_time_y",&Options::time_y,-40,40},
}};
inline Options Original() { Options o; o.width=400; return o; }
inline Options Widescreen() { return {240.f*16.f/9.f,-6.f,.8f,-13.333333f,1.f,1.f,13.333333f,0.f}; }
}
