#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <functional>
#include <array>
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
    // F07 diagnostic only: a second name the env trace prefix may match (the
    // restoration sites are named by hook file, the trace by action, e.g.
    // rst.MainScreen_Init). Never used for dispatch or receipts.
    std::string TraceName;
    // Runs in place of the instruction at Pc, on the guest thread's state
    // and memory, and returns the PC the guest continues at (Pc + 4 to fall
    // through), or nothing to execute the original instruction instead.
    std::function<std::optional<std::uint32_t>(oot3d::recomp::a32::GuestState&,
                                               oot3d::recomp::a32::MemoryBus&)>
        Handler;
    // Optional: runs once the guards match, on the mounted image (a site
    // that derives its replacement from the code there); false disables it.
    // Every site's guard is checked before any preparation runs, so a
    // preparation may depend on which guarded sites are enabled.
    std::function<bool(oot3d::recomp::a32::MemoryBus&)> Prepare;
    // False for a PC outside guest code (a host return sentinel), which the
    // runtime dispatches without a registry block.
    bool InGuestCode = true;
    // False when the guest can never continue at Pc + 4 from this site: the
    // handler never returns Pc + 4 and the replaced instruction is an
    // unconditional return (an observer on a function epilogue whose next
    // word is literal data). Such a site needs no boundary at Pc + 4.
    bool ContinuesAtNext = true;
    // False when the handler itself never returns Pc + 4 (it declines, or
    // continues elsewhere: a declared target, the caller). The product
    // (whole-AOT title module) runs a declined site as the module's code at
    // Pc, so only a handler's own Pc + 4 needs a module boundary there; the
    // registry runtime keeps the boundary ContinuesAtNext gives.
    bool HandlerReturnsNext = true;
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
        // Product return check: continuations the site does not declare.
        std::uint64_t ReturnViolations = 0;
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
    // Whole-AOT product mode: the compiled title module must exit to the
    // dispatcher at every boundary an enabled site needs (its PC, the
    // instruction after it, the declared targets). Given the module's
    // sorted function entry points, returns the boundaries it lacks.
    std::vector<std::uint32_t> MissingProductBoundaries(
        const std::vector<std::uint32_t>& sortedEntryPoints) const;
    bool Empty() const noexcept { return mSites.empty(); }
    // Whole-AOT product mode (G4a-F4): every continuation a handler returns
    // must be one its site declares: Pc + 4 only when HandlerReturnsNext,
    // otherwise a declared target (AddTargets) or the LR the handler was
    // entered with. The title module's boundaries were checked against those
    // declarations (MissingProductBoundaries), so any other PC would leave
    // compiled code at a PC that is no module entry. A violation is counted
    // per site (Statistics, runtime.json) and reported on stderr once.
    void SetProductReturnCheck(bool enabled) noexcept { mProductReturnCheck = enabled; }
    // F07 diagnostic (env TRIAEVUM_MM3D_GUEST_PATCH_TRACE=<site name prefix>,
    // default off): each consult of a site whose name starts with the prefix
    // (a SkipOnce resume, a decline, a replacement and its continuation) is
    // reported to the sink with the site's counters and the trail of the
    // last sites consulted on any thread. An empty prefix turns it off.
    void SetTrace(std::string prefix, std::function<void(const std::string&)> sink);
    // Reports the traced sites' Verify outcome (enabled, mismatch) to the sink.
    void TraceVerify() const;
    std::uint64_t ReturnViolations() const noexcept { return mReturnViolations; }
    // Whether a continuation is declared for the site (the check above).
    bool DeclaredContinuation(const Mm3dGuestPatchSite& site, std::uint32_t next,
                              std::uint32_t lrAtEntry) const noexcept;
    // The next time the guest thread (its TLS pointer) reaches the site at
    // pc, its own instruction runs: a site whose work ran elsewhere (a
    // coroutine that called guest code) resumes the guest at itself.
    void SkipOnce(std::uint32_t pc, std::uint32_t thread);
    // A savestate was restored: pending SkipOnce marks are dropped.
    void ResetPending() noexcept;

    // oot3d::recomp::a32::NativeFunctionCallback over a Mm3dGuestPatches.
    static bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState& state,
                        oot3d::recomp::a32::MemoryBus& memory,
                        oot3d::recomp::a32::ExecutionResult* result, void* user);

  private:
    struct Entry {
        Mm3dGuestPatchSite Site;
        SiteStatistics Stats;
        std::vector<std::uint32_t> SkipThreads;
    };
    Entry* Find(std::uint32_t pc) noexcept;
    std::vector<Entry> mSites; // sorted by PC
    std::vector<std::uint32_t> mTargets;
    bool mProductReturnCheck = false;
    std::string mTracePrefix;
    std::function<void(const std::string&)> mTraceSink;
    std::array<std::uint32_t, 16> mTrail{};
    std::size_t mTrailNext = 0;
    bool TraceMatches(const Entry& entry) const;
    void Trace(const Entry& entry, std::uint32_t thread, std::uint32_t lr, const char* what,
               std::uint32_t next);
    std::uint64_t mReturnViolations = 0;
};

// Whether the condition field of an A32 instruction word passes for CPSR,
// as a replaced conditional instruction (or its conditional trap) must check.
bool Mm3dConditionPassed(std::uint32_t instruction, std::uint32_t cpsr) noexcept;

} // namespace Oot3dNativeGame
