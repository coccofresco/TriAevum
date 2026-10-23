#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace Fast::Oot3d {
// Renderer thread only. Disabled path does not read a clock, allocate, lock or
// issue GPU queries. CPU timings nest; do not add inclusive scopes together.
enum class GrassStage : uint8_t {
    Source,
    Overlay,
    Memo,
    Eligibility,
    TextureHash,
    Tev,
    Geometry,
    World,
    Publish,
    Requests,
    Placement,
    Selection,
    Compaction,
    Lighting,
    Shadows,
    Draw,
    Prepare,
    Interaction,
    Upload,
    // Selection sub-stages (nested inside Selection): cluster traversal and
    // merge, cluster-order sort, per-root evaluation including worker waits,
    // index/batch assembly. Diagnostics covers sampled log-only scans.
    SelectTraverse,
    SelectOrder,
    SelectEvaluate,
    SelectAssemble,
    Diagnostics,
    // The exact per-view pass over cached cluster candidates (nested in Selection).
    SelectRefine,
    Count
};
inline constexpr size_t kGrassStageCount = static_cast<size_t>(GrassStage::Count);
inline bool GrassStageProfileEnabled() {
    static const bool enabled = [] {
        const auto* s = std::getenv("TRIAEVUM_GRASS_PROFILE");
        return s && std::string_view(s) == "1";
    }();
    return enabled;
}
struct GrassStageFrame {
    uint64_t Frame = 0;
    std::array<double, kGrassStageCount> Ms{};
    std::array<uint64_t, kGrassStageCount> Calls{};
    uint64_t MemoHit = 0, MemoMiss = 0, NeitherHit = 0, SelectionHit = 0, SelectionMiss = 0;
    uint64_t DiskHit = 0, DiskMiss = 0, DiskReject = 0, DiskWrite = 0;
    uint64_t PlacementHit = 0, PlacementMiss = 0, SelectionUploadReuse = 0, CompactionReuse = 0;
    double WorkerMs = 0;
    uint64_t SelectClusters = 0, SelectCandidates = 0, SelectEvaluated = 0, SelectVisible = 0;
    // Fork/join balance of the selection jobs: summed job time against the
    // slowest job (the critical path the render thread waits for).
    uint64_t TraverseJobs = 0, EvaluateJobs = 0, RefineJobs = 0;
    double TraverseJobMs = 0, TraverseMaxJobMs = 0, EvaluateJobMs = 0, EvaluateMaxJobMs = 0;
    double RefineJobMs = 0, RefineMaxJobMs = 0;
    uint64_t CandidateHit = 0, CandidateMiss = 0, CandidateViolation = 0;
    uint64_t SelectHash = 0;
    uint64_t RasterDepthBatches = 0, RasterDepthInstances = 0, ExplicitDepthBatches = 0, ExplicitDepthInstances = 0;
    uint64_t SignatureRepeat = 0, ClipSame = 0, ClipChanged = 0, WorldInvalidations = 0;
};
inline thread_local std::array<GrassStageFrame, 8> GrassStageFrames{};
inline thread_local uint64_t GrassStageCurrentFrame = 0;
inline GrassStageFrame& GrassStageCurrent() {
    return GrassStageFrames[GrassStageCurrentFrame % 8];
}
inline void BeginGrassStageFrame(uint64_t frame) {
    if (!GrassStageProfileEnabled())
        return;
    GrassStageCurrentFrame = frame;
    GrassStageCurrent() = {};
    GrassStageCurrent().Frame = frame;
}
// Times one fork/join job on a worker thread; the caller folds the results
// into the renderer thread's frame after the join.
struct GrassJobTiming {
    double Ms = 0;
    static std::chrono::steady_clock::time_point Start() {
        return GrassStageProfileEnabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    }
    void Finish(std::chrono::steady_clock::time_point start) {
        if (GrassStageProfileEnabled())
            Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
};
inline void AddGrassJobTimings(uint64_t& jobs, double& total, double& maximum, const GrassJobTiming* timings,
                               size_t count) {
    if (!GrassStageProfileEnabled())
        return;
    double slowest = 0;
    for (size_t i = 0; i < count; ++i) {
        total += timings[i].Ms;
        slowest = std::max(slowest, timings[i].Ms);
    }
    jobs += count;
    maximum += slowest;
}
class GrassStageTimer {
  public:
    explicit GrassStageTimer(GrassStage stage) : mStage(stage), mEnabled(GrassStageProfileEnabled()) {
        if (mEnabled)
            mStart = std::chrono::steady_clock::now();
    }
    ~GrassStageTimer() {
        Stop();
    }
    void Stop() {
        if (!mEnabled)
            return;
        auto& frame = GrassStageCurrent();
        const auto i = static_cast<size_t>(mStage);
        frame.Ms[i] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mStart).count();
        ++frame.Calls[i];
        mEnabled = false;
    }

