// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from mm3d-dekui (tgacquin/mm3d-dekui v1.0.0-beta.1,
// src/mm3d/difficulty.h): the damage multiplier and the disabled drops of its
// difficulty options. Only the vanilla v1.1 sites are kept; the settings are
// passed in instead of read from globals.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace Mm3dDekui::Difficulty {

using U32 = std::uint32_t;

struct Settings {
    int DamageMultiplier = 1; // 1..16
    bool DisableHeartDrops = false;
    bool DisableRupeeDrops = false;
    bool DisableFairyDrops = false;
    bool DisableMagicDrops = false;
    bool DisableAmmoPickups = false;
};

// The health-change routine: the instruction adding the (defence-reduced)
// change to the current health, ADD r2, r2, r4.
constexpr U32 kDamagePc = 0x0021E34CU;
constexpr U32 kDamageWord = 0xE0822004U;
// The routine around it, from kDamagePc - 0x38 (defence reduction, healing
// sound, signed clamp, return and its literal pool).
constexpr std::array<U32, 29> kHealthCode{
    0xe92d4010, 0xe2514000, 0xda000002, 0xe59f0054, 0xebff886a, 0xea000004,
    0xe59f004c, 0xe5d00148, 0xe3500000, 0x13540000, 0xb1a040c1, 0xe59f003c,
    0xe1d023bc, 0xe1d013fa, kDamageWord, 0xe6bf2072, 0xe1520001, 0xe1c023bc,
    0xc1c013bc, 0xe1d013fc, 0xe3510000, 0xd3a01000, 0xd1c013bc, 0xd1a00001,
    0xc3a00001, 0xe8bd8010, 0x01000777, 0x007751d8, 0x007752d8};
constexpr U32 kHealthCodeAddress = kDamagePc - 0x38U;

// ADD r2, r2, r4 after the native defence reduction. Only damage changes; an
// amplified loss is clamped to the current health before the native SXTH so
// it cannot wrap around.
inline U32 ApplyDamage(U32 health, U32 delta, int multiplier) {
    const auto signedDelta = std::bit_cast<std::int32_t>(delta);
    multiplier = std::clamp(multiplier, 1, 16);
    if (multiplier == 1 || signedDelta >= 0 || health > 0x7fff) {
        return health + delta;
    }
    const auto loss = -std::int64_t(signedDelta) * multiplier;
    return loss >= health ? 0 : health - U32(loss);
}

// The Actor_Spawn wrapper's entry, STMFD sp!, {r4-r6, lr}.
constexpr U32 kSpawnPc = 0x0022053CU;
constexpr U32 kSpawnWord = 0xE92D4070U;
constexpr std::array<U32, 14> kSpawnCode{
    kSpawnWord, 0xe24dd020, 0xe28d4030, 0xe3a0c001, 0xecd41a03, 0xe28de00c, 0xe06c550c,
    0xe3e04000, 0xe3a06000, 0xe88e1070, 0xeccd1a03, 0xebff5112, 0xe28dd020, 0xe8bd8070};

// En_Item00 (actor 0xE): recovery heart 3; rupees 0, 1, 2, 0x13, 0x14; magic
// jars 0xE/0xF; ammo: bombs 4/0xB, arrows 5/8/9/0xA, nuts 0xC/0x17, stick
// 0xD (the zero-ammo bomb placeholder 0x19 is not a refill). Verified by
// dekui against the native pickup switch at 0x428C24; high flags do not
// change the type. En_Elf (actor 0x10) type 2 (low nibble) is the timed
// healing fairy of the native drop paths; its other roles pass.
inline bool SuppressDrop(U32 actor, U32 params, const Settings& settings) {
    if (settings.DisableFairyDrops && actor == 0x10 && (params & 15) == 2) {
        return true;
    }
    const U32 type = params & 255;
    return actor == 0xe &&
           ((settings.DisableHeartDrops && type == 3) ||
            (settings.DisableRupeeDrops && (type <= 2 || type == 0x13 || type == 0x14)) ||
            (settings.DisableMagicDrops && (type == 0xe || type == 0xf)) ||
            (settings.DisableAmmoPickups &&
             (type == 4 || type == 5 || (type >= 8 && type <= 0xd) || type == 0x17)));
}

// At the wrapper's entry (r2 = actor ID, the seventh argument at SP + 8 =
// params): a suppressed drop returns the normal null-spawn result without
// allocating; anything else replays the STMFD. Returns the next PC.
template <class M, class Registers>
U32 Spawn(M& m, Registers& regs, const Settings& settings) {
    const U32 sp = regs[13];
    const auto params = m.Read(sp + 8);
    if (params && SuppressDrop(regs[2], *params, settings)) {
        regs[0] = 0;
        return regs[14];
    }
    for (unsigned i = 0; i < 3; ++i) {
        m.Write(sp - 16 + 4 * i, regs[4 + i]);
    }
    m.Write(sp - 4, regs[14]);
    regs[13] = sp - 16;
    return kSpawnPc + 4;
}

} // namespace Mm3dDekui::Difficulty
