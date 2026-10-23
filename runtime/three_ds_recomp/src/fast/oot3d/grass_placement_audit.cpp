#include "fast/oot3d/grass_placement_audit.h"

#include "fast/oot3d/grass_texture_source_cache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

namespace Fast::Oot3d {
namespace {

std::string Line(const char* format, auto... values) {
    char text[256];
    const int length = std::snprintf(text, sizeof(text), format, values...);
    return length > 0 ? std::string(text, std::min<size_t>(size_t(length), sizeof(text) - 1)) : std::string{};
}

// A bottom-up 32-bit BMP: no image library, and reviewers open it anywhere.
void WriteBmp(const std::filesystem::path& path, const GrassTexturePreview& preview) {
    const uint32_t pixels = uint32_t(preview.Width) * preview.Height;
    if (pixels == 0 || preview.Rgba8.size() < size_t(pixels) * 4U) return;
    std::ofstream stream(path, std::ios::binary);
    const auto put = [&](uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) stream.put(char((value >> (8 * i)) & 0xFFU));
    };
    stream.put('B');
    stream.put('M');
    put(54U + pixels * 4U, 4);
    put(0U, 4);
    put(54U, 4);
    put(40U, 4);
    put(preview.Width, 4);
    put(preview.Height, 4);
    put(1U, 2);
    put(32U, 2);
    put(0U, 4);
    put(pixels * 4U, 4);
    put(2835U, 4);
    put(2835U, 4);
    put(0U, 4);
    put(0U, 4);
    for (uint32_t row = preview.Height; row-- > 0;)
        for (uint32_t column = 0; column < preview.Width; ++column) {
            const auto* rgba = &preview.Rgba8[(size_t(row) * preview.Width + column) * 4U];
            stream.put(char(rgba[2]));
            stream.put(char(rgba[1]));
            stream.put(char(rgba[0]));
            stream.put(char(rgba[3]));
        }
}

struct GlobalAudit {
    explicit GlobalAudit(uint64_t interval) : Audit(interval) {}
    ~GlobalAudit() {
        const auto report = Audit.Flush();
        std::fputs(report.c_str(), stderr);
    }
    GrassPlacementAudit Audit;
    std::filesystem::path Dump;
};

GlobalAudit* Global() {
    static const std::unique_ptr<GlobalAudit> audit = []() -> std::unique_ptr<GlobalAudit> {
        const char* interval = std::getenv("TRIAEVUM_GRASS_AUDIT");
        if (interval == nullptr || *interval == '\0') return nullptr;
        const uint64_t frames = std::strtoull(interval, nullptr, 10);
        auto created = std::make_unique<GlobalAudit>(frames == 0 ? 300 : frames);
        if (const char* dump = std::getenv("TRIAEVUM_GRASS_AUDIT_DUMP"); dump != nullptr && *dump != '\0') {
            created->Dump = dump;
            std::error_code error;
            std::filesystem::create_directories(created->Dump, error);
        }
        return created;
    }();
    return audit.get();
}

} // namespace

const char* GrassRenderStatusKey(GrassRenderStatus status) noexcept {
    switch (status) {
        case GrassRenderStatus::Disabled: return "disabled";
        case GrassRenderStatus::ContractUnavailable: return "contract_unavailable";
        case GrassRenderStatus::PipelineUnavailable: return "pipeline_unavailable";
        case GrassRenderStatus::CameraUnavailable: return "camera_unavailable";
        case GrassRenderStatus::NoRules: return "no_rules";
        case GrassRenderStatus::NoBudget: return "no_budget";
        case GrassRenderStatus::NoMasks: return "no_masks";
        case GrassRenderStatus::NoMatchingTexture: return "no_matching_texture";
        case GrassRenderStatus::NoScopedSurfaces: return "no_scoped_surfaces";
        case GrassRenderStatus::PlacementPending: return "placement_pending";
        case GrassRenderStatus::NoAnchors: return "no_anchors";
        case GrassRenderStatus::Culled: return "culled";
        case GrassRenderStatus::Drawn: return "drawn";
        case GrassRenderStatus::Error: return "error";
    }
    return "unknown";
}

