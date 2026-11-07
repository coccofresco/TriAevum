#pragma once

#include "mm3d_guest_patches.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace Mm3dDekui {
class Core;
struct NativeQuadQueueMove;
}

namespace Oot3dNativeGame {

class Mm3dRestorationPort;

// Majora's Mask 3D's single-screen mode: mm3d-dekui's guest side
// (titles/mm3d/dekui, ported onto the vanilla v1.1 code.bin) run the way
// dekui's emulator runs it. Every PC dekui may change is a guest patch site;
// once the image is mounted the ported substitution chain gives the word
// dekui would fetch there, and the site is enabled when that differs from
// the guest's. An SVC replacement runs the ported SVC dispatch (its
// condition checked, r15 = pc + 4 beforehand, the guest continuing at r15
// afterwards); any other replacement word is executed as it is. The
// settings are dekui's own (its module atomics).
class Mm3dSingleScreen {
  public:
    // The global time dekui's hooks animate with, in microseconds; the
    // window passes its deterministic presentation clock.
    explicit Mm3dSingleScreen(std::function<std::int64_t()> globalTimeUs = {});
    ~Mm3dSingleScreen();
    Mm3dSingleScreen(const Mm3dSingleScreen&) = delete;
    Mm3dSingleScreen& operator=(const Mm3dSingleScreen&) = delete;

    // Adds every candidate PC to patches; this object must outlive them.
    void Register(Mm3dGuestPatches& patches);
    static std::vector<std::uint32_t> CandidatePcs();
    // dekui runs on Project Restoration: its sites that check Restoration's
    // appended code accept the port's verified sites instead, and the port's
    // calls into dekui reach this object's Core. Bind before Verify; the port
    // must outlive this object's sites.
    void BindRestoration(Mm3dRestorationPort& restoration);
    using QuadQueueObserver = std::function<void(std::span<const Mm3dDekui::NativeQuadQueueMove>,
        oot3d::recomp::a32::MemoryBus&)>;
    void SetQuadQueueObserver(QuadQueueObserver observer) { mQuadQueueObserver = std::move(observer); }
    // dekui's settings (its presentation.json): the file at path, or
    // dekui's shipped defaults when path is empty or missing. Applied as
    // dekui applies them at startup; call before the guest runs. Returns
    // false if a given file could not be read (the defaults apply then).
    static bool LoadSettings(const std::filesystem::path& path);
    // dekui's difficulty option (1..16), until the F1 menu owns its settings.
    static void SetDamageMultiplier(int multiplier);

  private:
    // dekui's headers stay out of the window's translation units.
    std::unique_ptr<Mm3dDekui::Core> mCore;
    std::map<std::uint32_t, std::uint32_t> mReplacements;
    Mm3dRestorationPort* mRestoration = nullptr;
    QuadQueueObserver mQuadQueueObserver;
};

} // namespace Oot3dNativeGame
