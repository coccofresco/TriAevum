// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <utility>

namespace Mm3dDekui::ButtonGlyphEmission {
// EUR v1.1 native glyph renderer. r1 is the command count, r2 the pane,
// r3 a 15-word glyph record. Main/shadow call sites are both inside the
// already visibility-validated widget renderer. Declining executes the whole
// original compiled function; replacing returns directly to the native caller.
inline constexpr uint32_t Pc=0x00144c74,MainReturn=0x00134f20,ShadowReturn=0x00134ef8;
inline constexpr std::array<std::pair<uint32_t,uint32_t>,13> Guards{{
    {Pc,0xe92d4ff0},{Pc+4,0xe1a04000},{Pc+8,0xe1a05003},
    {Pc+12,0xe1a06002},{Pc+16,0xe1a0a001},{0x144c9c,0xe59d70c8},
    {0x134ef4,0xeb003f5e},{0x134f1c,0xeb003f54},
    {0x144d10,0xe5952000},{0x144d94,0xe2863e11},{0x144e54,0xe28630e0},
    // Native inline-icon builder resolves the live texture descriptor/pixels.
    {0x5fe038,0xe590004c},{0x5fe03c,0xe5900020},
}};
}