void GrassPlacementAudit::RecordSource(const GrassAuditTexture& texture, std::string_view outcome,
                                       uint32_t vertices) {
    std::scoped_lock lock(mMutex);
    const TextureKey key{texture.Observed, texture.Width, texture.Height};
    // Only the draws the rules would have admitted are catalog candidates.
    if (!texture.Configured && outcome != "unconfigured") return;
    if (mSeen.insert(key).second) {
        mNewTextures.push_back(texture);
        mNewThisWindow.insert(key);
    }
    if (!texture.Configured) {
        auto& count = mCandidates[key];
        ++count.Events;
        count.Amount += vertices;
        return;
    }
    // Report tokens are space-free; some rejection reasons are phrases.
    std::string token(outcome);
    std::replace(token.begin(), token.end(), ' ', '_');
    auto& count = mSources[{texture.Observed, texture.Width, texture.Height, std::move(token)}];
    ++count.Events;
    count.Amount += vertices;
}

void GrassPlacementAudit::RecordPlacement(uint64_t ruleTexture, std::string_view outcome, uint64_t anchors) {
    std::scoped_lock lock(mMutex);
    auto& count = mPlacements[{ruleTexture, std::string(outcome)}];
    ++count.Events;
    count.Amount += anchors;
}

void GrassPlacementAudit::RecordError(std::string_view reason) {
    std::scoped_lock lock(mMutex);
    ++mErrors[std::string(reason)];
}

std::string GrassPlacementAudit::RecordPass(uint64_t frameId, GrassRenderStatus status, uint32_t visibleBlades,
                                            const GrassAuditResources& cache, uint32_t thinningCut) {
    std::scoped_lock lock(mMutex);
    if (!mWindowOpen) {
        mWindowOpen = true;
        mWindowFirst = frameId;
    }
    mWindowLast = frameId;
    ++mPasses;
    ++mStatuses[status];
    mVisibleMinimum = std::min<uint64_t>(mVisibleMinimum, visibleBlades);
    mVisibleMaximum = std::max<uint64_t>(mVisibleMaximum, visibleBlades);
    mThinningCutMinimum = std::min<uint64_t>(mThinningCutMinimum, thinningCut);
    mCacheMaximum.ResidentBytes = std::max(mCacheMaximum.ResidentBytes, cache.ResidentBytes);
    mCacheMaximum.Entries = std::max(mCacheMaximum.Entries, cache.Entries);
    mCacheMaximum.Pending = std::max(mCacheMaximum.Pending, cache.Pending);
    mCacheMaximum.GpuDeviceLocal = std::max(mCacheMaximum.GpuDeviceLocal, cache.GpuDeviceLocal);
    mCacheMaximum.GpuHostVisible = std::max(mCacheMaximum.GpuHostVisible, cache.GpuHostVisible);
    mCacheMaximum.MemoryShift = std::max(mCacheMaximum.MemoryShift, cache.MemoryShift);
    mEvictionsLast = cache.Evictions;
    return frameId - mWindowFirst + 1U < mInterval ? std::string{} : Report();
}

std::string GrassPlacementAudit::Flush() {
    std::scoped_lock lock(mMutex);
    return mPasses == 0 ? std::string{} : Report();
}

