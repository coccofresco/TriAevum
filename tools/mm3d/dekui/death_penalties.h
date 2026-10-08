// Ported from mm3d-dekui src/mm3d/death_penalties.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace Mm3dDekui::DeathPenalties {
using U32 = std::uint32_t;
inline std::atomic<bool> lose_rupees{false};
inline std::atomic<bool> world_ends{false};
constexpr U32 FirstSVC = 0xfffc80;
constexpr U32 DeathPC = 0x00206134, DeathWord = 0xe1c01abc; // ref 0x206344
constexpr U32 ContinuePC = 0x0016f8bc, ContinueWord = 0xeb00841f; // ref 0x16fa18
constexpr U32 NativeRespawn = 0x00190940, NativeMoonCrash = 0x001bfbc0; // ref 0x190a9c, 0x1bfdc8
constexpr U32 ContinueReturn = 0x0016f908; // ref 0x16fa64
constexpr U32 Wallet = 0x775318, Accumulator = 0x7751fc, Health = 0x775314;
constexpr U32 GameOverOffset = 0x86ac;
struct Signature { U32 start, size, hash; };
// USA Restoration binary: fairy lookup/consumption, the no-fairy branch,
// death-state progression, ordinary respawn, and the complete moon-crash routine.
// Hash the original guest code, never translated SVC instructions.
constexpr std::array<Signature, 6> Signatures{{
    {0x002060a0, 0x38, 0x995c1962}, {0x00206130, 0x44, 0x6eaeefab}, // ref 0x2062b0, 0x206340
    {0x0016f854, 0xb8, 0x04a8cdc4}, {0x001bfbc0, 0x1c4, 0x9a938074}, // ref 0x16f9b0, 0x251abe27, 0x1bfdc8, 0xefb7f3e9
    {0x0020e9c8, 0x110, 0x3b0c287a}, {0x00190940, 0x64, 0x1965b18b}, // ref 0x20ebd8, 0x5766bf1a, 0x190a9c
}};
inline U32 Instruction(U32 pc, U32 word) {
    if (pc == DeathPC && word == DeathWord) return 0xef000000 | FirstSVC;
    if (pc == ContinuePC && word == ContinueWord) return 0xef000000 | (FirstSVC + 1);
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for (const auto& signature : Signatures) {
        U32 hash = 2166136261u;
        for (U32 offset = 0; offset < signature.size; offset += 4) {
            auto word = m.Read(signature.start + offset);
            if (!word) return false;
            for (unsigned byte = 0; byte < 4; ++byte)
                hash = (hash ^ ((*word >> (8 * byte)) & 255)) * 16777619u;
        }
        if (hash != signature.hash) return false;
    }
    return true;
}
inline bool OnMoon(U32 scene) {
    // Majora, Deku/Goron/Zora/Link trials, and the Moon field.
    return scene == 0x0b || scene == 0x2a || scene == 0x3f ||
           scene == 0x47 || scene == 0x66 || scene == 0x67;
}
template<class M> std::optional<U32> Half(M& m, U32 at) {
    auto word = m.Read(at & ~3u);
    if (!word) return std::nullopt;
    return (*word >> ((at & 2) * 8)) & 0xffff;
}
template<class M> void StoreHalf(M& m, U32 at, U32 value) {
    const U32 aligned = at & ~3u, shift = (at & 2) * 8;
    if (auto word = m.Read(aligned))
        m.Write(aligned, (*word & ~(0xffffu << shift)) | ((value & 0xffff) << shift));
}
template<class M> bool PlayContext(M& m, U32 ctx) {
    // State::status=Running, State::type=Play. Reject stale/unmapped contexts.
    auto state = m.Read(ctx + 0x13c);
    return ctx != 0 && (ctx & 3) == 0 && state && (*state & 0xffff) == 0x0402;
}
template<class M> void ClearWallet(M& m) {
    // Clear pending rupee animation too, so a pickup before death cannot refill
    // the wallet afterwards. Preserve the adjacent form/equipment halfwords.
    if (!m.Read(Wallet) || !m.Read(Accumulator)) return;
    StoreHalf(m, Wallet, 0);
    StoreHalf(m, Accumulator, 0);
}
template<class M, class R> void Death(M& m, R& r, bool remove_rupees) {
    // This STRH is reached only after the native seven-bottle fairy scan fails.
    // Pits/void respawns and successful fairy revivals never reach this site.
    if (remove_rupees && PlayContext(m, r[6]) && r[0] == r[6] + 0x8600 &&
        r[1] == 1 && Half(m, Health) == 0)
        ClearWallet(m);
    StoreHalf(m, r[0] + 0xac, r[1]); // displaced STRH r1,[r0,#0xac]
}
template<class M, class R> void Continue(M& m, R& r, bool end_world) {
    const U32 ctx = r[0];
    auto scene = Half(m, ctx + 0x148);
    auto health = Half(m, Health);
    if (end_world && PlayContext(m, ctx) && r[9] == ctx &&
        r[6] == ctx + GameOverOffset && Half(m, r[6]) == 3 &&
        scene && *scene <= 0x74 && !OnMoon(*scene) && health && *health == 0) {
        // Finish the death state before changing scenes. Native ordinary death
        // also restores three hearts and advances state3->4 at this point.
        StoreHalf(m, Health, 0x30);
        StoreHalf(m, r[6], 4);
        // Call the native moon-crash entry, which sets day/time, both entrance
        // fields, cutscene ID, transition and audio. Skip the ordinary respawn
        // writes after this call; they would overwrite the crash transition.
        r[14] = ContinueReturn;
        r[15] = NativeMoonCrash;
        return;
    }
    r[14] = ContinuePC + 4;
    r[15] = NativeRespawn; // displaced BL, including every disabled/Moon case
}
} // namespace Mm3dDekui::DeathPenalties
