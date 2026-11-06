// Ported from tgacquin/mm3d-dekui 6e36dfb4 src/mm3d/owl_intro.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

namespace Mm3dDekui::OwlIntro {
using U32 = std::uint32_t;
inline std::atomic<bool> disabled{false};
constexpr U32 Intro = 0x0043fb78, IntroPC = 0x0043fb7c, IntroWord = 0xe1a04000;
constexpr U32 IntroReturn = 0x0043fc50, Idle = 0x0058da44;
constexpr U32 TatlPC = 0x0047b4a0, TatlWord = 0xe5d002d3, FirstSVC = 0xfffe25;
// USA Restoration: first-statue proximity state and the specific Tatl 0x0282
// reminder gate. Do not mark the tutorial seen in the persistent event flags.
inline constexpr std::array<std::pair<U32, U32>, 21> Guards{{
    {Intro, 0xe92d4070}, {IntroPC, IntroWord},
    {0x0043fc4c, 0xe3a00001}, {IntroReturn, 0xe8bd8070},
    {0x0043fc60, 0x007761d8}, {0x004896e8, Intro},
    {0x0047b16c, 0xe92d4ff0}, {0x0047b498, 0xe59f0268},
    {TatlPC, TatlWord}, {0x0047b4a4, 0xe3100010}, {0x0047b4ac, 0x0a000001},
    {0x0047b4dc, 0x159f0228}, {0x0047b4e8, 0xe1c60cbc},
    {0x0047b708, 0x007761d8}, {0x0047b70c, 0x00000282},
    {Idle, 0xe1d021fc}, {0x0058da4c, 0xe3a00000}, {0x0058da88, 0xe12fff1e},
    {0x0058da90, 0x007752d8}, {0x0058da94, 0x00626c5c}, {0x0058da98, 0x00000c01}
}};
template<class M> bool CodeMatches(M& m) {
    for (auto [at, word] : Guards) if (m.Read(at) != word) return false;
    return true;
}
inline U32 Instruction(U32 pc, U32 word) {
    if (pc == IntroPC && word == IntroWord) return 0xef000000 | FirstSVC;
    if (pc == TatlPC && word == TatlWord) return 0xef000000 | (FirstSVC + 1);
    return word;
}
template<class R> void Enter(R& r) {
    r[4] = r[0]; // Replay MOV after the native push; use its matching pop.
    if (disabled) {
        // Run the normal idle handler to select the correct inactive/active/
        // feather dialogue and return zero (allow A). The native pop then
        // restores the original caller PC saved by the intro prologue.
        r[14] = IntroReturn;
        r[15] = Idle;
    }
}
inline U32 TatlFlags(U32 flags) {
    // Register-only override of this reminder's gate, not all Tatl prompts.
    return disabled ? flags | 0x10u : flags;
}
inline bool AllowsSword(U32 state) { return disabled && state == Intro; }
}
