#pragma once

#include "difficulty.h"
#include "mm3d_guest_patches.h"

namespace Oot3dNativeGame {

// Majora's Mask 3D's single-screen mode, integrated from mm3d-dekui (and the
// Project Restoration it builds on) onto the vanilla v1.1 code.bin: its
// settings and the guest patch sites that implement it
// (docs/MOONFALL_CURSE.md, "Guest patch sites").
struct Mm3dSingleScreenSettings {
    Mm3dDekui::Difficulty::Settings Difficulty;
};

// Adds every site to patches. The handlers read settings when they run, so
// a change takes effect at the next replaced instruction; settings must
// outlive patches.
void RegisterMm3dSingleScreenSites(Mm3dGuestPatches& patches,
                                   const Mm3dSingleScreenSettings& settings);

} // namespace Oot3dNativeGame
