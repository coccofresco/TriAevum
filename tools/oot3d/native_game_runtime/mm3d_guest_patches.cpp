#include "mm3d_guest_patches.h"

#include <algorithm>
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
    std::size_t enabled = 0;
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

std::vector<std::uint32_t> Mm3dGuestPatches::BlockBoundaries() const {
    std::vector<std::uint32_t> boundaries;
    for (const auto& entry : mSites) {
        if (!entry.Site.InGuestCode) {
            continue;
        }
        boundaries.push_back(entry.Site.Pc);
        boundaries.push_back(entry.Site.Pc + 4U);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    return boundaries;
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
    const auto next = entry->Site.Handler(state, memory);
    if (!next.has_value()) {
        ++entry->Stats.Declined;
        return false;
    }
    ++entry->Stats.Replacements;
    state.r[15] = *next;
    *result = {a32::ExitKind::Branch, *next, a32::FallbackReason::None, pc};
    return true;
}

bool Mm3dConditionPassed(std::uint32_t instruction, std::uint32_t cpsr) noexcept {
    return a32::ConditionPassed(static_cast<a32::Condition>(instruction >> 28U), cpsr);
}

} // namespace Oot3dNativeGame
