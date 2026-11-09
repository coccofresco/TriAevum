#include "mm3d_guest_patches.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;

void Mm3dGuestPatches::Add(Mm3dGuestPatchSite site) {
    if ((site.Pc & 3U) != 0U) {
        throw std::invalid_argument("MM3D guest patch site is not word aligned: " + site.Name);
    }
    if (!site.Handler) {
        throw std::invalid_argument("MM3D guest patch site has no handler: " + site.Name);
    }
    const auto at = std::lower_bound(mSites.begin(), mSites.end(), site.Pc,
                                     [](const Entry& entry, std::uint32_t pc) {
                                         return entry.Site.Pc < pc;
                                     });
    if (at != mSites.end() && at->Site.Pc == site.Pc) {
        throw std::invalid_argument("two MM3D guest patch sites replace the same instruction: " +
                                    at->Site.Name + ", " + site.Name);
    }
    SiteStatistics stats;
    stats.Name = site.Name;
    stats.Pc = site.Pc;
    mSites.insert(at, Entry{std::move(site), std::move(stats)});
}

std::size_t Mm3dGuestPatches::Verify(a32::MemoryBus& memory, const a32::Registry* registry) {
    // Guards first, for every site; then the preparations, which may ask
    // which sites are enabled (EnabledPcs) - every guard is settled by then.
    for (auto& entry : mSites) {
        entry.Stats.MismatchAddress.reset();
        entry.Stats.MissingBlock = entry.Site.InGuestCode && registry != nullptr &&
                                   a32::FindBlock(*registry, entry.Site.Pc) == nullptr;
        entry.Stats.Enabled = !entry.Stats.MissingBlock;
        for (const auto& [address, expected] : entry.Site.Guard) {
            if (!entry.Stats.Enabled) {
                break;
            }
            std::uint32_t word = 0;
            if (!memory.Read32(address, &word) || word != expected) {
                entry.Stats.Enabled = false;
                entry.Stats.MismatchAddress = address;
                break;
            }
        }
        entry.Stats.Prepared = true;
    }
    std::size_t enabled = 0;
    for (auto& entry : mSites) {
        if (entry.Stats.Enabled && entry.Site.Prepare && !entry.Site.Prepare(memory)) {
            entry.Stats.Enabled = false;
            entry.Stats.Prepared = false;
        }
        enabled += entry.Stats.Enabled ? 1U : 0U;
    }
    return enabled;
}

std::vector<std::uint32_t> Mm3dGuestPatches::EnabledPcs() const {
    std::vector<std::uint32_t> pcs;
    for (const auto& entry : mSites) {
        if (entry.Stats.Enabled) {
            pcs.push_back(entry.Site.Pc);
        }
    }
    return pcs;
}

void Mm3dGuestPatches::AddTargets(const std::vector<std::uint32_t>& targets) {
    mTargets.insert(mTargets.end(), targets.begin(), targets.end());
}