  private:
    GrassStage mStage;
    bool mEnabled;
    std::chrono::steady_clock::time_point mStart{};
};
inline void ReportGrassStageFrame(uint64_t frame, double total, double compact, double blades, double shadows,
                                  double lighting) {
    if (!GrassStageProfileEnabled())
        return;
    const auto& f = GrassStageFrames[frame % 8];
    if (f.Frame != frame)
        return;
    constexpr std::array<const char*, kGrassStageCount> names{
        "source",  "overlay", "memo",     "eligibility", "texture_hash", "tev",        "geometry",
        "world",   "publish", "requests", "placement",   "selection",    "compaction", "lighting",
        "shadows", "draw",    "prepare",  "interaction", "upload",
        "select_traverse", "select_order", "select_evaluate", "select_assemble", "diagnostics", "select_refine"
    };
    std::fprintf(stderr, "TRIAEVUM_GRASS_PROFILE frame=%llu", static_cast<unsigned long long>(frame));
    for (size_t i = 0; i < names.size(); ++i)
        std::fprintf(stderr, " %s_ms=%.6f %s_n=%llu", names[i], f.Ms[i], names[i],
                     static_cast<unsigned long long>(f.Calls[i]));
    std::fprintf(stderr,
                 " memo_hit=%llu memo_miss=%llu neither_hit=%llu selection_hit=%llu selection_miss=%llu "
                 "placement_hit=%llu placement_miss=%llu disk_hit=%llu disk_miss=%llu disk_reject=%llu disk_write=%llu "
                 "selection_upload_reuse=%llu compaction_reuse=%llu gpu_total_ms=%.6f gpu_compaction_ms=%.6f "
                 "gpu_blades_ms=%.6f gpu_shadows_ms=%.6f gpu_lighting_ms=%.6f worker_ms=%.6f signature_repeat=%llu "
                 "clip_same=%llu clip_changed=%llu world_invalidations=%llu select_clusters=%llu "
                 "select_candidates=%llu select_evaluated=%llu select_visible=%llu raster_depth_batches=%llu "
                 "raster_depth_instances=%llu explicit_depth_batches=%llu explicit_depth_instances=%llu "
                 "traverse_jobs=%llu traverse_job_ms=%.6f traverse_max_job_ms=%.6f evaluate_jobs=%llu "
                 "evaluate_job_ms=%.6f evaluate_max_job_ms=%.6f refine_jobs=%llu refine_job_ms=%.6f "
                 "refine_max_job_ms=%.6f candidate_hit=%llu candidate_miss=%llu candidate_violation=%llu select_hash=%016llx\n",
                 (unsigned long long)f.MemoHit, (unsigned long long)f.MemoMiss, (unsigned long long)f.NeitherHit,
                 (unsigned long long)f.SelectionHit, (unsigned long long)f.SelectionMiss,
                 (unsigned long long)f.PlacementHit, (unsigned long long)f.PlacementMiss, (unsigned long long)f.DiskHit,
                 (unsigned long long)f.DiskMiss, (unsigned long long)f.DiskReject, (unsigned long long)f.DiskWrite,
                 (unsigned long long)f.SelectionUploadReuse, (unsigned long long)f.CompactionReuse, total, compact,
                 blades, shadows, lighting, f.WorkerMs, (unsigned long long)f.SignatureRepeat,
                 (unsigned long long)f.ClipSame, (unsigned long long)f.ClipChanged,
                 (unsigned long long)f.WorldInvalidations, (unsigned long long)f.SelectClusters,
                 (unsigned long long)f.SelectCandidates, (unsigned long long)f.SelectEvaluated,
                 (unsigned long long)f.SelectVisible, (unsigned long long)f.RasterDepthBatches,
                 (unsigned long long)f.RasterDepthInstances, (unsigned long long)f.ExplicitDepthBatches,
                 (unsigned long long)f.ExplicitDepthInstances, (unsigned long long)f.TraverseJobs, f.TraverseJobMs,
                 f.TraverseMaxJobMs, (unsigned long long)f.EvaluateJobs, f.EvaluateJobMs, f.EvaluateMaxJobMs,
                 (unsigned long long)f.RefineJobs, f.RefineJobMs, f.RefineMaxJobMs, (unsigned long long)f.CandidateHit,
                 (unsigned long long)f.CandidateMiss, (unsigned long long)f.CandidateViolation,
                 (unsigned long long)f.SelectHash);
}
} // namespace Fast::Oot3d
