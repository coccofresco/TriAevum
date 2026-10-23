#pragma once

#include "fast/oot3d/grass_render_telemetry.h"
#include "fast/oot3d/grass_selection_budget.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace Fast::Oot3d {

struct GrassAuditTexture {
    // The rule key: the texture's catalog (native content) hash.
    uint64_t Observed = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    // Its own rule, or the rule of a texture it is the same image as.
    bool Configured = false;
};

// After a pass: the placement cache (resident bytes, entries, placements still
// building, evictions since it was created) and the grass passes' GPU memory.
struct GrassAuditResources {
    uint64_t ResidentBytes = 0, Entries = 0, Pending = 0, Evictions = 0;
    uint64_t GpuDeviceLocal = 0, GpuHostVisible = 0;
    // Memory pressure: candidates and visible budget divided by 2^MemoryShift.
    uint64_t MemoryShift = 0;
};

// Where each candidate surface leaves the grass pipeline, aggregated per
// report window: the scene draws that sample texture 0 (the first stage that
// dropped them, or "published"), the placement of every scoped rule surface
// and the status of every grass pass. Configured textures are reported with
// every outcome; eligible textures without a rule (the catalog candidates)
// with their draw volume, flagged new the first time they are seen.
class GrassPlacementAudit final {
  public:
    explicit GrassPlacementAudit(uint64_t interval) : mInterval(interval == 0 ? 1 : interval) {}
    void RecordSource(const GrassAuditTexture& texture, std::string_view outcome, uint32_t vertices);
    // "other_target", "pending", "failed", "no_anchors" or "ready".
    void RecordPlacement(uint64_t ruleTexture, std::string_view outcome, uint64_t anchors);
    // A grass pass that failed, with the renderer's reason.
    void RecordError(std::string_view reason);
    // One grass pass, with its budget thinning cut (kGrassBudgetCutAll when
    // every visible root was drawn). Returns the window's report when this
    // pass closes it.
    [[nodiscard]] std::string RecordPass(uint64_t frameId, GrassRenderStatus status, uint32_t visibleBlades,
                                         const GrassAuditResources& cache = {},
                                         uint32_t thinningCut = kGrassBudgetCutAll);
    // The open window's report, if it has any pass (at exit).
    [[nodiscard]] std::string Flush();
    // Configured textures and candidates first seen since the last call.
    [[nodiscard]] std::vector<GrassAuditTexture> TakeNewTextures();

  private:
    struct Count {
        uint64_t Events = 0, Amount = 0;
    };
    using TextureKey = std::tuple<uint64_t, uint16_t, uint16_t>;
    using SourceKey = std::tuple<uint64_t, uint16_t, uint16_t, std::string>;
    using PlacementKey = std::tuple<uint64_t, std::string>;
    [[nodiscard]] std::string Report();
    std::mutex mMutex;
    uint64_t mInterval;
    bool mWindowOpen = false;
    uint64_t mWindowFirst = 0, mWindowLast = 0;
    std::map<SourceKey, Count> mSources;
    std::map<PlacementKey, Count> mPlacements;
    std::map<GrassRenderStatus, uint64_t> mStatuses;
    std::map<std::string, uint64_t> mErrors;
    uint64_t mPasses = 0, mVisibleMinimum = UINT64_MAX, mVisibleMaximum = 0;
    uint64_t mThinningCutMinimum = kGrassBudgetCutAll;
    GrassAuditResources mCacheMaximum;
    uint64_t mEvictionsBefore = 0, mEvictionsLast = 0;
    std::map<TextureKey, Count> mCandidates;
    std::set<TextureKey> mSeen, mNewThisWindow;
    std::vector<GrassAuditTexture> mNewTextures;
};

[[nodiscard]] const char* GrassRenderStatusKey(GrassRenderStatus status) noexcept;

// TRIAEVUM_GRASS_AUDIT=<frames> reports on stderr every <frames> frames and
// at exit; TRIAEVUM_GRASS_AUDIT_DUMP=<dir> also writes each new configured
// texture (rule_<hash>_<w>x<h>.bmp) and candidate (<hash>_<w>x<h>.bmp) there
// once. Unset: every entry point returns before reading or writing any state.
[[nodiscard]] bool GrassPlacementAuditEnabled() noexcept;
void AuditGrassSource(const GrassAuditTexture& texture, std::string_view outcome, uint32_t vertices);
void AuditGrassPlacement(uint64_t ruleTexture, std::string_view outcome, uint64_t anchors);
void AuditGrassError(std::string_view reason);
void AuditGrassPass(uint64_t frameId, GrassRenderStatus status, uint32_t visibleBlades,
                    const GrassAuditResources& cache, uint32_t thinningCut = kGrassBudgetCutAll);

} // namespace Fast::Oot3d