std::string GrassPlacementAudit::Report() {
    // thinning_cut_min: the lowest budget cut of the window, in buckets out of
    // kGrassBudgetBuckets (that value: nothing thinned).
    std::string report = Line("TRIAEVUM_GRASS_AUDIT frames=%llu-%llu passes=%llu visible_min=%llu visible_max=%llu "
                              "thinning_cut_min=%llu",
                              (unsigned long long)mWindowFirst, (unsigned long long)mWindowLast,
                              (unsigned long long)mPasses, (unsigned long long)mVisibleMinimum,
                              (unsigned long long)mVisibleMaximum, (unsigned long long)mThinningCutMinimum);
    for (const auto& [key, passes] : mStatuses)
        report += Line(" %s=%llu", GrassRenderStatusKey(key), (unsigned long long)passes);
    report += Line(" cache_resident_mib_max=%llu cache_entries_max=%llu cache_pending_max=%llu cache_evictions=%llu",
                   (unsigned long long)(mCacheMaximum.ResidentBytes >> 20U), (unsigned long long)mCacheMaximum.Entries,
                   (unsigned long long)mCacheMaximum.Pending,
                   (unsigned long long)(mEvictionsLast - std::min(mEvictionsBefore, mEvictionsLast)));
    report += Line(" gpu_device_mib_max=%llu gpu_host_mib_max=%llu memory_shift_max=%llu\n",
                   (unsigned long long)(mCacheMaximum.GpuDeviceLocal >> 20U),
                   (unsigned long long)(mCacheMaximum.GpuHostVisible >> 20U),
                   (unsigned long long)mCacheMaximum.MemoryShift);
    for (const auto& [key, count] : mSources)
        report += Line("TRIAEVUM_GRASS_AUDIT_SOURCE texture=%016llx size=%ux%u outcome=%s draws=%llu vertices=%llu\n",
                       (unsigned long long)std::get<0>(key), unsigned(std::get<1>(key)), unsigned(std::get<2>(key)),
                       std::get<3>(key).c_str(), (unsigned long long)count.Events, (unsigned long long)count.Amount);
    for (const auto& [key, count] : mPlacements)
        report += Line("TRIAEVUM_GRASS_AUDIT_PLACEMENT texture=%016llx outcome=%s surfaces=%llu anchors=%llu\n",
                       (unsigned long long)std::get<0>(key), std::get<1>(key).c_str(),
                       (unsigned long long)count.Events, (unsigned long long)count.Amount);
    for (const auto& [reason, passes] : mErrors)
        report += Line("TRIAEVUM_GRASS_AUDIT_ERROR passes=%llu reason=", (unsigned long long)passes) + reason + '\n';
    // Candidates by draw volume, the largest surfaces first.
    std::vector<std::pair<TextureKey, Count>> candidates(mCandidates.begin(), mCandidates.end());
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.second.Amount > b.second.Amount; });
    for (const auto& [key, count] : candidates)
        report += Line("TRIAEVUM_GRASS_AUDIT_CANDIDATE texture=%016llx size=%ux%u draws=%llu vertices=%llu new=%d\n",
                       (unsigned long long)std::get<0>(key), unsigned(std::get<1>(key)), unsigned(std::get<2>(key)),
                       (unsigned long long)count.Events, (unsigned long long)count.Amount,
                       mNewThisWindow.contains(key) ? 1 : 0);

    mWindowOpen = false;
    mSources.clear();
    mPlacements.clear();
    mStatuses.clear();
    mErrors.clear();
    mPasses = 0;
    mVisibleMinimum = UINT64_MAX;
    mVisibleMaximum = 0;
    mThinningCutMinimum = kGrassBudgetCutAll;
    mCacheMaximum = {};
    mEvictionsBefore = mEvictionsLast;
    mCandidates.clear();
    mNewThisWindow.clear();
    return report;
}

std::vector<GrassAuditTexture> GrassPlacementAudit::TakeNewTextures() {
    std::scoped_lock lock(mMutex);
    return std::exchange(mNewTextures, {});
}

bool GrassPlacementAuditEnabled() noexcept {
    return Global() != nullptr;
}

void AuditGrassSource(const GrassAuditTexture& texture, std::string_view outcome, uint32_t vertices) {
    if (auto* global = Global()) global->Audit.RecordSource(texture, outcome, vertices);
}

void AuditGrassPlacement(uint64_t ruleTexture, std::string_view outcome, uint64_t anchors) {
    if (auto* global = Global()) global->Audit.RecordPlacement(ruleTexture, outcome, anchors);
}

void AuditGrassError(std::string_view reason) {
    if (auto* global = Global()) global->Audit.RecordError(reason);
}

void AuditGrassPass(uint64_t frameId, GrassRenderStatus status, uint32_t visibleBlades,
                    const GrassAuditResources& cache, uint32_t thinningCut) {
    auto* global = Global();
    if (global == nullptr) return;
    const auto report = global->Audit.RecordPass(frameId, status, visibleBlades, cache, thinningCut);
    if (report.empty()) return;
    std::fputs(report.c_str(), stderr);
    if (global->Dump.empty()) return;
    for (const auto& texture : global->Audit.TakeNewTextures())
        WriteBmp(global->Dump / Line("%s%016llx_%ux%u.bmp", texture.Configured ? "rule_" : "",
                                     (unsigned long long)texture.Observed, unsigned(texture.Width),
                                     unsigned(texture.Height)),
                 GrassTextureSourceCache::Instance().AcquirePreview(texture.Observed));
}

} // namespace Fast::Oot3d
