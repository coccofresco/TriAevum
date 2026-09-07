#pragma once

#include "fast/oot3d/effect_graph.h"
#include "fast/oot3d/graphics_settings.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fast::Oot3d {

enum class DisplayEffectPass : uint8_t {
    Guides,
    Cacao,
    HiZ,
    Reflections,
    Outline,
    Composite,
    Motion,
    Taa,
    TemporalUpscaler,
    Nis,
    Smaa,
    Scanout,
    WorkingColor,
    Count,
};

[[nodiscard]] std::string_view DisplayEffectPassName(
    DisplayEffectPass pass) noexcept;

struct DisplayEffectPlanInput {
    bool AlphaOverlay = false;
    bool WorldSurface = false;
    bool PerspectiveAvailable = false;
    bool CameraAvailable = false;
    bool SmaaAvailable = false;
    bool ForceTaa = false;
    bool ForceMotion = false;
    AmbientOcclusionMode AmbientOcclusion = AmbientOcclusionMode::Off;
    ReflectionMode Reflections = ReflectionMode::Off;
    ToonMode Toon = ToonMode::Off;
    bool OutlineEnabled = false;
    AntiAliasingMode AntiAliasing = AntiAliasingMode::Off;
    UpscalerProvider Upscaler = UpscalerProvider::Nis;
    bool GuideDiagnostics = false;
};

struct DisplayEffectPlan {
    ReflectionMode ReflectionProvider = ReflectionMode::Off;
    UpscalerProvider Upscaler = UpscalerProvider::Nis;

    // Compatibility projections. They are populated from Graph after it is
    // compiled and are never the authority used by renderer dispatch.
    bool Cacao = false;
    bool Outline = false;
    bool Reflections = false;
    bool FidelityFxSssr = false;
    bool TemporalMetadata = false;
    bool Taa = false;
    bool Smaa = false;
    bool Nis = false;
    bool Fsr = false;
    bool Dlss = false;
    bool TemporalUpscaler = false;
    bool MotionVectors = false;
    bool RequiresGuideTarget = false;
    CompiledEffectGraph Graph;

    [[nodiscard]] const CompiledEffectPass* FindPass(
        DisplayEffectPass pass) const noexcept;
    [[nodiscard]] bool Enabled(DisplayEffectPass pass) const noexcept;
    [[nodiscard]] std::span<const EffectResourceUse> Bindings(
        DisplayEffectPass pass) const noexcept;
    [[nodiscard]] size_t DeclaredBindingCount() const noexcept;
};

enum class DisplayEffectExecutionOutcome : uint8_t {
    Unresolved,
    Executed,
    Reused,
    Fused,
    Skipped,
    Failed,
};

struct DisplayEffectExecutionSummary {
    uint32_t DeclaredPassMask = 0;
    uint32_t ExecutedPassMask = 0;
    uint32_t ReusedPassMask = 0;
    uint32_t FusedPassMask = 0;
    uint32_t SkippedPassMask = 0;
    uint32_t FailedPassMask = 0;
    uint32_t UnresolvedPassMask = 0;
    uint32_t DeclaredPassCount = 0;
    uint32_t ExecutedPassCount = 0;
    uint32_t ReusedPassCount = 0;
    uint32_t FusedPassCount = 0;
    uint32_t SkippedPassCount = 0;
    uint32_t FailedPassCount = 0;
    uint32_t UnresolvedPassCount = 0;
    uint32_t ActiveBindingCount = 0;
    uint32_t UndeclaredRecordCount = 0;
    uint32_t DuplicateRecordCount = 0;

    [[nodiscard]] bool FullyAccounted() const noexcept {
        return UnresolvedPassCount == 0U &&
               UndeclaredRecordCount == 0U &&
               DuplicateRecordCount == 0U;
    }
    [[nodiscard]] bool Successful() const noexcept {
        return FullyAccounted() && FailedPassCount == 0U;
    }
};

// Fixed-size execution state for the compiled display graph. This is kept
// separate from the graph declaration so one immutable plan can describe
// multiple display transfers without mutable backend state leaking into it.
class DisplayEffectExecutionLedger {
  public:
    explicit DisplayEffectExecutionLedger(
        const DisplayEffectPlan& plan) noexcept;

    [[nodiscard]] bool Record(
        DisplayEffectPass pass,
        DisplayEffectExecutionOutcome outcome) noexcept;
    [[nodiscard]] DisplayEffectExecutionSummary Summary() const noexcept;

  private:
    static constexpr size_t kPassCount =
        static_cast<size_t>(DisplayEffectPass::Count);

    const DisplayEffectPlan* mPlan = nullptr;
    std::array<DisplayEffectExecutionOutcome, kPassCount> mOutcomes{};
    uint32_t mUndeclaredRecordCount = 0;
    uint32_t mDuplicateRecordCount = 0;
};

// Pure per-view policy. Backend code executes the resulting pass flags but no
// longer re-derives feature dependencies from UI/environment state.
[[nodiscard]] DisplayEffectPlan BuildDisplayEffectPlan(
    const DisplayEffectPlanInput& input);

} // namespace Fast::Oot3d