std::vector<std::uint32_t> Mm3dGuestPatches::BlockBoundaries() const {
    std::vector<std::uint32_t> boundaries = mTargets;
    for (const auto& entry : mSites) {
        if (!entry.Site.InGuestCode) {
            continue;
        }
        boundaries.push_back(entry.Site.Pc);
        if (entry.Site.ContinuesAtNext) {
            boundaries.push_back(entry.Site.Pc + 4U);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    return boundaries;
}

std::vector<std::uint32_t> Mm3dGuestPatches::MissingProductBoundaries(
    const std::vector<std::uint32_t>& sortedEntryPoints) const {
    std::vector<std::uint32_t> needed;
    bool anyEnabled = false;
    for (const auto& entry : mSites) {
        if (!entry.Stats.Enabled || !entry.Site.InGuestCode) {
            continue;
        }
        anyEnabled = true;
        needed.push_back(entry.Site.Pc);
        if (entry.Site.ContinuesAtNext && entry.Site.HandlerReturnsNext) {
            needed.push_back(entry.Site.Pc + 4U);
        }
    }
    if (anyEnabled) {
        needed.insert(needed.end(), mTargets.begin(), mTargets.end());
    }
    std::sort(needed.begin(), needed.end());
    needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
    std::vector<std::uint32_t> missing;
    for (const auto pc : needed) {
        if (!std::binary_search(sortedEntryPoints.begin(), sortedEntryPoints.end(), pc)) {
            missing.push_back(pc);
        }
    }
    return missing;
}

std::vector<Mm3dGuestPatches::SiteStatistics> Mm3dGuestPatches::Statistics() const {
    std::vector<SiteStatistics> stats;
    stats.reserve(mSites.size());
    for (const auto& entry : mSites) {
        stats.push_back(entry.Stats);
    }
    return stats;
}

Mm3dGuestPatches::Entry* Mm3dGuestPatches::Find(std::uint32_t pc) noexcept {
    const auto at = std::lower_bound(mSites.begin(), mSites.end(), pc,
                                     [](const Entry& entry, std::uint32_t value) {
                                         return entry.Site.Pc < value;
                                     });
    return at != mSites.end() && at->Site.Pc == pc ? &*at : nullptr;
}

bool Mm3dGuestPatches::Execute(std::uint32_t pc, a32::GuestState& state, a32::MemoryBus& memory,
                               a32::ExecutionResult* result, void* user) {
    auto* entry = static_cast<Mm3dGuestPatches*>(user)->Find(pc);
    if (entry == nullptr || !entry->Stats.Enabled) {
        return false;
    }
    auto* self = static_cast<Mm3dGuestPatches*>(user);
    if (self->mTraceSink) {
        self->mTrail[self->mTrailNext++ % self->mTrail.size()] = pc;
    }
    if (!entry->SkipThreads.empty()) {
        const auto skip = std::find(entry->SkipThreads.begin(), entry->SkipThreads.end(),
                                    state.thread_pointer);
        if (skip != entry->SkipThreads.end()) {
            entry->SkipThreads.erase(skip);
            self->Trace(*entry, state.thread_pointer, state.r[14], "skip_once_resume", pc);
            return false;
        }
    }
    auto* patches = static_cast<Mm3dGuestPatches*>(user);
    const std::uint32_t lrAtEntry = state.r[14];
    const auto next = entry->Site.Handler(state, memory);
    if (!next.has_value()) {
        ++entry->Stats.Declined;
        self->Trace(*entry, state.thread_pointer, lrAtEntry, "declined", pc);
        return false;
    }
    ++entry->Stats.Replacements;
    self->Trace(*entry, state.thread_pointer, lrAtEntry, "replaced", *next);
    if (patches->mProductReturnCheck && !patches->DeclaredContinuation(entry->Site, *next, lrAtEntry)) {
        if (entry->Stats.ReturnViolations++ == 0U)
            std::fprintf(stderr,
                         "MM3D guest patch %s (0x%08X) continued at undeclared 0x%08X "
                         "(HandlerReturnsNext=%d, lr=0x%08X)\n",
                         entry->Site.Name.c_str(), pc, *next, entry->Site.HandlerReturnsNext ? 1 : 0,
                         lrAtEntry);
        ++patches->mReturnViolations;
    }
    state.r[15] = *next;
    *result = {a32::ExitKind::Branch, *next, a32::FallbackReason::None, pc};
    return true;
}

bool Mm3dGuestPatches::DeclaredContinuation(const Mm3dGuestPatchSite& site, std::uint32_t next,
                                            std::uint32_t lrAtEntry) const noexcept {
    if (next == site.Pc + 4U) return site.HandlerReturnsNext;
    if (std::find(mTargets.begin(), mTargets.end(), next) != mTargets.end()) return true;
    return (next & ~1U) == (lrAtEntry & ~1U);
}

void Mm3dGuestPatches::SetTrace(std::string prefix,
                                std::function<void(const std::string&)> sink) {
    mTracePrefix = std::move(prefix);
    mTraceSink = mTracePrefix.empty() ? nullptr : std::move(sink);
}

bool Mm3dGuestPatches::TraceMatches(const Entry& entry) const {
    return entry.Site.Name.rfind(mTracePrefix, 0) == 0 ||
           (!entry.Site.TraceName.empty() && entry.Site.TraceName.rfind(mTracePrefix, 0) == 0);
}

void Mm3dGuestPatches::TraceVerify() const {
    if (!mTraceSink) return;
    for (const auto& entry : mSites) {
        if (!TraceMatches(entry)) continue;
        char line[256];
        std::snprintf(line, sizeof line,
                      "MM3D guest patch trace: verify site=%s%s%s pc=0x%08X enabled=%d "
                      "mismatch=0x%08X missing_block=%d",
                      entry.Site.Name.c_str(), entry.Site.TraceName.empty() ? "" : " action=",
                      entry.Site.TraceName.c_str(), entry.Site.Pc, entry.Stats.Enabled ? 1 : 0,
                      entry.Stats.MismatchAddress.value_or(0U), entry.Stats.MissingBlock ? 1 : 0);
        mTraceSink(line);
    }
}

void Mm3dGuestPatches::Trace(const Entry& entry, std::uint32_t thread, std::uint32_t lr,
                             const char* what, std::uint32_t next) {
    if (!mTraceSink || !TraceMatches(entry)) return;
    char line[640];
    int n = std::snprintf(line, sizeof line,
                          "MM3D guest patch trace: %s site=%s%s%s pc=0x%08X thread=0x%08X lr=0x%08X "
                          "next=0x%08X replacements=%llu declined=%llu pending_skips=%zu trail=",
                          what, entry.Site.Name.c_str(), entry.Site.TraceName.empty() ? "" : " action=",
                          entry.Site.TraceName.c_str(), entry.Site.Pc, thread, lr, next,
                          static_cast<unsigned long long>(entry.Stats.Replacements),
                          static_cast<unsigned long long>(entry.Stats.Declined),
                          entry.SkipThreads.size());
    for (std::size_t i = 0; i < mTrail.size(); ++i) {
        if (n <= 0 || n >= static_cast<int>(sizeof line) - 12) break;
        const auto pc = mTrail[(mTrailNext + i) % mTrail.size()];
        if (pc != 0U) n += std::snprintf(line + n, sizeof line - n, "%08X,", pc);
    }
    mTraceSink(line);
}

void Mm3dGuestPatches::SkipOnce(std::uint32_t pc, std::uint32_t thread) {
    if (auto* entry = Find(pc)) {
        entry->SkipThreads.push_back(thread);
    }
}

void Mm3dGuestPatches::ResetPending() noexcept {
    for (auto& entry : mSites) {
        entry.SkipThreads.clear();
    }
}

bool Mm3dConditionPassed(std::uint32_t instruction, std::uint32_t cpsr) noexcept {
    return a32::ConditionPassed(static_cast<a32::Condition>(instruction >> 28U), cpsr);
}

} // namespace Oot3dNativeGame
