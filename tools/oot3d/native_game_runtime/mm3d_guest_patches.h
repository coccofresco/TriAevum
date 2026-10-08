#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

// Majora's Mask 3D's single-screen mode (mm3d-dekui) and the Project
// Restoration it builds on change the game by replacing instructions at fixed
// code addresses: the emulator substitutes the word it fetches there (mostly
// with an SVC trap whose handler runs host code) when the surrounding words
// still read as the code the change was written against. Here each such site
// is a typed replacement: the registry starts a block at its PC (and at the
// next instruction), the A32 runtime calls the site's handler instead of
// executing that instruction, and the handler continues the guest at the PC
// it returns. The guest image is never modified.
struct Mm3dGuestPatchSite {
    // The replaced instruction.
    std::uint32_t Pc = 0;
    // The code the replacement was written against, as {address, word}
    // pairs; the site is enabled only if every word matches the mounted
    // image.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> Guard;
    std::string Name;
    // Runs in place of the instruction at Pc, on the guest thread's state
    // and memory, and returns the PC the guest continues at (Pc + 4 to fall
    // through), or nothing to execute the original instruction instead.
    std::function<std::optional<std::uint32_t>(oot3d::recomp::a32::GuestState&,
                                               oot3d::recomp::a32::MemoryBus&)>
        Handler;
    // Optional: runs once the guards match, on the mounted image (a site
    // that derives its replacement from the code there); false disables it.
    std::function<bool(oot3d::recomp::a32::MemoryBus&)> Prepare;
    // False for a PC outside guest code (a host return sentinel), which the
    // runtime dispatches without a registry block.
    bool InGuestCode = true;
};

class Mm3dGuestPatches {
  public:
    struct SiteStatistics {
        std::string Name;
        std::uint32_t Pc = 0;
        bool Enabled = false;
        // The first guard word that did not match, when disabled.
        std::optional<std::uint32_t> MismatchAddress;
        // Disabled because the registry has no block at the site's PC.
        bool MissingBlock = false;
        // Disabled by the site's own preparation.
        bool Prepared = true;
        std::uint64_t Replacements = 0;
        std::uint64_t Declined = 0;
    };

    // Throws std::invalid_argument for a second site at the same PC, a
    // misaligned PC or a site without a handler.
    void Add(Mm3dGuestPatchSite site);
    // Checks every site's guard against the mounted image and enables the
    // sites whose code matches and, given the registry, that start a block
    // in it (a site inside a block would only run when branched to);
    // returns how many are enabled.
    std::size_t Verify(oot3d::recomp::a32::MemoryBus& memory,
                       const oot3d::recomp::a32::Registry* registry = nullptr);
    // The enabled sites' PCs, sorted: the native-function filter.
    std::vector<std::uint32_t> EnabledPcs() const;
    // Guest PCs a handler may continue at besides a site's next
    // instruction (a branch into the middle of a function): each must start
    // a registry block too, as the runtime does not interpret code outside
    // the registry.
    void AddTargets(const std::vector<std::uint32_t>& targets);
    // The registry boundaries the sites need: each site's PC, the
    // instruction after it and the declared targets, sorted and unique.
    std::vector<std::uint32_t> BlockBoundaries() const;
    std::vector<SiteStatistics> Statistics() const;
    bool Empty() const noexcept { return mSites.empty(); }

    // oot3d::recomp::a32::NativeFunctionCallback over a Mm3dGuestPatches.
    static bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState& state,
                        oot3d::recomp::a32::MemoryBus& memory,
                        oot3d::recomp::a32::ExecutionResult* result, void* user);

  private:
    struct Entry {
        Mm3dGuestPatchSite Site;
        SiteStatistics Stats;
    };
    Entry* Find(std::uint32_t pc) noexcept;
    std::vector<Entry> mSites; // sorted by PC
    std::vector<std::uint32_t> mTargets;
};

// Whether the condition field of an A32 instruction word passes for CPSR,
// as a replaced conditional instruction (or its conditional trap) must check.
bool Mm3dConditionPassed(std::uint32_t instruction, std::uint32_t cpsr) noexcept;

} // namespace Oot3dNativeGame
